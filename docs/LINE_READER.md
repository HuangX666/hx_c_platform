# Bounded UTF-8 Line Reading

[简体中文](LINE_READER.zh-CN.md)

**Start with a small text file.** For large files or line-oriented records, a reader avoids loading the entire file into a string.

Prepare demo.txt using [input setup](../examples/README.md#inputs) and build `hx_os_read_lines`. Inside build, run `./bin/hx_os_read_lines demo.txt` on Linux or `.\bin\Release\hx_os_read_lines.exe demo.txt` on Windows. It prints `0123456789`; a multiline input produces one output line per record.

[read_lines.c](../examples/read_lines.c) follows open → repeated next → process line → close after EOF → destroy string. Replace fwrite with record processing. EOF is normal completion; invalid encoding, oversized lines and I/O failures are separate results.

## 1. Read one line at a time

`hx_os_line_reader` reads UTF-8 text without loading the whole file. Enable `HX_OS_ENABLE_LINE_READER` and its `HX_OS_ENABLE_STREAM` dependency. `max_line_bytes=0` selects a 1 MiB line limit. There is no background thread or implicit network deadline.

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

For `first\r\nsecond\nlast` the returned lines are `first`, `second`, and `last`, then `HX_OS_EOF`. LF and CRLF are removed; a lone CR is preserved. Only the very first UTF-8 BOM is removed. Empty files, including a BOM-only file, return EOF without a line. A trailing LF does not invent an additional empty line. Empty physical lines still return a successful empty string.

The [read_lines example](../examples/read_lines.c) accepts a path and writes exact line lengths, including embedded NUL. Build `hx_os_read_lines` in an existing examples-enabled build directory, then run it with your UTF-8 file path. The simpler `puts` example above is intended for ordinary text without embedded NUL.

## 2. Bounds, ownership and stream position

The reader uses a fixed 4 KiB read-ahead buffer and a growing staging buffer capped at `max_line_bytes + 4` bytes. The extra bytes allow an initial BOM and CRLF. Growth can briefly hold old and new staging buffers, plus the reader object and allocation overhead. The caller's old/new output strings are separate from this budget. A successful output uses the string's own context.

`open` owns its internal file stream. `from_stream` borrows an existing file, memory or callback stream. Keep a borrowed stream alive and use it exclusively through the reader until close. Read-ahead advances the stream beyond the last returned line, and closing the reader does not rewind it. Do not mix raw reads/seeks or use output strings whose storage backs the borrowed stream.

The reader requires external serialization. Callback streams may block, and callbacks cannot reenter the reader. Short reads and progress returned together with EOF/error are handled: buffered complete lines may be delivered before the pending native error is reported. Partial line data survives an I/O error for retry. An `OK` callback with zero progress becomes `IO_ERROR`, preventing a busy loop.

## 3. Handle bad lines explicitly

Every failure, including EOF, leaves the caller's output string unchanged.

| Result | Next step |
|---|---|
| `OK` | Use the complete validated line. |
| `EOF` | End of input. A final non-LF line was already returned once. |
| `LIMIT` | Current physical line exceeds the limit. Call `skip_line` to discard it, or close. |
| Invalid/truncated UTF-8 | Current line is rejected. Call `skip_line` or close. |
| `NO_MEMORY` | Free other resources and retry; a staged complete line is retained. |
| Native I/O error | Partial bytes and diagnostics are retained; retry only if the backend can recover. |

Repeated `next` after a line-limit or UTF-8 error returns the same error until recovery. `skip_line` discards only the offending physical line. If its LF was already consumed, it resets the error without reading the next line. Otherwise it reads until LF/EOF without growing the line buffer. This explicit discard can block on the source; it has no hard deadline. If discard encounters an I/O error, retry `skip_line`; `next` returns `BUSY` while that discard remains unfinished.

Close releases reader storage. An owned stream's close error is reported, but still consumes the reader; a borrowed stream remains open. Reentrant close returns `BUSY` and keeps the reader alive.

## 4. C++11 loop

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

`next` returns `Result<bool>`: successful `true` means one line, successful `false` means EOF, and a failed result contains the error. The class is move-only, uses no exceptions and closes on destruction. Explicit `close()` lets you inspect any close error.
