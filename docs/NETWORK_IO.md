# Practical Network I/O: Packet Routing, Batch/Vector I/O, Happy Eyeballs & Trusted-LAN KCP

[简体中文](NETWORK_IO.zh-CN.md) · [Network Options](NETWORK_OPTIONS.md) · [Installed SDK](INSTALL.md)

Beyond basic socket reads and writes, networked applications frequently need finer-grained control: multi-homed devices must identify which network interface received a UDP packet and reply from that same interface, high-rate services want batched datagram I/O or scatter/gather vector sends without intermediate buffer copies, clients need dual-stack IPv4/IPv6 hostname dialing, and multi-machine deployments on a trusted LAN often want reliable KCP sessions. This guide covers these entry points, runnable examples, and failure-handling rules.

## 1. Choose an entry point

| Task | Entry Point | What the Caller Owns |
| --- | --- | --- |
| Wait for a single UDP datagram | `hx_os_socket_receive_from_until` | `hx_os_socket` handle and receive buffer |
| Identify the receiving interface and local destination IP | `hx_os_socket_receive_packet_until` | `hx_os_socket`, buffer, and returned `hx_os_packet_info` |
| Select the outgoing interface or source IP for a UDP reply | `hx_os_socket_send_packet_until` | Optional per-packet route struct (`hx_os_packet_info`) |
| Send or receive up to 64 UDP datagrams in one call | `hx_os_socket_send_batch_until` / `receive_batch_until` | Array of up to 64 datagram entries and their buffers |
| Send or receive split headers and payloads (Scatter/Gather) | `hx_os_socket_sendv_until` / `receivev_until` | Array of up to 64 ordered borrowed buffers |
| Connect by hostname with staggered IPv4/IPv6 fallback | `hx_os_tcp_dial_until` | `hx_os_network` lease and the connected `hx_os_socket` |
| Perform cancellable DNS lookups with a deadline | `hx_os_resolver_resolve_until` | Explicit `hx_os_resolver` (c-ares) and returned address list |
| Exchange reliable KCP messages across trusted LAN machines | `hx_os_kcp_session_*` | Session handle (`hx_os_kcp_session`) and received messages |

All `_until` functions accept an **absolute monotonic deadline**. Create one with `hx_os_deadline_after_ms(...)` at the start of an operation and reuse it across steps.
- Passing `HX_OS_DEADLINE_NOW` to a UDP call attempts one non-blocking transfer and returns `HX_OS_TIMEOUT` immediately if the socket would block.
- A **zero-length UDP datagram** is a valid message.
- Raw TCP sockets remain byte streams; use [Message Frames (`hx_frame`)](FRAMES.md) when you need discrete message boundaries over TCP.

## 2. Run the UDP packet example

Build the standalone [udp_packet.c](../examples/udp_packet.c) example in your existing build directory:

```sh
cmake --build build --config Release --target hx_os_udp_packet
```

Run `build/bin/Release/hx_os_udp_packet.exe` on Windows (Visual Studio) or `build/bin/hx_os_udp_packet` on Linux. It requires no fixed port, second process, or external server—it binds an ephemeral loopback port, sends a JSON datagram to itself, reads back both the payload and ancillary packet metadata, and prints:

```text
JSON: {"device_id":7,"state":"online"}
Destination: 127.0.0.1
Interface: <this machine's loopback index>
```

### Replying via the Receiving Interface on Multi-Homed Hosts

On a device with both Ethernet and Wi-Fi active, knowing the sender's IP address alone does not tell you which local IP address or network interface the sender contacted. Enabling packet metadata (`hx_os_socket_set_packet_info`) before receiving populates `hx_os_packet_info`. You can then pin the outgoing reply to that same interface without altering the socket's global routing table:

```c
hx_os_packet_info route = {0};
route.flags = HX_OS_PACKET_INTERFACE;
route.interface_index = received_info.interface_index;

status = hx_os_socket_send_packet_until(socket, &sender, &route,
	json, json_bytes, deadline, &sent, &error);
```

- **Inspect `flags` First**: Always check `received_info.flags` before reading a metadata field; fields not supplied by the OS are never fabricated. Note that `info.local.port` is always `0` (query the socket's bound address separately if you need the local port).
- **Specifying a Unicast Source IP**: You can also set `HX_OS_PACKET_LOCAL_ADDRESS` in `route.flags` to select a specific local unicast source IP. **Never pass a multicast or broadcast destination address as a reply's source IP.**
- **C++11 Wrapper**: Include `<hx/Datagram.hpp>` to use the non-owning `hx::Datagram` wrapper with the same packet metadata semantics.

## 3. Truncation, batches and vectors

- **UDP Truncation (`HX_OS_LIMIT`)**: When a received datagram is larger than your buffer, the receive call returns `HX_OS_LIMIT`. **The datagram has already been consumed from the OS socket queue, and its excess trailing bytes are permanently discarded by the kernel** (on Windows, `WSAEMSGSIZE` is also recorded in `hx_os_error`). Inspect the copied byte count and truncation flags, then move on to the next message—retrying the receive call cannot recover the discarded tail of the truncated packet.
- **Batched Datagram I/O**:
  - `hx_os_socket_send_batch_until()` returns the number of complete datagrams accepted before the first error; retry only the remaining unsent entries.
  - `hx_os_socket_receive_batch_until()` waits until **at least one datagram** arrives, then drains up to 64 immediately available datagrams without blocking to fill the entire array. If the final datagram in the batch is truncated, it is included in the completed count and the call returns `HX_OS_LIMIT`.
- **Scatter/Gather Stream Vectors**:
  - `hx_os_socket_sendv_until()` and `hx_os_socket_receivev_until()` process up to 64 buffers in sequence without requiring you to allocate a combined contiguous buffer.
  - The `completed` output reports the **total byte count transferred across all buffers**, not the number of buffers completed. `sendv_until` loops until the entire prefix is sent or the deadline expires; `receivev_until` returns as soon as any bytes arrive. If a timeout occurs, advance your buffer views by `completed` bytes before continuing.

## 4. Change an attached socket safely

Once an `hx_os_socket` is attached to an `hx_os_loop`, ordinary `hx_os_socket_set_*` option setters return `HX_OS_BUSY`.

To reconfigure a socket that is already attached to an event loop:
1. On the loop's owner thread, finish or cancel any in-flight operations on that socket and consume their completion callbacks.
2. Call `hx_os_loop_socket_configure_tcp()` or `hx_os_loop_socket_multicast()` (from another thread, post a normal `HX_OS_LOOP_POST` callback to run this on the loop thread). These helpers preserve the native IOCP/epoll association while rejecting changes if unfinished I/O remains.

> [!NOTE]
> **Platform-Specific Socket Options**
> - `hx_os_socket_set_buffers()` requests kernel send/receive buffer sizes, while `hx_os_socket_get_buffers()` reads back the actual capacities granted by the OS (which may be clamped, rounded, or doubled on Linux).
> - `hx_os_socket_set_tcp_user_timeout()` configures the Linux/Android `TCP_USER_TIMEOUT` for unacknowledged in-flight data; it does not replace connect deadlines or application heartbeats.
> - `hx_os_socket_set_udp_icmp_errors()` controls Windows UDP `SIO_UDP_CONNRESET` error reporting.
> - Unsupported platform extensions return `HX_OS_NOT_SUPPORTED` rather than silently mapping to an option with different semantics.

## 5. Existing native sockets and shutdown

- **Importing Native Sockets (`hx_os_socket_import`)**:
  - Passing `HX_OS_SOCKET_DUP` duplicates an already non-blocking native IP socket into a new `hx_os_socket` handle (the duplicated descriptor shares kernel endpoint state and options with the original; it is not a second independent connection).
  - Passing `HX_OS_SOCKET_TAKE` transfers ownership of the caller's native handle only when the import succeeds.
  - Neither handle may have active I/O or an incompatible event-loop registration at import time. On Windows, because Winsock provides no read-only query for non-blocking mode, the caller must guarantee that the socket is already non-blocking and was created with `WSA_FLAG_OVERLAPPED`.
- **Sending FIN and Draining Input (`hx_os_socket_shutdown_drain_until`)**:
  - Explicitly sends a TCP `FIN` (shutting down the write half) and discards incoming bytes until `HX_OS_EOF` or the deadline.
  - Call this only when remaining incoming bytes are unneeded. It **never closes the socket handle itself**; on timeout, the same half-closed handle remains valid so you can retry draining or close it directly. Note that neither `HX_OS_EOF` nor a successful drain proves that the remote peer's application logic finished committing your data.

## 6. KCP JSON request/reply on a trusted network

While `hx_ipc_*` factories are restricted to same-machine loopback, `hx_os_kcp_session_*` lets you exchange reliable, correlated KCP messages **across separate machines on a trusted LAN**.

Build [kcp_session.c](../examples/kcp_session.c):

```sh
cmake --build build --config Release --target hx_os_kcp_session
```

Endpoint strings accept an optional `kcp://` prefix. Calling `hx_os_kcp_session_address()` on a listener returns its resolved URI (including the assigned port when bound to port `0`), which you can pass to clients (replace a wildcard `0.0.0.0` bind address with the server's LAN IP when connecting from another host).

**Run in two terminals on Linux:**

```sh
# Server: listens for one request for up to 30 seconds, then exits.
build/bin/hx_os_kcp_session server 0.0.0.0:39031

# Client: use 127.0.0.1 locally, or the server's LAN IP from another machine.
build/bin/hx_os_kcp_session client 127.0.0.1:39031
```

(On Windows, run `build/bin/Release/hx_os_kcp_session.exe` with the same arguments.)

Expected output:
- Server prints: `Request: {"type":"status","device_id":7}`
- Client prints: `Reply: {"accepted":true,"device_id":7}`

### Session Lifecycle and Security Boundaries

- **Threading & Ownership**: Each `hx_os_kcp_session` endpoint owns one background I/O worker thread, a bounded peer table, and bounded message queues. Send calls copy the payload and are thread-safe for application callers; receive calls return one complete message or connection event at a time (release each message once). A timed-out connect call may still return a non-NULL session handle—continue waiting or close it with a deadline rather than leaking the pointer. Automatic reconnect never silently replays prior business requests.
- **Wire Protocol**: Remote KCP sessions use the exact same [HXIP v1 session envelope and KCP message-mode protocol](IPC.md#8-hxip-v1-for-peers-in-other-languages) as local IPC. Peers written in other languages must implement both layers; raw KCP packets without the outer HXIP envelope will be rejected.
- **Trusted Networks Only**: `hx_os_kcp_session` provides neither encryption nor peer authentication (and never modifies OS firewall rules). Use it only on trusted internal networks; use [TLS](TLS.md) whenever you need certificate verification and encrypted streams.

## 7. Domain names and platform boundaries

- **IDN Hostname Conversion (`hx_os_hostname_ascii`)**: Converts a UTF-8 internationalized domain name into its Punycode ASCII representation for raw socket, TLS, and DNS APIs without performing a network lookup. Non-ASCII names require IDN support in the linked `libcurl` build; if unavailable, the call returns `HX_OS_NOT_SUPPORTED` and leaves the output buffer unchanged. Backend IDNA rules apply (this function does not perform visual confusable/homograph detection).
- **Transient Interface Snapshots & Mobile Rules**: Interface indices and route lookups are point-in-time snapshots, not permanent device IDs—re-query after network change notifications. On Android, the host APK manages permissions, Android `Network` handle selection, and lifecycle (an Android `Network` handle is distinct from an OS interface index).

## 8. Handle disconnects, slow consumers and shutdown

When a network call returns an error or timeout, always check **how much progress was already accepted** and **whether you still own the handle** before deciding to resume, retry, or close. **Never blindly resend an entire business message just because a call returned `HX_OS_TIMEOUT`.**

| Runtime Scenario | Library Behavior | Recommended Application Action |
| --- | --- | --- |
| Peer crashes or network path goes silent | IPC / KCP sessions detect inactivity and reconnect under a fresh session ID; raw sockets and TLS do not auto-reconnect | Handle disconnect events and re-verify device identity on the new session. Old requests are never replayed automatically; use business operation IDs and server deduplication to retry safely |
| DNS server stops responding | Explicit `hx_os_resolver` (c-ares) queries honor deadlines and cancellation tokens; default OS DNS calls may block | Use cancellable resolver APIs when bounded shutdown latency matters (see [Cancellable DNS](NETWORK_OPTIONS.md#123-use-an-event-loop-or-coroutine) and [HTTP DNS Cancellation](HTTP_FILES.md#4-cancel-even-when-dns-receives-no-reply)) |
| Slow consumer (backpressure) | Bounded IPC queues return `HX_OS_LIMIT`; a timed-out TLS write may report a non-zero `accepted` byte count | Keep draining and releasing received messages. On the same live TLS connection, **send only the unaccepted suffix** after a partial timeout (or call `flush_until` if all bytes were accepted) |
| Frequent connect / disconnect churn | Messages, requests, and connections have independent owners; stale `peer` IDs are invalid on new sessions | Release each message and request once; retain client handles until close succeeds; close or abort finished TLS server peers to free listener slots |
| Shutdown during active waits or backoff | Close rejects new calls and wakes active waiters; orderly TLS close also attempts to drain accepted ciphertext | Pass an explicit deadline and check both the return status and handle pointer. If the pointer remains non-NULL on timeout, keep the handle, allocator, and context alive to retry close (call explicit `abort` only when discarding data is acceptable; never kill worker threads) |

> [!TIP]
> **Why Partial TLS Writes Must Resume from `accepted`**
> Suppose you call a TLS write with an 8 MiB buffer and it returns `HX_OS_TIMEOUT` with `accepted = 64 KiB`. Those first 64 KiB have already been encrypted and queued inside the TLS session. Once the peer resumes reading, pass only the remaining `8 MiB - 64 KiB` starting at offset `64 KiB`—resubmitting from offset `0` would corrupt the stream by duplicating the first 64 KiB. And as always, transport acceptance (whether TLS `accepted` or IPC/KCP `ACK`) only confirms transport buffering; require an application-level reply to confirm database commits or hardware actions.

When validating your target environment, reuse the repository's [TCP Business Example](TCP_BUSINESS.md), [TLS Examples](TLS.md), [Trusted-LAN KCP Example](#6-kcp-json-requestreply-on-a-trusted-network), and [HTTP File Example](HTTP_FILES.md) to test slow receivers, peer restarts, and retryable close paths. Treat emulator shells, ARM user-mode QEMU, physical boards, and signed Android APKs as distinct targets—if a path is blocked by missing QEMU `setsockopt` translation, Android Unix-socket sandbox policies, or WSL2 mirrored IPv4 UDP fragmentation, record that platform constraint explicitly and select an appropriate transport.
