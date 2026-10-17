# Binary streams and bounded background file writing

[简体中文](STREAMS.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Use streams when a file or protocol defines numeric byte order, such as a binary device ID and temperature record.

Open [binary_stream.c](../examples/binary_stream.c). stream_memory → write_u32/write_f64 with BIG_ENDIAN → seek to start → read with the same byte order → close.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_binary_stream`. This program needs no arguments or input files.

**Expected result:** It prints `device=42 temperature=23.5`. Use a file stream for storage. For background output, buffered_file.c writes samples.jsonl and reports three saved records after successful close.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Choose the operation

| Task | Entry point | Execution |
| --- | --- | --- |
| Read/write a file with the same interface as memory | `hx_os_stream_open()` / `hx::Stream::openRead()` | Synchronous |
| Build or inspect binary messages in memory | `hx_os_stream_memory()` / `hx::Stream::memory()` | Synchronous, grows within a limit |
| Read an existing memory buffer | `hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` | Borrows immutable bytes |
| Write numbers in a specified byte order | `hx_os_stream_write_u32()` / `stream.write<uint32_t>()` | Encodes the numeric value, not a structure layout |
| Buffer writes and let a worker write the file | `hx_os_file_writer_open()` / `hx::FileWriter::open()` | One worker, bounded queue |

The binary stream and background writer have separate switches: `HX_OS_ENABLE_STREAM` and `HX_OS_ENABLE_BUFFERED_FILE`, both ON by default. The writer requires STREAM; `hx_os_file_writer_available()` reports whether both are enabled. Neither option starts a worker until an application creates a writer. They need no third-party library or thread pool.

## 2. Write and read a binary message

[binary_stream.c](../examples/binary_stream.c) is a complete C99 program. It puts device number 42 and temperature 23.5 in a memory stream, rewinds, then reads both values back. Build and run it from the repository root:

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream
# Linux/macOS:
./build/bin/hx_os_binary_stream
# Windows PowerShell:
.\build\bin\Release\hx_os_binary_stream.exe
```

Expected output: `device=42 temperature=23.5`. The first field occupies four bytes in big-endian order; the second occupies eight bytes as IEEE binary64. Both peers must agree on the field types and byte order. Writing an entire C structure would also write its padding and native layout, which is not a portable message format.

For C++11, include `<hx/Stream.hpp>`. This function returns a memory stream containing a single device number; it uses the same C implementation and no exceptions:

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

`stream.view()` returns borrowed bytes for memory streams; copy/use them before the next mutation or destruction. `Stream::fromMemory()` does not copy its input. `read<T>()` and `write<T>()` support fixed-width integers up to 64 bits, `float`, and `double`. The C API also directly supports 24-bit integers and generic widths of 1–8 bytes. Floating-point operations reject unsupported host representations.

## 3. Short I/O and failures

| Result | Meaning and caller action |
| --- | --- |
| `OK` from `read_some` / `write_some` | The returned byte count may be less than requested |
| `EOF` from `read_exact` | No byte of this request was read |
| `TRUNCATED_INPUT` from `read_exact` | A prefix was read, then the input ended |
| Other error from an exact operation | Inspect `completed`; an earlier prefix may have transferred |
| Numeric read error | The output numeric variable is unchanged; the stream position can have advanced |
| Numeric write error | A prefix of the encoded number may remain; blindly repeating the whole number is unsafe |

Use `read_exact` / `write_all` when transferring raw bytes and retain the reported progress to recover. Zero-byte transfers succeed without calling a backend. Custom callbacks must report their actual progress; an exact operation rejects `OK` with zero progress on a nonempty request to prevent an infinite loop. Native error codes are preserved.

Memory streams default to a 16 MiB limit. Growth can temporarily hold old and new buffers, at most twice the payload limit plus metadata. Borrowed and owned memory streams reject input/output buffers overlapping their own storage. All stream operations require caller serialization; file or callback operations may block. Closing consumes the handle, including a native close error. `flush(0)` drains user buffers; `flush(1)` requests native persistence. Memory streams reject persistence requests.

## 4. Save device readings in the background

[buffered_file.c](../examples/buffered_file.c) writes three JSON lines to `samples.jsonl`. The application submits each line, requests persistence, then closes. Build `hx_os_buffered_file` and run it from the same platform-specific `bin` directory shown above. The example creates/truncates `samples.jsonl` in its working directory.

The common C call is:

```c
size_t accepted = 0;
hx_os_status status = hx_os_file_writer_write_until(
	writer, json_line, json_bytes, &accepted,
	hx_os_deadline_after_ms(2000), &error);
```

This is a function-body fragment; `writer` is an open writer, `json_line`/`json_bytes` describe the input, and `error` is an `hx_os_error`. Returning OK means the queue owns a copy of every byte. It does not yet mean the file contains those bytes. On timeout/error, only `[0, accepted)` was accepted; retain and retry the suffix. Separate producers can call the same writer. Their calls are serialized, so bytes from one call do not interleave with another call. Several calls are not an atomic transaction.

The default queue is 1 MiB and the worker's maximum write request is 64 KiB. `open_ex()` accepts options and append flags. Queue storage is allocated before the worker starts; write and flush do not allocate. In-flight bytes still count against capacity. Thread stack/native synchronization/object storage are additional. A blocked disk operation can keep the queue full; waiting callers observe their deadlines.

## 5. Flush, retry and close

`flush_until()` captures bytes accepted at entry, waits for them, and flushes the sink. New writes do not extend that target. Concurrent flushes serialize, with one internal flush request. A timed-out flush can still finish later. `persistent=1` requests `fsync` / `FlushFileBuffers`, without promising physical-media durability.

A write or flush error is latched and stops further acceptance. `pending()` reports the error and remaining bytes. After fixing the cause, `retry()` resumes only the unwritten suffix. It does not replay a successful prefix. A native close failure consumes the sink; it cannot be retried.

If the worker has exited but its thread handle cannot be released, close/abort reports that cleanup error and retains the writer. Retry with the same owner. The sink is not closed again; its saved close result is returned once thread cleanup completes, at which point the owner is consumed.

`close_until()` stops acceptance, drains, flushes user buffers and closes. Timeout or a write/flush error retains the same pointer. The host can retry on a later shutdown attempt. `abort_until()` explicitly discards the remaining queue after the current native operation returns. It cannot terminate a hung syscall or third-party callback. Both take monotonic deadlines, including NOW and INFINITE. Final close/abort requires exclusion of all other public calls, including calls waiting to enter. For multiple producers, call `stop()` first to stop acceptance and wake them, join/quiesce those producers, then close. `stop()` retains the object and is safe alongside writes/flushes. Worker callbacks receive DEADLOCK if they try to wait on their own writer.

The C++ facade follows the thread-pool ownership model:

```cpp
hx_os_file_writer *owner = NULL; // Keep this owner across close retries.
auto opened = hx::FileWriter::open(owner, "samples.jsonl");
```

Check `opened`, then call `opened.value().write(...)`, `flushUntil(...)` and `closeUntil(...)`. A failed write result still has its accepted-byte count (`hasValue()` / `value()`). Destroying the facade does not block or dispose of a live worker; the host must finish close/abort while retaining `owner`. The console C example explicitly chooses an infinite cleanup wait after reporting an error. A GUI or service should retain its owner and schedule retries instead.

## 6. Validation and performance boundary

See [platform support](PLATFORMS.md) for target build/deployment requirements and limits. Public examples are independent of private maintainer regressions.

The worker amortizes native calls by writing queued chunks. It adds mutex/condition coordination and one worker per writer; acceptance may involve multiple copies when a call is larger than available space. No measured throughput or latency improvement is claimed. Streams use direct native calls for file/memory backends and indirect calls only for user-provided backends. The background writer is intended for workloads that benefit from decoupling producers from synchronous output; benchmark the actual file/device and producer load.
