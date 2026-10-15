# Practical network I/O

[简体中文](NETWORK_IO.zh-CN.md) · [Network options](NETWORK_OPTIONS.md) · [Installed SDK](INSTALL.md)

## 1. Choose an entry point

| Task | Entry point | What you own |
| --- | --- | --- |
| Wait for one UDP message | `hx_os_socket_receive_from_until` | Socket and receive buffer |
| Identify the receiving interface and local destination | `hx_os_socket_receive_packet_until` | Socket, buffer and returned metadata value |
| Choose the source interface for a UDP reply | `hx_os_socket_send_packet_until` | Optional per-packet route value |
| Send/receive several datagrams | `hx_os_socket_send_batch_until` / `receive_batch_until` | Up to 64 entries and their buffers |
| Send a header and payload without combining buffers | `hx_os_socket_sendv_until` | Ordered borrowed buffers |
| Connect by hostname with IPv4/IPv6 fallback | `hx_os_tcp_dial_until` | Network lease and returned socket |
| Cancel DNS network waiting | `hx_os_resolver_resolve_until` | Explicit c-ares resolver |
| Reliable KCP messages across trusted machines | `hx_os_kcp_session_*` | Explicit session owner and received messages |

All `_until` arguments are absolute monotonic deadlines. Construct one with `hx_os_deadline_after_ms(...)` and reuse it across the action. UDP `NOW` attempts once, then returns `TIMEOUT` if it would block. A zero-length datagram is a valid message. Raw TCP remains a byte stream; use [message frames](FRAMES.md) when message boundaries matter.

## 2. Run the UDP packet example

Build the complete [udp_packet.c](../examples/udp_packet.c) program in your existing directory:

```sh
cmake --build build --config Release --target hx_os_udp_packet
```

Run `build/bin/Release/hx_os_udp_packet.exe` on Visual Studio builds, or `build/bin/hx_os_udp_packet` on Linux. It sends a JSON datagram to its own ephemeral loopback port, receives it and prints:

```text
JSON: {"device_id":7,"state":"online"}
Destination: 127.0.0.1
Interface: <this machine's loopback index>
```

There is no fixed port, second process or external service to prepare. The example enables packet information before receiving. Check metadata flags before reading a field; absent information is not invented. `info.local.port` is zero: query the bound port separately only if needed.

For a device with Ethernet and Wi-Fi, receiving the sender's address alone does not tell you which local address it contacted. Packet information adds that distinction. To select the outgoing interface without changing the socket's global route:

```c
hx_os_packet_info route = {0};
route.flags = HX_OS_PACKET_INTERFACE;
route.interface_index = received_info.interface_index;

status = hx_os_socket_send_packet_until(socket, &sender, &route,
	json, json_bytes, deadline, &sent, &error);
```

Here `received_info` and `sender` are outputs from the previous packet receive. Only set `INTERFACE` when reception reported it. For a unicast local source you may also set `LOCAL_ADDRESS`; never reuse a multicast or broadcast destination as a reply's source address. This selects a route, not authentication or guaranteed delivery. C++ can use the borrowed `hx::Datagram` facade from `<hx/Datagram.hpp>`.

## 3. Truncation, batches and vectors

`LIMIT` means a datagram was consumed but did not fit; its remaining bytes are discarded by the OS. Inspect the copied byte count and truncation flags, then receive the next message. Windows also preserves `WSAEMSGSIZE` in the error record. Retrying cannot retrieve the missing suffix.

Batch send returns the actual number of complete packets accepted before the first failure. Retry only the remaining entries. Batch receive waits for the first packet, then returns the currently available batch; it does not wait to fill all 64 entries. A consumed truncated final entry is included in the count and the call returns `LIMIT`.

Stream vectors visit up to 64 buffers in order without a combined allocation. `completed` counts bytes across the entire prefix, not the number of buffers. `sendv_until` attempts the complete prefix; `receivev_until` returns when some data is available. On timeout, advance by the actual completed bytes. These interfaces reduce application-side copying; no throughput improvement is claimed without measurement.

## 4. Change an attached socket safely

Ordinary option setters still return `BUSY` after loop attachment. On the loop owner thread, first finish or cancel the pending socket operations and consume their completions. Then call `hx_os_loop_socket_configure_tcp` or `hx_os_loop_socket_multicast`. Submit a normal loop `POST` from another thread to arrange this work. These helpers preserve the native loop association and reject unfinished I/O; they do not introduce another queue.

`hx_os_socket_set_buffers` requests capacities, while `get_buffers` returns actual OS values, which may be rounded or doubled. `set_tcp_user_timeout` configures Linux/Android unacknowledged-data timing; it does not replace a connect deadline or application heartbeat. `set_udp_icmp_errors` controls Windows UDP reset reporting. Unsupported platform extensions return `NOT_SUPPORTED` rather than pretending that identical option names have identical effects.

## 5. Existing native sockets and shutdown

`hx_os_socket_import(network, &native, HX_OS_SOCKET_DUP, &socket, &error)` creates another handle for an already nonblocking IP socket. `TAKE` instead consumes the caller's handle only after success. The duplicate shares endpoint state and options with the original; it is not an independent connection. Neither input may have active I/O or an existing incompatible event-loop registration. Windows requires caller-guaranteed nonblocking and OVERLAPPED capability; Winsock does not expose a read-only nonblocking-mode query.

`hx_os_socket_shutdown_drain_until` explicitly sends FIN and discards remaining input while waiting for EOF. Use it only when those remaining bytes are unwanted. It never closes the socket; timeout leaves the same half-closed owner for retry or ordinary close. Neither EOF nor successful drain proves remote business completion.

## 6. KCP JSON request/reply on a trusted network

Build [kcp_session.c](../examples/kcp_session.c):

```sh
cmake --build build --config Release --target hx_os_kcp_session
```

Input endpoints accept an optional `kcp://` prefix. `hx_os_kcp_session_address` returns that URI, including an ephemeral listener port; it can be passed directly to a client. Replace a wildcard bind address with the server's reachable IP when connecting.

In two terminals on Linux:

```sh
# Server; listens for one request for up to 30 seconds.
build/bin/hx_os_kcp_session server 0.0.0.0:39031

# Client on the same machine; use the server's LAN IP on another machine.
build/bin/hx_os_kcp_session client 127.0.0.1:39031
```

On Windows use `build/bin/Release/hx_os_kcp_session.exe` with the same arguments. The server prints `Request: {"type":"status","device_id":7}`; the client prints `Reply: {"accepted":true,"device_id":7}`. The example's reply is an application acknowledgement, distinct from transport queue admission. The library does not alter firewall rules.

An endpoint owns one I/O thread, bounded peers and message queues. Sends copy the message and are safe to call from application threads; receive returns a complete message or a connection event. Release each message once. A timed-out connection can still return a non-NULL session: wait again or close it with a deadline. Reconnection never silently replays a previous request.

The remote session reuses the documented [HXIP session envelope and KCP message protocol](IPC.md#8-hxip-v1-for-peers-in-other-languages). A peer written in another language must implement both. Raw KCP engine packets are not interchangeable with these session packets. There is no encryption or authentication; use it only on a trusted network. Existing `hx_ipc_*` factories remain loopback-only. Use TLS for verified encrypted streams.

## 7. Domain names and platform boundaries

`hx_os_hostname_ascii` converts a UTF-8 DNS name for the raw socket/TLS/DNS entries. It performs no lookup. Non-ASCII conversion requires IDN support in the selected libcurl build; otherwise it returns `NOT_SUPPORTED` and preserves the output. The backend's IDNA rules apply; this is not a confusable-name detector.

Interface indices and routes are snapshots, not stable identities or reachability tests. Requery after an interface-change notification. Android applications own permissions, Network selection and APK lifecycle. Android Network handles are not interface indices. UDP/KCP cannot repair a firewall or virtual network path that persistently drops fragments. Keep datagrams appropriately sized and use business IDs, acknowledgement, deduplication and persistence where your application requires them.
