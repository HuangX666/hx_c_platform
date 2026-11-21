# Message Frames

[简体中文](FRAMES.zh-CN.md)

TCP sockets, local stream sockets (Unix domain sockets / named pipes), and TLS connections are unstructured byte streams: a single write on one side may arrive split across multiple reads on the other, or several small writes may coalesce into a single read buffer. When your application wants each receive call to return **one complete JSON or binary message** without adopting the full session handshake and request-routing machinery of `hx_ipc`, wrap the stream in `hx_os_frame` for **4-byte big-endian length-prefixed framing**.

Build and run the self-contained [framed_messages.c](../examples/framed_messages.c) example to see the framing lifecycle in action (enable `HX_OS_BUILD_EXAMPLES=ON` per the [example setup guide](../examples/README.md#run) and build `hx_os_framed_messages`; no arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_framed_messages
```

Expected output:

```text
{"device":"door-1","request":42}
```

To run in a single process without opening network ports, the example wraps an in-memory stream (`hx_os_frame_from_stream`), writes a length-prefixed JSON message, seeks back to the beginning, and reads the complete message back out. On a live network connection, simply replace `from_stream` with `hx_os_frame_from_socket()` or `hx_os_frame_from_tls()`, omit the seek call, and use the same 4-byte length prefix on the peer. You can also drop this file straight into the [minimal C project template](USAGE.md#c-project).

## 1. Receive complete messages

Include `hx_os_frame.h`. On the wire, every frame consists of a **4-byte unsigned big-endian byte length (`uint32_t`) followed by exactly that many payload bytes**. TCP sockets, local stream sockets, TLS sessions, and custom stream callbacks all share this exact format (UDP already preserves datagram boundaries and is not wrapped by `hx_os_frame`). Peers written in Python, Go, Rust, C#, or Java only need to read and write the same 4-byte big-endian length prefix.

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

Here `connected_socket` is an established TCP or local stream socket. Because `hx_os_frame` **borrows** the underlying transport handle, always **destroy the frame wrapper before closing the socket**. Even when the OS delivers partial frames or coalesces multiple frames into a single read, `hx_os_frame_receive_text_until()` buffers and splits the stream internally so each call returns exactly one complete, UTF-8-validated message ready for your JSON or configuration parser.

## 2. Send and retry

```c
int accepted = 0;

status = hx_os_frame_send_text_until(frame,
	hx_string_view_from_cstr("{\"request\":42}"),
	hx_os_deadline_after_ms(5000), &accepted, NULL);
if (status == HX_OS_TIMEOUT && accepted)
	status = hx_os_frame_flush_until(
		frame, hx_os_deadline_after_ms(5000), NULL);
```

When sending, the library copies the complete message into an internal frame buffer before writing to the transport:
- **Checking `accepted` After a Timeout**: If `hx_os_frame_send_text_until()` returns `HX_OS_TIMEOUT` and sets `accepted` to non-zero (`1`), the frame object has **already taken ownership of your message and may have transmitted part of it**. **Do not call `send_text_until` again with the same message** (which would duplicate the frame on the wire); instead, call `hx_os_frame_flush_until()` to finish draining the retained frame buffer.
- **What Flush Confirms**: `HX_OS_OK` from `flush_until` confirms that the local transport accepted the remaining bytes—not that the remote application processed the business command. Use application-level request IDs and replies when end-to-end confirmation is required.

## 3. Ownership and limits

- **Payload Limits & Status Codes**:
  - By default, each direction buffers at most `1 MiB` per frame (configurable up to `64 MiB` at creation; zero-length empty frames are valid).
  - Partial incoming frames survive read timeouts (`HX_OS_TIMEOUT`) and caller output-allocation failures so the next receive call can resume seamlessly.
  - If `receive_text_until` encounters invalid UTF-8 bytes, it returns an encoding error while leaving the complete frame buffered so you can still retrieve the raw bytes via `hx_os_frame_receive_until()`.
  - A clean disconnect on a frame boundary returns `HX_OS_EOF`; an abrupt EOF in the middle of a header or payload returns `HX_OS_TRUNCATED_INPUT`. An incoming length header that exceeds the configured frame cap returns `HX_OS_LIMIT` and disables further reading on that stream.
- **Concurrency Rules**:
  - Concurrent sender threads are serialized internally (time spent waiting behind an earlier sender counts against the caller's `deadline`, and `HX_OS_DEADLINE_NOW` returns `HX_OS_TIMEOUT` immediately if the send side is busy).
  - When the underlying transport supports full-duplex I/O (such as sockets or TLS), **one reader thread and one writer thread can operate concurrently**.
  - Stop all active send/receive calls before closing the underlying transport, calling `abort`, or destroying the frame. Note that single-stream adapters (`hx_os_frame_from_stream`) share a single file cursor and require external serialization, and wrapping a synchronous stream does not give its underlying blocking read/write calls hard real-time deadlines.

> [!WARNING]
> **Drain or Explicitly `abort` Before Destroying**
> Destroying an `hx_os_frame` while a partial incoming frame or unflushed outgoing frame remains buffered returns `HX_OS_BUSY` (the library never silently drops half-transmitted frames). Either finish receiving/flushing or call explicit `abort` when abandoning the connection before destroying the frame. The C++11 `hx::Frame` wrapper borrows a caller-owned `hx_os_frame *` and returns `hx::Result` values including the send `accepted` flag.

## 4. Run the example

Build `hx_os_framed_messages` with `-DHX_OS_BUILD_EXAMPLES=ON` to run [framed_messages.c](../examples/framed_messages.c). The module is controlled by `HX_OS_ENABLE_FRAME` (`ON` by default), and each transport adapter (`from_socket`, `from_tls`, `from_stream`) requires its corresponding base module.

When working with a pair of unidirectional pipes or streams (such as a child process's `stdin` and `stdout`), call `hx_os_frame_from_streams()` (`hx::Frame::fromStreams()` in C++11) with separate reader and writer `hx_os_stream *` handles (or `NULL` for an unused direction) instead of writing custom callback glue.
