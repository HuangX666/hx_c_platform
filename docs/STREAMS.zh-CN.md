# 二进制读写流与有界后台文件写出

[English](STREAMS.md) · [首页](../README.zh-CN.md)

当文件格式或网络协议明确规定了多字节数值的字节序（Endianness），或者你希望用同一套接口读写磁盘文件与内存缓冲区时，可以使用二进制流接口（`hx_os_stream` / `hx::Stream`）。此外，如果业务线程需要高频记录数据（如设备采样或 JSON Lines 日志）而不希望被同步磁盘写入阻塞，可以使用配套的有界后台文件写出器（`hx_os_file_writer` / `hx::FileWriter`）。

你可以直接编译并运行 [binary_stream.c](../examples/binary_stream.c) 来体验完整的二进制流读写流程：它创建一个内存流，按大端序（`HX_OS_BIG_ENDIAN`）写入 32 位设备编号 `42` 和 64 位浮点温度 `23.5`，随后将读写位置重置回开头（`seek`），按相同字节序读回并打印。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 `HX_OS_BUILD_EXAMPLES=ON` 并构建 `hx_os_binary_stream`（无需任何命令行参数或外部输入文件）：

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream

# Linux / macOS：
./build/bin/hx_os_binary_stream

# Windows PowerShell：
.\build\bin\Release\hx_os_binary_stream.exe
```

运行后会输出 `device=42 temperature=23.5`。把内存流替换为文件流即可直接落盘；若想了解后台异步写文件，可参阅第 4 节及完整示例 [buffered_file.c](../examples/buffered_file.c)（它向 `samples.jsonl` 写入三条采样记录并在安全关闭后打印完成信息）。你也可以参考[最小工程配置](USAGE.zh-CN.md#c-project)将这些示例直接作为工程模板使用。

## 1. 按任务选择入口

| 任务 | C99 / C++11 入口 | 执行方式与特点 |
| --- | --- | --- |
| 用统一接口读写磁盘文件 | `hx_os_stream_open()` / `hx::Stream::openRead()` | 同步调用，直接操作底层文件句柄 |
| 在内存中组装或解析二进制消息 | `hx_os_stream_memory()` / `hx::Stream::memory()` | 同步调用，按需自动扩容（受容量上限保护） |
| 从已有内存缓冲区读取二进制数据 | `hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` | 零拷贝借用只读内存，不分配新缓冲区 |
| 按指定字节序读写整数与浮点数 | `hx_os_stream_write_u32()` / `stream.write<uint32_t>()` | 按跨平台数值编码读写，避免直接转储 C 结构体内存布局 |
| 先写入有界内存队列，由后台线程落盘 | `hx_os_file_writer_open()` / `hx::FileWriter::open()` | 每个写出器配一个专用后台工作线程与有界环形队列 |

**CMake 编译开关**：二进制流与后台文件写出器分别由 `HX_OS_ENABLE_STREAM` 和 `HX_OS_ENABLE_BUFFERED_FILE` 控制，默认均开启（`ON`）。后台写出器依赖 `HX_OS_ENABLE_STREAM`，运行时可通过 `hx_os_file_writer_available()` 查询两者是否同时可用。这两项功能均不依赖第三方库或全局线程池，且只有在显式创建 `hx_os_file_writer` 对象时才会启动后台线程。

## 2. 写入并读回二进制消息

直接用 `fwrite(&my_struct, sizeof(my_struct), 1, fp)` 写入整个 C 结构体，会把编译器插入的对齐填充字节（Padding）和当前 CPU 的原生字节序原样写进文件，导致跨平台或跨编译器读取失败。使用二进制流接口时，发送方与接收方只需约定字段类型和字节序（例如四字节大端整数加八字节 IEEE 754 `binary64` 浮点数），即可安全地序列化数据。

在 C++11 中，只需包含 `<hx/Stream.hpp>`。下面的函数演示如何在内存中构造一条仅含设备编号的二进制消息（底层复用 C99 实现，全程通过 `hx::Result<T>` 传递状态，不使用异常）：

```cpp
hx::Result<hx::Stream> make_message(uint32_t device) noexcept
{
	auto result = hx::Stream::memory(64);
	if (!result)
		return hx::Failure(result.error());

	auto written = result.value().write<uint32_t>(device, HX_OS_BIG_ENDIAN);
	if (!written)
		return hx::Failure(written.error());

	return result;
}
```

> [!IMPORTANT]
> **内存流生命周期与数值宽度支持**
> - **借用视图有效性**：对内存流调用 `view()`（或 `hx_os_stream_memory_view`）返回的是指向流内部缓冲区的借用切片，必须在下一次写入、扩容或销毁流之前读取或复制走。
> - **只读内存包装**：`hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` 仅借用调用方传入的只读缓冲区而不做拷贝，调用方必须保证该缓冲区在流存活期间一直有效且不被修改。
> - **支持的数据类型**：C++ 模板 `read<T>()` 与 `write<T>()` 支持 8/16/32/64 位有符号与无符号整数、`float` 和 `double`；C 接口除此之外还额外提供 24 位整数（`u24` / `i24`）以及 1～8 字节任意宽度整数接口。浮点读写接口会校验宿主平台的浮点表示，拒绝非标准 IEEE 754 格式。

## 3. 读不满、写不完和失败分别表示什么

处理流式 I/O 时，区分“单次部分传输（Short I/O）”与“定长完整传输（Exact I/O）”至关重要：

| 场景与返回状态 | 具体含义与调用方处理建议 |
| --- | --- |
| `read_some` / `write_some` 返回 `HX_OS_OK` | 本次底层调用成功，但**实际传输的字节数可能少于请求字节数**（需检查输出的完成字节数） |
| `read_exact` 返回 `HX_OS_EOF` | 本次请求**一个字节都未读到**，流已到达末尾（干净的 EOF） |
| `read_exact` 返回 `HX_OS_TRUNCATED_INPUT` | 读到了部分前缀字节后流提前结束，不够填满本次请求的完整长度 |
| `read_exact` / `write_all` 返回其他错误码 | 传输中途出错；查看 `completed` 输出参数可知报错前已成功传输了多少字节 |
| 数值读取（如 `read_u32`）失败 | 输出变量保持原值不变；但如果底层读到了半截字节后出错，流的位置可能已经前移 |
| 数值写入（如 `write_u32`）失败 | 底层可能已经写出了该数字编码的前几个字节，**不能**直接在当前位置盲目重写整个数字 |

如果需要实现断点恢复，请使用原始字节接口 `hx_os_stream_read_exact` / `hx_os_stream_write_all` 并根据 `completed` 计数值处理剩余字节。长度为 `0` 的读写请求会直接返回 `HX_OS_OK`，不会触发底层回调。自定义流后端回调必须如实填报完成字节数——对于非空请求，若回调返回 `HX_OS_OK` 却报告 `0` 字节进展，定长读写接口会立即中止并返回 I/O 错误，防止陷入死循环。

> [!NOTE]
> **内存上限、线程安全与持久化边界**
> - **内存流容量上限**：内存流默认最大容量为 `16 MiB`。扩容瞬间可能同时存在新旧两块缓冲区，峰值内存最多为有效载荷上限的两倍加上结构元数据。传入流读写接口的外部缓冲区不得与内存流自身持有的缓冲区发生地址重叠。
> - **并发与关闭**：同一个 `hx_os_stream` 实例需要由调用方串行访问。关闭流（`close`）总会消费并释放流句柄（即便底层原生 `close` 返回了错误）。
> - **冲刷语义**：`flush(0)` 仅冲刷用户态缓冲；`flush(1)` 会进一步请求操作系统对文件流执行持久化刷盘（内存流不支持 `flush(1)` 持久化请求）。

## 4. 在后台保存设备采样

当采集线程不希望因为磁盘偶发抖动而阻塞时，可以使用 `hx_os_file_writer`。完整示例 [buffered_file.c](../examples/buffered_file.c) 将三条设备读数按 JSON Lines 格式写入当前工作目录下的 `samples.jsonl`（若同名文件已存在则将其截断重写），最后请求持久化并安全关闭。构建目标为 `hx_os_buffered_file`。

在业务代码中，日常提交数据只需调用 `hx_os_file_writer_write_until`：

```c
size_t accepted = 0;
hx_os_status status = hx_os_file_writer_write_until(
	writer, json_line, json_bytes, &accepted,
	hx_os_deadline_after_ms(2000), &error);
```

在这段代码中，`writer` 是已打开的后台写出对象，`json_line` 与 `json_bytes` 是待写入缓冲区及其字节长度。理解其返回语义对编写可靠服务非常关键：

- **`HX_OS_OK` 表示已入队，不代表已落盘**：返回 `HX_OS_OK` 说明后台队列已完整复制并接管了这段数据，调用方可以立即复用 `json_line` 缓冲区；此时数据可能仍在内存队列中等待工作线程写出。
- **部分入队与超时重试**：当磁盘阻塞导致有界队列写满时，调用会等待直到指定的单调截止时间（如上例的 2000 毫秒）。若返回 `HX_OS_TIMEOUT` 或其他错误，只有前 `accepted` 个字节被队列接收；重试时应从 `json_line + accepted` 处继续提交剩余的 `json_bytes - accepted` 字节。
- **多生产者线程安全**：多个业务线程可以并发调用同一个 `writer`。写出器内部通过互斥锁保证单次调用的数据块不会与其他线程的调用交错（但多次独立调用之间不构成原子事务）。
- **固定内存预算**：默认队列容量为 `1 MiB`，工作线程单次向操作系统发起的最大写入块为 `64 KiB`（可通过 `hx_os_file_writer_open_ex` 调整容量或开启追加模式 `append`）。环形队列在工作线程启动前一次性分配完毕，后续 `write` 和 `flush` 过程**零动态内存分配**；正在由工作线程写入磁盘的在途字节仍占用队列容量，直到系统调用返回后才释放空间。

## 5. 冲刷、错误重试和关闭

后台写出器提供了明确的冲刷（Flush）、故障恢复（Retry）与两阶段关闭机制：

- **冲刷（`hx_os_file_writer_flush_until`）**：记录调用进入时刻队列中已接收的数据水位，等待后台线程将这部分数据全部写出后冲刷底层流（之后新提交的写入不会延长本次冲刷的等待目标）。多个并发冲刷请求会串行化处理。若冲刷等待返回 `HX_OS_TIMEOUT`，该冲刷请求仍可能在后台继续完成。传入 `persistent = 1` 会请求底层调用 `fsync` / `FlushFileBuffers`（实际物理介质落盘仍取决于硬件控制器）。
- **错误锁定与恢复（`pending` / `retry`）**：一旦后台线程遇到写入或冲刷失败，写出器会锁存该错误并停止接收新数据。调用方可通过 `hx_os_file_writer_pending()` 查询当前剩余未写出的字节数及错误详情；排查并修复外部故障（如释放磁盘空间）后，调用 `hx_os_file_writer_retry()` 即可让工作线程从尚未写出的后缀字节继续写出，此前已成功写入的前缀绝不会重复写入。（注：若底层文件在 `close` 阶段发生原生关闭错误，底层句柄已被消费，无法重试文件关闭。）
- **关闭与放弃（`close_until` / `abort_until` / `stop`）**：
  - `hx_os_file_writer_close_until()`：停止接收新数据，排空队列中所有待写数据，冲刷用户态缓冲并关闭底层文件。若在截止时间内未能排空或遇到写入错误，函数返回错误并**保留原 `writer` 指针有效**，供宿主稍后重试关闭。
  - `hx_os_file_writer_abort_until()`：停止接收并**丢弃队列中尚未写出的剩余数据**，但仍需等待当前正在执行的原生系统调用或回调返回后才能安全回收线程（库不会强行杀死卡在内核态的系统调用）。
  - **多生产者退出顺序**：执行最终的 `close_until` 或 `abort_until` 时，调用方必须确保没有其他线程正在调用或排队等待调用该 `writer` 的公共接口。如果有多个生产者线程，请先调用 `hx_os_file_writer_stop()`（它可以与写入/冲刷安全并发，用于停止接收并唤醒所有阻塞中的生产者），待所有生产者线程退出写入逻辑后，再调用 `close_until()`。工作线程的自定义回调如果试图等待其所属的 `writer`，会立即收到 `HX_OS_DEADLOCK`。
  - **线程句柄清理重试**：极少数情况下，若后台工作线程已正常退出、文件也已关闭，但操作系统释放线程句柄失败，`close_until` / `abort_until` 会报告清理错误并保留 `writer`。此时用同一个 `owner` 重试关闭即可——底层文件不会被二次关闭，待线程资源清理成功后即会返回之前保存的文件关闭结果并将 `owner` 置空。

在 C++11 中，`hx::FileWriter` 采用与线程池一致的显式 `owner` 托管模式，确保关闭超时后不会发生悬垂析构：

```cpp
hx_os_file_writer *owner = NULL; // 若关闭超时，继续保留此 owner 以便稍后重试
auto opened = hx::FileWriter::open(owner, "samples.jsonl");
```

检查 `opened` 成功后，即可通过 `opened.value()` 调用 `write()`、`flushUntil()` 和 `closeUntil()`。即便 `write()` 因超时失败，返回结果中依然包含已入队的字节数（可先检查 `hasValue()` 再通过 `value()` 读取）。请注意：`hx::FileWriter` 包装器析构时**不会**隐式阻塞等待或强行销毁仍在运行的后台线程；宿主程序必须一直持有 `owner`，直到 `closeUntil()` 或 `abortUntil()` 成功返回。

## 6. 示例与性能边界

各目标平台的编译工具链、部署要求与系统限制详见[平台支持](PLATFORMS.zh-CN.md)。

在性能特征方面，文件流与内存流直接调用底层系统接口或内存拷贝，仅在接入用户自定义流后端时才存在函数指针间接调用。后台写出器（`hx_os_file_writer`）通过将零散小数据聚合为最大 `64 KiB` 的块来减少系统调用次数，帮助业务线程摆脱同步磁盘 I/O 停顿；但与此同时，它也引入了互斥锁、条件变量唤醒、环形缓冲内存拷贝（当单次写入大于尾部连续空间时需分两段拷贝）以及每个实例一个专用内核线程的开销。实际吞吐量与尾延迟收益取决于具体磁盘介质与负载模型，建议在目标设备上实测评估。
