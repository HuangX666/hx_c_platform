# 有界 UTF-8 逐行读取

[English](LINE_READER.md)

当需要处理体积较大的日志文件、数据导入清单或按行组织的文本记录时，一次性把整份文件读进内存既浪费空间，又容易被异常超长行撑爆内存。`hx_os_line_reader` 提供流式、有界的 UTF-8 逐行读取能力，按单行上限增量拉取并校验 UTF-8 文本，无需将整个文件加载到单个字符串中。

你可以先通过内置示例体验完整流程：按[输入准备](../examples/README.zh-CN.md#inputs)在构建目录准备好 `demo.txt`，并编译 `hx_os_read_lines` 目标。在构建目录中运行 `./bin/hx_os_read_lines demo.txt`（Linux/macOS）或 `.\bin\Release\hx_os_read_lines.exe demo.txt`（Windows），控制台会输出 `0123456789`；若将输入文件换成多行文本，则会逐行输出。

完整源码见 [read_lines.c](../examples/read_lines.c)：程序先调用 `hx_os_line_reader_open()` 打开文件，在循环中调用 `hx_os_line_reader_next()` 逐行读取并处理，遇到 `HX_OS_EOF` 正常结束后关闭读取器并销毁行字符串。请注意：`HX_OS_EOF` 代表正常读完文件末尾，而非法 UTF-8 编码、超长物理行和底层 I/O 错误会返回各自独立的状态码。

## 1. 一次读取一行

要使用逐行读取器，请在构建时开启 `HX_OS_ENABLE_LINE_READER` 及其依赖的 `HX_OS_ENABLE_STREAM`（均默认开启）。调用时若将 `max_line_bytes` 设为 `0`，则默认采用 **1 MiB** 的单行字节上限。读取器在当前线程内同步工作，不启动后台线程，也不设隐式的网络超时。

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

**换行符、BOM 与空行处理规则：**
- 当输入内容为 `first\r\nsecond\nlast` 时，连续调用 `hx_os_line_reader_next()` 会依次得到 `"first"`、`"second"`、`"last"`，随后在下一次调用时返回 `HX_OS_EOF`。
- 行尾的 `\n`（LF）和 `\r\n`（CRLF）会被自动剥离；但行内单独出现的 `\r`（CR）会原样保留。
- 仅当文件最开头存在一个 UTF-8 BOM（`EF BB BF`）时会自动剥离；空文件或只包含一个 UTF-8 BOM 的文件在首次读取时直接返回 `HX_OS_EOF`，不会产生空行。
- 文件末尾的换行符不会额外多造出一个空行；但文件中间实际存在的空行（例如连续两个 `\n`）会正常返回 `HX_OS_OK` 及长度为 `0` 的空字符串。

上面的短示例使用 `puts()` 打印普通文本；若行内可能包含合法的内嵌 `\0`（NUL）字节，请参考 [read_lines 完整示例](../examples/read_lines.c)，按 `hx_string_size(&line)` 显式长度读取。

## 2. 内存边界、所有权与流位置

- **内部缓冲与内存上限**：读取器内部维护一个固定的 **4 KiB** 预读缓冲区（Read-ahead Buffer），以及一个按需增长的行暂存区（上限为 `max_line_bytes + 4` 字节，多出的 4 字节用于容纳文件开头的 UTF-8 BOM 和行尾 CRLF）。暂存区扩容瞬间可能短暂同时持有新旧两块暂存缓冲，成功产出的一行则使用调用方传入的 `hx_string` 自身绑定的内存上下文分配。
- **打开文件 vs. 借用现有流**：
  - `hx_os_line_reader_open()` 由读取器内部独占打开并管理底层文件流，关闭读取器时一并关闭文件。
  - `hx_os_line_reader_from_stream()` 则借用调用方已创建的文件流、内存流或自定义回调流。在关闭读取器之前，被借用的流必须保持有效且由读取器独占访问。由于存在 4 KiB 预读，底层流的实际游标位置会超前于最近返回的那一行，且关闭读取器时**不会**自动回退（seek）流位置。因此，请勿在读取期间混用原始流读取或 `seek`，也不要将输出 `hx_string` 本身用作被借用内存流的底层存储。
- **并发与短读处理**：同一个读取器实例不支持跨线程并发调用，也不允许在自定义流回调中重入同一个读取器。实现已妥善处理底层短读（Short Read）以及“伴随部分数据的 EOF／I/O 错误”：已完整拼好并放入缓冲区的行会优先交付给调用方，待下一次调用时再报告挂起的底层错误。若底层流发生 I/O 错误，已读入暂存区的半行数据会保留以便重试；若自定义回调流返回 `HX_OS_OK` 却未产出任何字节，读取器会将其转为 `HX_OS_IO_ERROR` 以防止死循环。

## 3. 明确处理坏行

任何非 `HX_OS_OK` 的返回（包括 `HX_OS_EOF`）都保证调用方传入的输出字符串保持原样不变：

| 返回状态码 | 含义与后续处理建议 |
|---|---|
| `HX_OS_OK` | 成功读取并校验完一整行 UTF-8 文本，可直接使用输出字符串。 |
| `HX_OS_EOF` | 已到达输入末尾（若最后一行没有换行符，它已在前一次调用中成功返回过）。 |
| `HX_OS_LIMIT` | 当前物理行字节数超过 `max_line_bytes` 上限；可调用 `hx_os_line_reader_skip_line()` 丢弃该超长行后继续，或直接关闭读取器。 |
| `HX_OS_INVALID` | 当前物理行包含非法或截断的 UTF-8 字节序列；可调用 `hx_os_line_reader_skip_line()` 跳过该坏行，或直接关闭读取器。 |
| `HX_OS_NO_MEMORY` | 内存分配失败；已完整暂存的行不会丢失，释放其他内存后可重试。 |
| `HX_OS_IO_ERROR`（或原生 I/O 错误） | 底层读取出错，已读入的半行字节和错误详情均被保留；仅在底层流支持恢复时重试。 |

> [!TIP]
> **如何使用 `hx_os_line_reader_skip_line()` 从超长行或坏编码行恢复：**
> 当某一行触发 `HX_OS_LIMIT` 或 UTF-8 编码错误（`HX_OS_INVALID`）后，如果直接再次调用 `hx_os_line_reader_next()`，它会继续返回相同的错误，直到你显式恢复。调用 `hx_os_line_reader_skip_line()` 会且仅会丢弃当前出错的这一条物理行：
> - 若该坏行的换行符 `\n` 此前已被读入预读区，它会立即清除错误状态，不会误删下一行；
> - 若尚未读到该行的行尾，它会继续从流中读取并原地丢弃字节（不再增长行暂存区），直到遇到 `\n` 或 `EOF`。
> - 注意：若丢弃途中再次遭遇底层 I/O 错误，请重试 `hx_os_line_reader_skip_line()`；在坏行尚未彻底丢弃前调用 `next()` 会返回 `HX_OS_BUSY`。

调用 `hx_os_line_reader_close()` 会释放读取器自身占用的内存并将句柄置空。对于 `open` 打开的内部流，若底层关闭报错，错误状态会返回给调用方，但读取器内存仍会被释放；对于 `from_stream` 借用的外部流，读取器关闭后外部流仍保持打开状态。

## 4. C++11 循环

在 C++11 中，包含 `<hx/LineReader.hpp>` 即可使用移动语义封装的 `hx::LineReader`：

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

- `reader.value().next(line)` 返回 `hx::Result<bool>`：调用成功且值为 `true` 表示读到新的一行，成功且值为 `false` 表示已到达 `EOF`，失败结果则携带具体错误。
- `hx::LineReader` 仅支持移动（Move-only）、不使用 C++ 异常，析构时会自动关闭；显式调用 `close()` 则允许你主动检查关闭阶段的返回值。
