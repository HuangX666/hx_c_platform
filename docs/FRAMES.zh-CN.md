# 通用消息帧

[English](FRAMES.md)

TCP、本地流套接字（Unix Domain Socket / 命名管道）和 TLS 连接在底层都是无边界的字节流：发送方调用一次写函数，接收方可能分两次才读完（半包），也可能一次读出连在一起的两三条消息（粘包）。当你的业务希望每次调用接收函数都直接拿到**一条完整的 JSON 或二进制消息**，但又不需要 `hx_ipc` 复杂的握手、心跳与请求路由时，可以使用 `hx_os_frame` 在任意字节流连接外层套上一层轻量的 **4 字节大端长度前缀帧（Length-Prefixed Framing）**。

你可以直接编译并运行仓库自带的示例程序 [framed_messages.c](../examples/framed_messages.c) 来观察完整工作流（按[示例构建说明](../examples/README.zh-CN.md#run)开启 `HX_OS_BUILD_EXAMPLES=ON` 并构建目标 `hx_os_framed_messages` 即可，无需命令行参数或外部文件）：

```sh
cmake --build build --config Release --target hx_os_framed_messages
```

运行后会输出：

```text
{"device":"door-1","request":42}
```

为了无需起双端网络就能单进程演示，该示例先使用内存流（`hx_os_frame_from_stream`）写入带长度前缀的 JSON、将读写游标 `seek` 回开头后再完整读出。在实际网络工程中，只需将创建函数换为 `hx_os_frame_from_socket()` 或 `hx_os_frame_from_tls()`（并去掉 `seek`），即可在真实连接上收发同样格式的完整消息。你也可以按[最小 C 工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为自己的 `main.c` 模板。

## 1. 接收完整消息

包含头文件 `hx_os_frame.h`。`hx_os_frame` 的线协议格式极其简单：**4 字节大端无符号整数（`uint32_t`）表示的正文长度，紧跟对应字节数的消息正文**。无论是 TCP、本地流套接字、TLS 还是自定义读写回调，都共享完全相同的线格式（注意：UDP 本身已具备数据报边界，不需要也不使用此封装）。如果对端用 Python、Go、C# 或 Java 编写，只需按相同的 4 字节大端长度头收发即可直接互通。

```c
hx_os_frame *frame = NULL;
hx_string json = HX_STRING_INIT;

hx_os_status status =
	hx_os_frame_from_socket(NULL, connected_socket, 0, &frame, NULL);
if (status == HX_OS_OK)
	status = hx_os_frame_receive_text_until(frame,
		hx_os_deadline_after_ms(5000), &json, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&json));
```

这里的 `connected_socket` 是已经建立连接的 TCP 或本地流套接字。`hx_os_frame` 仅**借用**该底层连接句柄，因此在退出清理时，必须**先销毁 `hx_os_frame`，最后再关闭底层套接字**。即便底层网络一次只收到半帧、或者一次粘连了多帧，`hx_os_frame_receive_text_until()` 也会在内部自动累积与切分，每次调用严格返回一条完整且通过 UTF-8 校验的文本消息，随后你就可以直接将其交给配置/JSON 解析器处理。

## 2. 发送和重试

```c
int accepted = 0;

status = hx_os_frame_send_text_until(frame,
	hx_string_view_from_cstr("{\"request\":42}"),
	hx_os_deadline_after_ms(5000), &accepted, NULL);
if (status == HX_OS_TIMEOUT && accepted)
	status = hx_os_frame_flush_until(
		frame, hx_os_deadline_after_ms(5000), NULL);
```

发送时，库会先将完整消息拷贝到帧对象的内部发送缓冲区中，然后再向底层传输写入：
- **为什么超时后要检查 `accepted`**：如果 `hx_os_frame_send_text_until()` 因超时返回 `HX_OS_TIMEOUT`，且输出参数 `accepted` 为非零（真），说明这条消息**已经被帧对象完整接管并可能已发出了前半截字节**。此时**切勿再次调用 `send_text_until` 重发同一条消息**（否则会在流中造成重复或破坏帧结构），而应当调用 `hx_os_frame_flush_until()` 继续把内部暂存的剩余字节冲刷出去！
- **冲刷成功的含义**：`flush_until` 返回 `HX_OS_OK` 仅代表本端已将整帧字节写入底层传输，不代表对端业务代码已经处理完毕；若业务要求端到端确认，请在应用层消息（如 JSON）中设计请求 ID 与确认回复。

## 3. 所有权和限制

- **单帧大小限制与错误码语义**：
  - 默认情况下，接收和发送每个方向最多缓存 `1 MiB` 的单条消息（创建时可显式指定上限，最高允许 `64 MiB`；长度为 0 的空消息也是合法帧）。
  - 如果接收中途超时（`HX_OS_TIMEOUT`）或调用方输出缓冲区分配失败，已读入的半帧进度会完好保留在内部供下次调用继续读取。
  - 如果调用 `receive_text_until` 时发现消息正文包含非法 UTF-8 字节序列，函数会报错但不会丢弃该帧——你依然可以改调二进制入口 `hx_os_frame_receive_until()` 把这帧原始字节取出来。
  - 当对端在**两帧边界之间**正常关闭连接时，返回 `HX_OS_EOF`；当对端在**半帧传输中途**突然断开时，返回 `HX_OS_TRUNCATED_INPUT`；若收到的 4 字节长度头超过本端设定的最大帧上限，则立即返回 `HX_OS_LIMIT` 并停止后续读取。
- **多线程并发规则**：
  - 多个线程同时调用发送接口时，库会在内部自动串行化（排在后面的发送者等待前一个发送者完成的时间也计入其自身的 `deadline`；若传入 `HX_OS_DEADLINE_NOW` 且发送方向正被占用，则直接返回 `HX_OS_TIMEOUT`）。
  - 当底层传输支持全双工（如 Socket 或 TLS）时，**允许 1 个接收线程与 1 个发送线程同时并发工作**。
  - 在关闭底层连接、调用 `abort` 或销毁 `hx_os_frame` 之前，调用方必须确保没有其他线程正在执行收发调用。注意：基于文件/内存流（`hx_os_stream`）的适配器共享同一个读写游标，需要外部自行串行化，且流适配器不会让原本不支持超时的底层流凭空获得硬超时能力。

> [!WARNING]
> **销毁前请先排空或显式 `abort`**
> 如果 `hx_os_frame` 内部仍滞留着未读完的半帧或未冲刷完的发送帧，直接调用销毁函数会返回 `HX_OS_BUSY`（库绝不会静默丢弃半截数据）。请先完成接收/冲刷，或者在决定放弃该连接时先显式调用 `abort` 清空状态，然后再销毁 `hx_os_frame`。C++11 的 `hx::Frame` 同样借用调用方持久持有的 `hx_os_frame *` 指针，并通过 `hx::Result` 返回错误码与发送接管标志。

## 4. 运行示例

开启 `-DHX_OS_BUILD_EXAMPLES=ON` 后构建 `hx_os_framed_messages` 即可运行 [framed_messages.c](../examples/framed_messages.c)。该模块由 CMake 开关 `HX_OS_ENABLE_FRAME` 控制（默认开启），各底层连接工厂函数（`from_socket`、`from_tls`、`from_stream`）还需对应的底层模块处于开启状态。

如果你使用的是由一对单向管道组成的双向通道（例如子进程的标准输入/输出管道），可以直接调用 `hx_os_frame_from_streams()`（C++11 对应 `hx::Frame::fromStreams()`）分别传入独立的读取流和写入流（单向通信时可将另一方向传 `NULL`），无需自己编写粘合回调函数。
