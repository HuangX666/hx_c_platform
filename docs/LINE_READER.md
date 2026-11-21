# Bounded UTF-8 Line Reading

[简体中文](LINE_READER.zh-CN.md)

When processing large log files, line-delimited data exports, or streaming text records, reading the entire file into memory at once wastes RAM and leaves your application vulnerable to oversized lines. `hx_os_line_reader` reads and validates UTF-8 text one line at a time under an explicit per-line byte ceiling, without loading the whole file into a single string.

To try it on a real file, prepare `demo.txt` in your build directory using the [input setup](../examples/README.md#inputs) guide and build `hx_os_read_lines`. From the build directory, run `./bin/hx_os_read_lines demo.txt` on Linux/macOS or `.\bin\Release\hx_os_read_lines.exe demo.txt` on Windows. It prints `0123456789` (or one line per record for a multiline input file).

Open [read_lines.c](../examples/read_lines.c) to see the lifecycle: open the reader with `hx_os_line_reader_open()`, call `hx_os_line_reader_next()` in a loop to process each line, close the reader once `HX_OS_EOF` is reached, and destroy the reusable line buffer. Note that `HX_OS_EOF` signals normal end-of-input, whereas malformed UTF-8, oversized lines, and I/O errors return distinct status codes.

## 1. Read one line at a time

Enable `HX_OS_ENABLE_LINE_READER` and its `HX_OS_ENABLE_STREAM` dependency in CMake (both default to `ON`). Passing `max_line_bytes = 0` selects the default **1 MiB** per-line byte limit. The reader operates synchronously on the calling thread with no background worker threads or implicit network deadlines.

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

**Line Endings, BOM, and Empty Line Rules:**
- Given the input `first\r\nsecond\nlast`, successive calls to `hx_os_line_reader_next()` yield `"first"`, `"second"`, and `"last"`, followed by `HX_OS_EOF`.
- Trailing `\n` (LF) and `\r\n` (CRLF) line endings are stripped, while standalone `\r` (CR) bytes inside a line are preserved.
- A single leading UTF-8 BOM (`EF BB BF`) at the start of the stream is stripped automatically. Empty files and BOM-only files return `HX_OS_EOF` on the first call without emitting an empty line.
- A trailing LF at the end of the file does not synthesize an extra empty line, whereas actual blank physical lines inside the file return `HX_OS_OK` with an empty string (`size == 0`).

The snippet above uses `puts()` for ordinary text without embedded `NUL` bytes. When lines may contain embedded `NUL` bytes, inspect `hx_string_size(&line)` as shown in the [read_lines example](../examples/read_lines.c).

## 2. Bounds, ownership and stream position

- **Internal buffers & memory ceiling**: Each reader maintains a fixed **4 KiB** read-ahead buffer and a dynamically growing line staging buffer capped at `max_line_bytes + 4` bytes (the extra 4 bytes accommodate an initial UTF-8 BOM and trailing CRLF). Buffer growth may briefly hold both old and new staging buffers, while committed output lines allocate through the caller's `hx_string` context.
- **Owned file streams vs. borrowed streams**:
  - `hx_os_line_reader_open()` opens and owns the underlying file stream, closing it when the reader is closed.
  - `hx_os_line_reader_from_stream()` borrows an existing file, memory, or callback stream. Keep the borrowed stream alive and access it exclusively through the reader until `hx_os_line_reader_close()`. Because the 4 KiB read-ahead buffer advances the underlying stream cursor past the most recently returned line—and closing the reader does **not** rewind the stream—do not interleave raw stream reads/seeks or back a borrowed memory stream with the same `hx_string` used as the output buffer.
- **Concurrency & short reads**: Access to a single reader must be serialized externally, and stream callbacks must not re-enter the reader. Short reads and partial progress returned alongside `EOF` or an I/O error are handled cleanly: any complete buffered lines are delivered first before reporting the pending native error. Partial line bytes survive a transient I/O error so you can retry; a custom stream callback that returns `HX_OS_OK` with zero bytes read is converted into `HX_OS_IO_ERROR` to prevent infinite busy loops.

## 3. Handle bad lines explicitly

Every non-`HX_OS_OK` return (including `HX_OS_EOF`) leaves the caller's output `hx_string` untouched:

| Status code | Meaning and recommended recovery action |
|---|---|
| `HX_OS_OK` | A complete, valid UTF-8 line was written into `line`. |
| `HX_OS_EOF` | End of input reached (any final non-LF-terminated line was already delivered on the preceding call). |
| `HX_OS_LIMIT` | The current physical line exceeds `max_line_bytes`. Call `hx_os_line_reader_skip_line()` to discard it, or close the reader. |
| `HX_OS_INVALID` | The current physical line contains malformed or truncated UTF-8. Call `hx_os_line_reader_skip_line()` to discard it, or close the reader. |
| `HX_OS_NO_MEMORY` | Allocation failed; any staged complete line is preserved so you can free memory elsewhere and retry. |
| `HX_OS_IO_ERROR` (or native I/O error) | Underlying stream error; partial bytes and error diagnostics are retained for retry if the stream backend is recoverable. |

> [!TIP]
> **Recovering from Oversized or Malformed Lines with `hx_os_line_reader_skip_line()`:**
> After a line-length (`HX_OS_LIMIT`) or UTF-8 validation (`HX_OS_INVALID`) failure, calling `hx_os_line_reader_next()` again will repeat the same error until you explicitly recover. Calling `hx_os_line_reader_skip_line()` discards **only** the offending physical line:
> - If its terminating LF was already consumed into the read-ahead buffer, `skip_line` clears the error state immediately without consuming the next line.
> - Otherwise, it drains and discards bytes from the stream without growing the staging buffer until it reaches LF or `EOF`.
> - If `skip_line` itself hits a transient I/O error, retry `hx_os_line_reader_skip_line()`; calling `next()` while a discard is still in progress returns `HX_OS_BUSY`.

`hx_os_line_reader_close()` frees the reader's buffers and nulls out the handle pointer. If closing an internally owned stream fails, the error is reported to the caller while the reader object itself is still freed; borrowed streams remain open.

## 4. C++11 loop

In C++11, include `<hx/LineReader.hpp>` to use the move-only `hx::LineReader` wrapper:

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

- `reader.value().next(line)` returns `hx::Result<bool>`: `true` indicates a new line was read into `line`, `false` indicates normal `EOF`, and an error result carries the failure status.
- `hx::LineReader` is move-only, uses no C++ exceptions, and closes automatically in its destructor; calling `close()` explicitly lets you check for stream close errors.
