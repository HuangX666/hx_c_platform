# 通用消息帧

[English](FRAMES.md)

## 1. 接收完整消息

包含 `hx_os_frame.h`。线上格式为四字节大端无符号长度，随后是对应长度的消息内容。TCP、本地流套接字、TLS 和自定义回调使用相同格式；UDP 保留数据报语义，不使用这个封装。其他语言只需实现相同的长度前缀。

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

`connected_socket` 是已经连接的 TCP／本地套接字。消息帧借用它，先销毁消息帧，再关闭套接字。即使底层一次只读到半条消息，或者一次收到多条消息，接收入口也只返回一条完整消息。返回 UTF-8 文本后，可以继续用配置／文档接口解析 JSON。

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

库先复制完整消息，再开始发送。`accepted` 为真时，超时后继续冲刷保留的消息；再次发送相同消息会造成重复。冲刷成功只说明本端传输取得进展，不代表对端业务已经处理。可靠业务仍需要请求编号和确认回复。

## 3. 所有权和限制

默认每个方向最多保留 1 MiB 消息；显式上限最多 64 MiB。允许空消息。调用者的输出副本和缓冲扩容还会占用额外内存。接收超时或输出分配失败时保留进度。UTF-8 无效时，可通过 `hx_os_frame_receive_until` 取出原始字节。消息之间断开返回 `EOF`，消息内部断开返回 `TRUNCATED_INPUT`；超长消息返回 `LIMIT` 并停止接收。

多个发送线程由库串行组织；底层支持全双工时，一个接收线程可以同时配合一个发送线程。关闭底层连接、abort 和销毁对象前，需要排除正在执行的调用。流适配器共用文件位置，需要外部串行化；封装不会让原本不支持超时的流获得硬截止时间。

保留半条消息时销毁返回 `BUSY`。完成接收／冲刷，或显式 abort 并放弃原连接后再销毁，不会静默丢弃。C++ `hx::Frame` 借用调用者长期持有的 `hx_os_frame *`，使用 `Result` 返回错误及发送接纳标志。

## 4. 运行示例

开启 `HX_OS_BUILD_EXAMPLES=ON` 后构建 `hx_os_framed_messages`。[framed_messages.c](../examples/framed_messages.c) 用内存流发送和接收 JSON。换成 `from_socket` 或 `from_tls` 即可在连接上使用相同协议。模块开关是 `HX_OS_ENABLE_FRAME`，各适配器还需要对应底层模块。

双向管道可用 `hx_os_frame_from_streams` 直接传入独立接收流和发送流，缺少某个方向时传 `NULL`，无需自己编写粘合回调。底层操作仍为同步，每个流需要能独立满足所选并发方式；C++ 对应 `hx::Frame::fromStreams`。
