# Binary streams and bounded background file writing

[简体中文](STREAMS.zh-CN.md) · [README](../README.md)

Use the binary stream API (`hx_os_stream` / `hx::Stream`) when a file format or wire protocol specifies an explicit byte order (endianness) for numeric fields, or when you want a single interface for reading and writing both files and memory buffers. When producer threads need to record data (such as sensor readings or JSON Lines logs) without blocking on synchronous disk writes, pair streams with the bounded background file writer (`hx_os_file_writer` / `hx::FileWriter`).

To see binary streams in action, build and run [binary_stream.c](../examples/binary_stream.c). The program opens an in-memory stream, writes device ID `42` (`uint32_t`) and temperature `23.5` (`double`) in big-endian byte order (`HX_OS_BIG_ENDIAN`), seeks back to the beginning, and reads both values back out.

Follow the [example run instructions](../examples/README.md#run) to enable `HX_OS_BUILD_EXAMPLES=ON` and build `hx_os_binary_stream` (it requires no command-line arguments or input files):

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream

# Linux / macOS:
./build/bin/hx_os_binary_stream

# Windows PowerShell:
.\build\bin\Release\hx_os_binary_stream.exe
```

Running the binary prints `device=42 temperature=23.5`. You can switch from a memory stream to a file stream with a one-line change, or see Section 4 and [buffered_file.c](../examples/buffered_file.c) for background file output (which writes three JSON records to `samples.jsonl` and reports completion after a clean close). You can also drop these examples directly into the [minimal project](USAGE.md#c-project) template.

## 1. Choose the operation

| Task | C99 / C++11 Entry Point | Execution Model |
| --- | --- | --- |
| Read or write a disk file with the unified stream API | `hx_os_stream_open()` / `hx::Stream::openRead()` | Synchronous; wraps native file descriptors/handles |
| Build or parse binary messages in memory | `hx_os_stream_memory()` / `hx::Stream::memory()` | Synchronous; grows dynamically up to a configurable limit |
| Read from an existing memory buffer | `hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` | Zero-copy; borrows immutable caller bytes |
| Read or write numbers with explicit byte order | `hx_os_stream_write_u32()` / `stream.write<uint32_t>()` | Encodes portable numeric values rather than dumping C struct layout |
| Buffer writes in a bounded queue and flush on a worker thread | `hx_os_file_writer_open()` / `hx::FileWriter::open()` | One dedicated background worker thread and bounded ring buffer per writer |

**CMake Feature Flags**: Binary streams and the background file writer are controlled by `HX_OS_ENABLE_STREAM` and `HX_OS_ENABLE_BUFFERED_FILE` (both `ON` by default). The background writer depends on `HX_OS_ENABLE_STREAM`; call `hx_os_file_writer_available()` at runtime to check whether both are enabled. Neither module requires third-party dependencies or a thread pool, and no background thread is spawned until your application explicitly opens a file writer.

## 2. Write and read a binary message

Writing raw C structs directly to disk or the network exposes compiler-specific padding bytes and host CPU endianness. With `hx_os_stream`, the sender and receiver agree on exact field widths and byte order—for example, a four-byte big-endian unsigned integer followed by an eight-byte IEEE 754 `binary64` floating-point value.

For C++11, include `<hx/Stream.hpp>`. The function below builds an in-memory binary message containing a 32-bit device ID using `hx::Result<T>` without C++ exceptions:

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
> **Memory Stream Lifetime and Supported Numeric Widths**
> - **Borrowed view lifetime**: `stream.view()` (or `hx_os_stream_memory_view`) returns a borrowed slice pointing into the memory stream's internal buffer. Copy or consume those bytes before the next write, resize, or stream destruction.
> - **Immutable memory buffers**: `hx_os_stream_from_memory()` / `hx::Stream::fromMemory()` borrows the caller's buffer without copying; keep the source buffer alive and unmodified for the lifetime of the stream.
> - **Numeric types**: C++ `read<T>()` and `write<T>()` support fixed-width signed and unsigned integers up to 64 bits, `float`, and `double`. The C API additionally provides 24-bit integer helpers (`u24` / `i24`) and arbitrary 1–8 byte integer encoders. Floating-point helpers verify standard IEEE 754 host representation.

## 3. Short I/O and failures

When working with streams, distinguish between partial transfers (`read_some` / `write_some`) and exact-length transfers (`read_exact` / `write_all`):

| Outcome | Meaning and Caller Action |
| --- | --- |
| `HX_OS_OK` from `read_some` / `write_some` | The transfer succeeded, but the **actual byte count may be smaller than requested** (check the returned count) |
| `HX_OS_EOF` from `read_exact` | **Zero bytes** were read because the stream is already at end-of-file |
| `HX_OS_TRUNCATED_INPUT` from `read_exact` | Some prefix bytes were read before the input ended early |
| Other error from `read_exact` / `write_all` | Transfer failed part-way; inspect `completed` to see how many leading bytes transferred before the error |
| Numeric read failure (e.g., `read_u32`) | The caller's output variable is untouched, though the stream position may have advanced if a partial prefix was read |
| Numeric write failure (e.g., `write_u32`) | A partial prefix of the encoded number may have been written; **do not** blindly re-write the entire number at the current offset |

When implementing transport recovery, transfer raw byte buffers with `hx_os_stream_read_exact` / `hx_os_stream_write_all` and use the reported `completed` byte count to resume. Zero-byte transfers succeed immediately (`HX_OS_OK`) without invoking the backend. Custom stream callbacks must report accurate byte progress; if a custom callback returns `HX_OS_OK` with zero progress on a non-empty request, exact operations fail with an I/O error to prevent an infinite loop.

> [!NOTE]
> **Capacity Limits, Concurrency, and Flush Semantics**
> - **Memory stream limits**: Growable memory streams default to a `16 MiB` payload limit. During buffer reallocation, both the old and new buffers exist briefly (peaking at up to twice the payload limit plus metadata). Both owned and borrowed memory streams reject caller input/output buffers that overlap the stream's own storage.
> - **Serialization and closing**: Stream instances are not internally mutex-locked; serialize access from the caller side. Closing a stream always consumes the handle, even if the underlying native close operation reports an error.
> - **Flush modes**: `flush(0)` drains user-space stream buffers, whereas `flush(1)` also requests OS persistence (`fsync` / `FlushFileBuffers`). Memory streams reject `flush(1)` persistence requests.

## 4. Save device readings in the background

When producer threads cannot afford to block on slow or spiky disk I/O, use `hx_os_file_writer`. The complete example [buffered_file.c](../examples/buffered_file.c) writes three JSON Lines records to `samples.jsonl` in the current working directory (creating or truncating the file), flushes with persistence, and closes cleanly. Build target: `hx_os_buffered_file`.

In day-to-day application code, producers submit records with `hx_os_file_writer_write_until`:

```c
size_t accepted = 0;
hx_os_status status = hx_os_file_writer_write_until(
	writer, json_line, json_bytes, &accepted,
	hx_os_deadline_after_ms(2000), &error);
```

Here `writer` is an open background writer, and `json_line` / `json_bytes` describe the caller's buffer:

- **`HX_OS_OK` means queued, not yet on disk**: When the call returns `HX_OS_OK`, the bounded queue has copied every byte of `json_line`, so the caller can immediately reuse its buffer while the background worker writes to disk asynchronously.
- **Partial acceptance on timeout or error**: If slow disk I/O keeps the queue full until the monotonic deadline expires (2000 ms above), the call returns `HX_OS_TIMEOUT` (or an active sink error) and sets `accepted` to the prefix length `[0, accepted)` that made it into the queue. Retry only the remaining suffix `json_line + accepted` (`json_bytes - accepted` bytes).
- **Multi-producer serialization**: Multiple producer threads can share a single `writer`. Calls are internally serialized so bytes from a single `write_until` call never interleave with another thread's call (though separate calls do not form an atomic multi-call transaction).
- **Preallocated memory budget**: By default, the ring buffer is `1 MiB` and the worker writes in chunks of up to `64 KiB` (use `hx_os_file_writer_open_ex` to customize capacities or open in append mode). Queue memory is allocated once before the worker thread starts; `write` and `flush` perform **zero heap allocations**. Bytes currently being written to the OS still count against queue capacity until the native write call completes.

## 5. Flush, retry and close

`hx_os_file_writer` provides explicit controls for flushing, recovering from disk errors, and shutting down cleanly:

- **Flushing (`hx_os_file_writer_flush_until`)**: Snapshots the byte position accepted at entry, waits for the worker to write everything up to that mark, and flushes the underlying sink. Subsequent writes from other threads do not push that target forward. Concurrent flush calls serialize behind a single internal flush request. If a flush times out (`HX_OS_TIMEOUT`), the request remains active and can finish later. Passing `persistent = 1` requests `fsync` / `FlushFileBuffers` (physical media durability remains subject to hardware guarantees).
- **Latched errors and recovery (`pending` / `retry`)**: If a background write or flush fails, the writer latches the error and stops accepting new writes. Call `hx_os_file_writer_pending()` to inspect the latched error and the number of unwritten bytes remaining in the queue. After resolving the underlying issue (such as freeing disk space), call `hx_os_file_writer_retry()` to resume writing the remaining suffix without replaying bytes that already succeeded. (Note: a native failure during sink `close` consumes the underlying sink and cannot be retried.)
- **Closing and aborting (`close_until` / `abort_until` / `stop`)**:
  - `hx_os_file_writer_close_until()` stops accepting new data, drains the queue, flushes user buffers, and closes the sink. If draining times out or hits a write/flush error, the function returns that error and **retains the `writer` pointer** so the application can retry closing later.
  - `hx_os_file_writer_abort_until()` stops acceptance and **discards any queued bytes not yet handed to the OS**, but still waits for any in-flight native syscall or callback to return before joining the worker thread (the library never forcibly kills a thread stuck inside a syscall).
  - **Quiescing multiple producers**: Final `close_until` or `abort_until` requires exclusive access—no other thread may be inside or waiting to enter a public call on that writer. In a multi-producer application, call `hx_os_file_writer_stop()` first (it safely runs alongside active writes/flushes, stops further acceptance, and wakes blocked producers), join or quiesce your producer threads, and then call `close_until()`. If a custom sink callback tries to wait on its own writer from the worker thread, it immediately receives `HX_OS_DEADLOCK`.
  - **Thread-cleanup retries**: If the worker thread has exited and closed the sink, but the OS fails to release the thread handle itself, `close_until` / `abort_until` reports that cleanup error and retains the `writer`. Simply retry with the same `owner` pointer: the sink will not be closed a second time, and once thread cleanup succeeds, the saved sink close result is returned and `owner` is set to `NULL`.

In C++11, `hx::FileWriter` uses the same explicit `owner` pointer pattern as `hx::ThreadPool` so a timed-out close never causes a destructive destructor hang or use-after-free:

```cpp
hx_os_file_writer *owner = NULL; // Retain this owner across close retries.
auto opened = hx::FileWriter::open(owner, "samples.jsonl");
```

After checking `opened`, call `opened.value().write(...)`, `flushUntil(...)`, and `closeUntil(...)`. Even when a write fails or times out, you can still inspect the accepted byte count via `hasValue()` and `value()`. Destroying the `hx::FileWriter` wrapper does **not** block or destroy a running worker thread; the host application must keep `owner` alive until `closeUntil()` or `abortUntil()` completes.

## 6. Validation and performance boundary

See [platform support](PLATFORMS.md) for compiler, OS, and deployment requirements across supported targets.

File and memory streams invoke native OS calls or memory copies directly, using function-pointer dispatch only when configured with a custom user backend. The background file writer (`hx_os_file_writer`) amortizes OS write calls by batching queued records into chunks of up to `64 KiB` and decouples producer threads from synchronous disk latency. In exchange, it adds mutex/condition-variable coordination, ring-buffer copying (up to two `memcpy` segments when a write wraps around the ring or exceeds free space), and one OS thread per writer instance. Always benchmark against your actual storage device and producer workload.
