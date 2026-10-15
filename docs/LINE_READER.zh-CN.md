# 有界 UTF-8 逐行读取

[English](LINE_READER.md)

**先读取一个小文本文件。** 文件较大，或者业务按行处理记录时，不必先把整份文件读进字符串。

按[输入准备](../examples/README.zh-CN.md#inputs)创建 demo.txt，再构建 `hx_os_read_lines`。在 build 中，Linux 运行 `./bin/hx_os_read_lines demo.txt`；Windows 运行 `.\bin\Release\hx_os_read_lines.exe demo.txt`。输出 `0123456789`。把文件换成多行，就会逐行输出。

[read_lines.c](../examples/read_lines.c) 的顺序是 open → 循环 next → 处理一行 → EOF 后 close → destroy 字符串。把 fwrite 换成解析一条记录的逻辑；EOF 是正常结束，坏编码、过长行和 I/O 错误不是 EOF。

## 1. 一次读取一行

`hx_os_line_reader` 不需要把整个文件读入内存，就能逐行读取 UTF-8 文本。开启 `HX_OS_ENABLE_LINE_READER` 和它依赖的 `HX_OS_ENABLE_STREAM`。`max_line_bytes=0` 使用默认单行 1 MiB 上限，没有后台线程或隐藏的网络截止时间。

```c
#include <hx_line_reader.h>
#include <stdio.h>

int main(void)
{
	hx_os_line_reader *reader = NULL;
	hx_string line = HX_STRING_INIT;
	hx_os_status status =
		hx_os_line_reader_open(NULL, "input.txt", 0, &reader, NULL);

	while (status == HX_OS_OK) {
		status = hx_os_line_reader_next(reader, &line, NULL);
		if (status == HX_OS_OK)
			puts(hx_string_c_str(&line));
	}

	(void)hx_os_line_reader_close(&reader, NULL);
	hx_string_destroy(&line);

	return status == HX_OS_EOF ? 0 : 1;
}
```

输入 `first\r\nsecond\nlast` 时，依次返回 `first`、`second`、`last`，再返回 `HX_OS_EOF`。LF 和 CRLF 被移除，单独的 CR 保留；只移除文件开头的一个 UTF-8 BOM。空文件和只有 BOM 的文件直接 EOF，不产生一行。末尾 LF 不额外产生空行，但文件里实际存在的空行会成功返回空字符串。

[read_lines 完整示例](../examples/read_lines.c) 接收路径，并按明确长度输出行，支持嵌入 NUL。复用现有开启 examples 的构建目录，编译 `hx_os_read_lines`，传入 UTF-8 文件路径运行。上面更短的 `puts` 示例适合不含嵌入 NUL 的普通文本。

## 2. 内存边界、所有权与流位置

读取器包含固定 4 KiB 预读缓冲区，行暂存按需增长，最多为 `max_line_bytes + 4` 字节，多出的字节用于开头 BOM 和 CRLF。扩容期间可能同时持有新旧暂存区，另有读取器对象和分配开销。调用者自己的新旧输出字符串不计入这个暂存预算。成功输出使用字符串自己的 context。

`open` 持有内部打开的文件流。`from_stream` 借用已有的文件、内存或回调流；关闭读取器前，流必须保持有效，并由读取器独占使用。预读会让底层流位置超过最近返回的那一行，关闭读取器不会自动回退。不能混用原始读操作、seek，也不要让输出字符串同时成为被借用流的底层存储。

读取器由调用者负责同步。回调流可能阻塞，回调不能重入读取器。支持短读以及“部分进度同时伴随 EOF／错误”：已缓冲的完整行可以先返回，之后再报告待处理的原生错误。原生 I/O 出错时保留部分行，允许重试；回调返回 `OK` 但没有读取任何字节时转为 `IO_ERROR`，避免忙循环。

## 3. 明确处理坏行

包括 EOF 在内的所有失败都保持调用者的输出字符串不变。

| 结果 | 后续处理 |
|---|---|
| `OK` | 使用完整、已验证的行。 |
| `EOF` | 已到输入末尾，没有 LF 的最后一行此前已经返回一次。 |
| `LIMIT` | 当前物理行超过上限，调用 `skip_line` 丢弃这一行，或者关闭。 |
| UTF-8 非法或截断 | 当前行被拒绝，调用 `skip_line` 或关闭。 |
| `NO_MEMORY` | 释放其他资源后重试；已经完整暂存的行会保留。 |
| 原生 I/O 错误 | 保留已读部分和错误详情，只在底层支持恢复时重试。 |

行长度或编码错误后，再次 `next` 会重复返回该错误，直到显式恢复。`skip_line` 只丢弃出错的这一物理行：如果 LF 已经被消费，就直接清除错误状态，不会再跳过下一行；否则继续读到 LF／EOF，不再增长行缓冲。主动丢弃可能阻塞等待源数据，没有硬截止时间。如果丢弃时发生 I/O 错误，请重试 `skip_line`；丢弃未完成时，`next` 返回 `BUSY`。

关闭释放读取器资源。内部持有流的关闭错误会报告给调用者，但读取器仍被消费；借用的流继续保持打开。回调重入关闭返回 `BUSY`，对象继续保留。

## 4. C++11 循环

```cpp
#include <hx/LineReader.hpp>
#include <cstdio>

int main()
{
	auto reader = hx::LineReader::open("input.txt");
	if (!reader)
		return 1;

	hx::String line;
	for (;;) {
		auto next = reader.value().next(line);
		if (!next)
			return 1;
		if (!next.value())
			break;

		std::puts(line.c_str());
	}

	return reader.value().close() ? 0 : 1;
}
```

`next` 返回 `Result<bool>`：成功且为 `true` 表示得到一行，成功且为 `false` 表示 EOF，失败结果包含错误。便利类只允许移动，不使用异常，析构自动关闭；显式 `close()` 可以检查关闭错误。
