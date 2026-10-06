# 二进制读写流与有界后台文件写出

[English](STREAMS.md) · [首页](../README.zh-CN.md)

## 1. 按任务选择入口

| 任务 | 入口 | 执行方式 |
| --- | --- | --- |
| 用同一套接口读写文件和内存 | `hx_os_stream_open()` / `hx::Stream::openRead()` | 同步调用 |
| 在内存中组织二进制消息 | `hx_os_stream_memory()` / `hx::Stream::memory()` | 按需增长，有容量上限 |
| 读取已有内存 | `hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` | 借用只读字节，不复制 |
| 按指定字节序读写数字 | `hx_os_stream_write_u32()` / `stream.write<uint32_t>()` | 写数字的编码，不写结构体布局 |
| 先缓存数据，由后台线程写文件 | `hx_os_file_writer_open()` / `hx::FileWriter::open()` | 一个工作线程、有界队列 |

二进制流与后台写出分别由 `HX_OS_ENABLE_STREAM`、`HX_OS_ENABLE_BUFFERED_FILE` 控制，默认开启。后台写出依赖 STREAM，可通过 `hx_os_file_writer_available()` 查询两者是否同时可用。只有显式创建写出对象才启动线程；这两个功能不依赖第三方库或线程池。

## 2. 写入并读回二进制消息

[binary_stream.c](../examples/binary_stream.c) 是完整的 C99 程序：先把设备编号 42、温度 23.5 写入内存流，再把位置移回开头，读出这两个数字。在项目根目录执行：

```sh
cmake -S . -B build
cmake --build build --config Release --target hx_os_binary_stream
# Linux/macOS：
./build/bin/hx_os_binary_stream
# Windows PowerShell：
.\build\bin\Release\hx_os_binary_stream.exe
```

预期输出为 `device=42 temperature=23.5`。第一项是四字节的大端整数，第二项是八字节的 IEEE binary64 浮点数。发送方与接收方需要约定字段类型和字节序。直接写整个 C 结构体会把填充字节、当前平台的布局一起写进去，不适合作为跨平台消息格式。

C++11 包含 `<hx/Stream.hpp>` 即可。下面这个函数生成只含设备编号的内存消息，底层复用 C 实现，不使用异常：

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

内存流的 `view()` 返回借用字节，应在下一次修改或销毁之前使用或复制。`fromMemory()` 同样借用输入，调用者需要保证输入一直有效且不被修改。C++ 的 `read<T>()`、`write<T>()` 支持不超过 64 位的整数、`float`、`double`；C 接口还直接提供 24 位整数及 1～8 字节任意宽度整数。浮点接口会拒绝不符合要求的宿主表示。

## 3. 读不满、写不完和失败分别表示什么

| 返回结果 | 含义与处理方式 |
| --- | --- |
| `read_some` / `write_some` 返回 `OK` | 本次成功，但实际数量可能少于请求数量 |
| `read_exact` 返回 `EOF` | 本次一个字节都没有读到，输入已经结束 |
| `read_exact` 返回 `TRUNCATED_INPUT` | 读到一部分后输入结束，数据不完整 |
| 完整读写接口返回其他错误 | 查看 `completed`；前面的一部分可能已经完成 |
| 数字读取失败 | 输出数字不变，但流的位置可能已经前移 |
| 数字写入失败 | 可能已经写出这个数字的一部分，不能直接重复写整个数字 |

需要自行恢复传输时，使用原始字节的 `read_exact` / `write_all` 并保留实际完成数量。零字节操作直接成功，不调用后端。自定义回调必须如实报告进度；非空操作返回 `OK` 却没有任何进度时，完整读写会返回 I/O 错误，避免死循环。原生错误详情继续保留。

内存流默认上限为 16 MiB。扩容过程中可能同时持有旧、新缓冲，最多为两倍数据上限加元数据。输入输出缓冲不能与流自身持有的内存重叠。流由调用者串行访问；文件和自定义回调可能阻塞。关闭会消费句柄，包括原生关闭失败。`flush(0)` 只处理用户缓冲，`flush(1)` 请求文件持久化；内存流不支持持久化请求。

## 4. 在后台保存设备采样

[buffered_file.c](../examples/buffered_file.c) 是完整程序，将三个设备读数按 JSON 行写入 `samples.jsonl`：业务提交每一行，最后请求持久化并关闭。构建目标为 `hx_os_buffered_file`，运行位置与上面的示例相同。文件在程序当前工作目录创建，已有同名文件会被截断。

日常使用主要是这一句：

```c
size_t accepted = 0;
hx_os_status status = hx_os_file_writer_write_until(
	writer, json_line, json_bytes, &accepted,
	hx_os_deadline_after_ms(2000), &error);
```

这是函数体片段：`writer` 是已经打开的写出对象，`json_line`、`json_bytes` 是待写数据与字节数，`error` 是 `hx_os_error`。返回成功表示队列已经保存整段数据的副本，还不表示磁盘写入完成。超时或失败时只有前 `accepted` 个字节被接受，重试应从剩余部分开始。多个线程可以共用一个写出对象；一次调用内部的数据不会与另一次调用穿插，多次调用之间不构成事务。

默认队列为 1 MiB，每次原生写入最多 64 KiB。需要追加模式或调整容量时使用 `open_ex()`。队列在线程启动前分配，写入和冲刷不分配内存；正在写出的数据仍占用队列容量。线程栈、系统同步对象及管理结构另计。磁盘阻塞时队列可能持续满，等待中的调用按自己的截止时间返回。

## 5. 冲刷、错误重试和关闭

`flush_until()` 记录调用时已经接受的数据，等待它们写完，再冲刷后端。之后新增的数据不会延长这个目标。多个冲刷调用串行执行，内部只保留一个冲刷请求。冲刷超时后，该请求仍可能稍后完成。`persistent=1` 请求 `fsync` / `FlushFileBuffers`，不承诺硬件一定完成落盘。

写入或冲刷错误会被保留，并停止继续接受数据。`pending()` 能同时查询剩余字节和错误。调用者处理故障原因后，用 `retry()` 继续尚未完成的部分，已经成功写出的前缀不会重复写。原生关闭失败时，后端句柄已经消费，不能再次关闭它。

`close_until()` 停止接受、排空队列、冲刷用户缓冲并关闭后端。超时或者写入／冲刷失败时，保留原来的指针，之后可以再次关闭。`abort_until()` 则是明确放弃尚未写出的队列，但同样要等当前原生操作返回，不能终止卡住的系统调用或第三方回调。两个接口使用单调截止时间，支持 NOW 和 INFINITE。最终关闭或放弃时，必须排除所有其他公共调用，包括正在等待进入的调用。多个生产者的程序可先调用 `stop()`：停止接受并唤醒生产者，等这些生产者结束调用，再最终关闭。`stop()` 会保留对象，可与写入、冲刷并发。工作线程回调等待自己的对象时返回 DEADLOCK。

C++ 便利层与线程池采用相同的持有方式：

```cpp
hx_os_file_writer *owner = NULL; // 关闭超时后继续保留这个 owner。
auto opened = hx::FileWriter::open(owner, "samples.jsonl");
```

检查 `opened` 后，通过 `opened.value()` 调用 `write()`、`flushUntil()` 和 `closeUntil()`。失败的写入结果仍带有已接受数量，用 `hasValue()`、`value()` 读取。便利对象析构不会等待或处置活跃线程；宿主需要持有 `owner`，直到关闭或放弃完成。C 控制台示例在报错后显式选择无限等待，以完成安全清理；GUI 或服务可以保留对象，按自己的退出策略安排后续重试。

## 6. 验证与性能边界

契约测试覆盖已知二进制字节、短读短写、数字输出失败不变、分配失败、环形回绕、多生产者、原生错误、只重试剩余部分、关闭超时、显式放弃及 context 资源回收。后台写出测试还在 WSL 的 ASan/UBSan、TSan 配置下运行，具体配置见[验证记录](VALIDATION.zh-CN.md)。

后台线程按块写出，适合让生产者与同步输出分开执行；它同时增加互斥锁、条件变量和每个对象一个线程的成本。调用数据大于当前可用空间时，需要分段复制。这里没有宣称测得吞吐量或延迟提升，仍需按实际磁盘、设备与生产负载测量。文件和内存流直接调用对应实现，只有自定义后端通过回调间接调用。
