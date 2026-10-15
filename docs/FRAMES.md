# Message frames

[简体中文](FRAMES.zh-CN.md)

**Start with a complete program.** Wrap a TCP or TLS byte stream when the application wants one complete JSON message per receive.

Open [framed_messages.c](../examples/framed_messages.c). Memory stream → from_stream → send_text_until writes length and JSON → seek to start → receive_text_until → destroy frame → close stream.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_framed_messages`. This program needs no arguments or input files.

**Expected result:** It prints `{"device":"door-1","request":42}`. For a real connection use from_socket/from_tls, remove seek, and have the peer implement the same length framing.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Receive complete messages

Include `hx_os_frame.h`. A frame uses a four-byte unsigned big-endian length followed by that many payload bytes. TCP, local stream sockets, TLS and user callbacks share this wire format. UDP keeps its datagram semantics and is not wrapped by this interface. Other languages need only implement this length prefix.

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

`connected_socket` is an existing connected TCP/local socket. The frame borrows it: destroy the frame before closing the socket. A receive call returns exactly one message even when native reads split or combine messages. Parse the returned UTF-8 text with the configuration/document API when using JSON.

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

The library copies a message before admitting it. If `accepted` is true, flush the retained message after a timeout; sending the same message again would duplicate it. Successful flushing confirms local transport progress, not peer processing. Use application request IDs and acknowledgements when delivery matters.

## 3. Ownership and limits

The default payload limit is 1 MiB per direction; an explicit limit may be at most 64 MiB. Empty messages are valid. Caller output copies and transient buffer growth consume additional memory. Partial receives survive timeout and output allocation failure. Invalid UTF-8 leaves the frame available for `hx_os_frame_receive_until` as raw bytes. Clean EOF is `EOF`; EOF inside a frame is `TRUNCATED_INPUT`. Excessive lengths return `LIMIT` and disable reading.

Senders serialize internally. A reader and a sender can run on separate threads when the backend supports full duplex. Exclude transport close, abort and frame destruction from active calls. Stream adapters share a cursor and require external serialization; their native calls do not gain a deadline from this wrapper. All deadlines are cooperative, not hard real-time guarantees.

Destroy returns `BUSY` while partial framing remains. Finish receiving/flushing, or explicitly abort and abandon the transport before destroying. The library never silently drops a partial message. C++ `hx::Frame` is a borrowed facade over a durable `hx_os_frame *` owner and offers `Result` values, including the send acceptance flag.

## 4. Run the example

Build `hx_os_framed_messages` with `HX_OS_BUILD_EXAMPLES=ON`. [framed_messages.c](../examples/framed_messages.c) writes and reads JSON using a memory stream. Replace `from_stream` with `from_socket` or `from_tls` to retain the same framing on a connected transport. Enable with `HX_OS_ENABLE_FRAME`; backend factories additionally require their modules.

For a read/write pipe pair, `hx_os_frame_from_streams` accepts distinct reader and writer streams, or NULL for an absent direction. Their underlying operations remain synchronous; each stream must be independently safe for the requested concurrent use. `hx::Frame::fromStreams` provides the same C++ entry.
