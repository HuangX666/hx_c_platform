# Network Options, Interface Selection and Multicast Discovery

[简体中文](NETWORK_OPTIONS.zh-CN.md) · [Usage](USAGE.md) · [Runnable Examples](../examples/README.md) · [Installed SDK](INSTALL.md)

When building LAN device discovery, industrial sensor multicast subscribers, long-lived TCP connections with keepalive probes, or dual-stack IPv4/IPv6 servers, you often need fine-grained socket options alongside accurate host network interface inspection, route selection, live network-change notifications, and cancellable DNS resolution. This guide covers the network options, interface enumeration, routing, and `c-ares` DNS APIs in `hx_c_platform`.

## 1. Pick the operation you need

The socket configuration APIs below operate on ordinary TCP and UDP sockets. **Call them before attaching the socket to an `hx_os_loop` event loop and while no other thread is accessing the socket.** None of these functions alter global OS network settings or spawn background threads.

> [!NOTE]
> **Build Switches and System Dependencies**
> Both `HX_OS_ENABLE_NETWORK` and `HX_OS_ENABLE_NET_OPTIONS` must be enabled (`ON` by default). Disabling either switch causes these functions to return `HX_OS_NOT_SUPPORTED` and `hx_os_interfaces_available()` to return `0`. On Windows, the exported CMake target automatically links the system `iphlpapi` library with no extra third-party dependency.

| Need | C Entry Point | What the Call Does |
| --- | --- | --- |
| Detect a silently dropped idle TCP connection | `hx_os_socket_configure_keepalive` | Enables TCP keepalive and sets the idle time and probe interval in seconds |
| Accept both IPv4 and IPv6 clients on one IPv6 socket | `hx_os_socket_set_dual_stack` | Explicitly enables or disables IPv4-mapped IPv6 (`IPV6_V6ONLY`) |
| Send an IPv4 UDP broadcast packet | `hx_os_socket_set_broadcast` | Grants broadcast permission (`SO_BROADCAST`); you still pass the destination to `send_to` |
| Subscribe to or leave a UDP multicast group | `hx_os_socket_join_multicast` / `leave_multicast` | Joins or leaves an any-source multicast (ASM) group on a specific interface |
| Choose which network interface sends multicast traffic | `hx_os_socket_set_multicast_interface` | Selects the outgoing interface using its family-specific index |
| Limit multicast router hops / control local loopback | `hx_os_socket_set_multicast_hops` / `set_multicast_loopback` | Configures native multicast TTL/hop-limit and local loopback delivery |
| Enumerate visible network interfaces and IP addresses | `hx_os_interfaces_query` | Returns a caller-owned, immutable snapshot without requiring an `hx_os_network` handle |

Ordinary socket receive calls still return raw stream bytes or single UDP datagrams. Use [Message Frames (`hx_frame`)](FRAMES.md) when you need discrete message boundaries over TCP, or [Local IPC (`hx_ipc`)](IPC.md) when you need request/reply correlation and automatic session reconnection.

## 2. Find an interface before choosing one

Reuse your existing CMake build directory (`build`) to compile the interface enumeration and multicast discovery examples. The Windows commands below use `Debug`—match the configuration of your installed dependency SDK and adjust both `--config` and the output binary path accordingly:

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Debug --target hx_os_network_interfaces
cmake --build build --config Debug --target hx_os_multicast_discovery
```

From the repository root, run the interface enumeration tool:

**Windows PowerShell:**

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe
```

**Linux / WSL:**

```sh
./build/bin/hx_os_network_interfaces
```

The program lists all IP-bearing interfaces visible to the current process along with their family indices, MTU, flags, and assigned addresses:

```text
IP-visible interfaces: 2
eth0 (eth0)
  IPv4 index=2, IPv6 index=2, MTU=1500
  up=1, multicast=1, loopback=0
  192.168.1.20/24 (IPv4, scope=0)
  fe80::1234/64 (IPv6, scope=2)
```

When selecting an interface for multicast, pick one that has an address in your target family and reports `up=1` and `multicast=1`. **Always pass `ipv4_index` for IPv4 operations and `ipv6_index` for IPv6 operations**—on Windows, those two indices can differ for the same adapter—and never assume the first entry in the list is the system's default route. Full source code is in [network_interfaces.c](../examples/network_interfaces.c).

### Querying Interfaces in C and C++11

To populate an interface selector in C:

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_interface_list *interfaces = NULL;
	hx_os_status status =
		hx_os_interfaces_query(NULL, &interfaces, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Interfaces: %s\n", hx_os_status_name(status));
		return 1;
	}

	for (size_t i = 0; i < hx_os_interface_list_count(interfaces); ++i) {
		const hx_os_interface_info *info =
			hx_os_interface_list_get(interfaces, i);
		printf("%s: IPv4 index %u\n", info->display_name,
			(unsigned int)info->ipv4_index);
	}

	hx_os_interface_list_destroy(interfaces);

	return 0;
}
```

In C++11, `<hx/NetworkInterfaces.hpp>` wraps the snapshot in an RAII container:

```cpp
#include <hx/NetworkInterfaces.hpp>
#include <cstdio>

int main()
{
	auto result = hx::NetworkInterfaces::query();
	if (!result.ok()) {
		std::fprintf(stderr, "Interfaces: %s\n",
			hx_os_status_name(result.status()));
		return 1;
	}

	const auto &interfaces = result.value();
	for (size_t i = 0; i < interfaces.size(); ++i)
		std::puts(interfaces.get(i)->display_name);

	return 0;
}
```

> [!NOTE]
> **Snapshot Lifetime and Field Semantics**
> - **Borrowed Pointers**: Interface names (UTF-8) and address arrays belong to the `hx_os_interface_list` snapshot and remain valid until `hx_os_interface_list_destroy()`. Windows populates both a stable adapter `name` and a friendly `display_name`; POSIX typically uses the interface name (e.g., `eth0`) for both.
> - **What Enumeration Covers**: Only interfaces with IP addresses visible to the current process are listed (an empty list is a valid result). `up=1` reflects link/protocol state rather than internet reachability; an MTU or index of `0` means unavailable, and a prefix length of `255` means unknown. MAC addresses, route metrics, and Android `Network` bindings are not part of this snapshot.

## 3. Configure TCP Keepalive in one call

For an existing TCP socket, `hx_os_socket_configure_keepalive()` enables keepalive probes and sets the idle delay and probe interval in a single call. The helper below starts probing after 30 seconds of inactivity, repeats probes every 5 seconds, and leaves the probe retry count at the OS default (`0`):

```c
hx_os_status configure_device_connection(hx_os_socket *socket,
	hx_os_error *error)
{
	return hx_os_socket_configure_keepalive(socket, 30, 5, 0, error);
}
```

Pass a positive fourth argument (such as `3`) if your application requires an explicit probe count. Note that Windows 7 and 8.1 support configuring the idle and interval durations through this API, but do not support per-socket probe counts—pass `0` when targeting that baseline (unsupported settings return `HX_OS_NOT_SUPPORTED` rather than silently succeeding). Both durations must be positive integers.

The C++11 wrapper borrows the `hx_os_socket *` without taking ownership:

```cpp
#include <hx/SocketOptions.hpp>

hx::Result<void> configureDeviceConnection(hx_os_socket *socket)
{
	return hx::SocketOptions(socket).keepAlive(30, 5);
}
```

To turn keepalive off, call `hx_os_socket_set_keepalive(socket, 0, error)` or `hx::SocketOptions(socket).keepAlive(false)`.

> [!IMPORTANT]
> TCP keepalive detects dead transport paths, **not whether the remote application thread is hung or whether a business command completed**. Because configuring keepalive issues multiple native socket calls sequentially, a failure on a later step may leave earlier steps applied without automatic rollback—stop setup on error and either reconfigure or close the socket.

## 4. Enable an IPv4/IPv6 listener explicitly

Because operating systems differ on whether an IPv6 socket accepts IPv4 connections by default (`IPV6_V6ONLY`), portable dual-stack servers should create an `HX_OS_IPV6` + `HX_OS_TCP` socket, call `hx_os_socket_set_dual_stack(listener, 1, error)` before `bind`, and bind to the IPv6 wildcard address `::` (not loopback-only `::1`):

```c
hx_os_status listen_on_both_families(hx_os_socket *listener,
	uint16_t port, hx_os_error *error)
{
	hx_os_address any;
	hx_os_status status =
		hx_os_socket_set_dual_stack(listener, 1, error);
	if (status == HX_OS_OK)
		status = hx_os_address_parse("::", port, &any, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_bind(listener, &any, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_listen(listener, 16, error);

	return status;
}
```

Passing `enabled = 0` restricts the socket to pure IPv6 traffic. In C++11, call `hx::SocketOptions(listener).dualStack()` or `.dualStack(false)`.

> [!TIP]
> **Handling IPv4 Clients Accepted on a Dual-Stack Socket**
> When an IPv4 client connects to a dual-stack IPv6 listener, `accept` reports the peer address with family `HX_OS_IPV6` as an **IPv4-mapped IPv6 address** (for example, `::ffff:192.168.1.20`). Do not reject the peer just because `family != HX_OS_IPV4`—use `hx_os_address_is_ipv4_mapped()` and `hx_os_address_unmap_ipv4()` from Section 8 to inspect or convert it.

## 5. Broadcast and multicast are different operations

- **IPv4 Broadcast**: Call `hx_os_socket_set_broadcast(socket, 1, error)` to enable broadcast permission, then pass an explicit broadcast address (such as `255.255.255.255` or a subnet broadcast address) to `hx_os_socket_send_to()`. Because IPv6 replaces broadcast with multicast, passing an IPv6 or TCP socket to `set_broadcast` returns an error.
- **UDP Multicast**: Multicast sends datagrams to a group address (e.g., `239.255.42.99` or `ff02::114`). A receiver **joins the group** on a specific interface and binds the UDP port; a sender independently **selects its outgoing interface**. Joining a group does not bind a port, and setting an outgoing interface does not join the group:

| Step | Multicast Receiver (Listener) | Multicast Sender |
| --- | --- | --- |
| 1. Create socket | Create an IPv4 or IPv6 UDP socket matching the group family | Create a UDP socket matching the group family |
| 2. Loopback | Enable `set_multicast_loopback(1)` for same-host demos | Enable `set_multicast_loopback(1)` for same-host demos |
| 3. Outgoing interface | Needed only if this socket also transmits multicast | Call `hx_os_socket_set_multicast_interface(socket, index, error)` |
| 4. Bind | Bind wildcard address (`0.0.0.0` or `::`) and the discovery port | Optional source bind |
| 5. Group membership | Call `hx_os_socket_join_multicast(socket, &group, index, error)` | Not required just to transmit |
| 6. Send / Receive | Call `receive_from` (on `HX_OS_WOULD_BLOCK`, wait and retry) | Call `send_to` (on `HX_OS_WOULD_BLOCK`, wait and retry) |
| 7. Cleanup | Call `leave_multicast` with the same group/index, then close | Close socket |

### Multicast Configuration Details

1. **Interface Index & IPv6 Scope**: `join_multicast` and `leave_multicast` use only the IP address in `group` and ignore `group.port` (the UDP port is determined by `bind` and `send_to`). Passing index `0` lets the OS pick a default interface; for IPv6 link-local groups (`ff02::...`), always specify an explicit IPv6 interface index (if `index` is `0` and `group.scope_id` is non-zero, `scope_id` is used; conflicting non-zero values return an error).
2. **Hop Limit & Cross-Platform Loopback**: `hx_os_socket_set_multicast_hops()` accepts `0..255` (`1` restricts packets to the local link; `0` restricts them to the local host). On Windows IPv4, `hx_os_socket_set_multicast_loopback()` applies to the **receiving** socket, whereas on POSIX it applies to the **sending** socket—enable it on both ends in portable same-machine tests.
3. **Port Sharing (`SO_REUSEADDR`)**: The library never enables `SO_REUSEADDR` behind your back. If your protocol requires multiple listeners on the same host to share one multicast UDP port, call `hx_os_socket_set_reuseaddr()` explicitly before `bind`.

## 6. Run a JSON multicast discovery message

[multicast_discovery.c](../examples/multicast_discovery.c) is a standalone two-mode (`listen` / `send`) program. First run `hx_os_network_interfaces` from Section 2 and replace the interface index below (`12` on Windows or `2` on Linux) with your active adapter's **IPv4 index**.

**Windows PowerShell (Terminal 1 — start listener for 10 seconds):**

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe listen 239.255.42.99 12
```

**Windows PowerShell (Terminal 2 — send discovery packet within 10 seconds):**

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe send 239.255.42.99 12
```

**Linux / WSL (illustrated with interface index `2`):**

```sh
# Terminal 1
./build/bin/hx_os_multicast_discovery listen 239.255.42.99 2
# Terminal 2, within ten seconds
./build/bin/hx_os_multicast_discovery send 239.255.42.99 2
```

Expected sender output:

```text
Sent 41 bytes: {"type":"discover","request_id":"demo-1"}
```

Expected listener output (sender IP and ephemeral port will match your machine):

```text
Listening for one discovery datagram (10 seconds)...
Received from 192.168.1.20 port 54321: {"type":"discover","request_id":"demo-1"}
```

For **IPv6 multicast discovery**, replace the group address with `ff02::114` and pass the adapter's **IPv6 index** (both modes also accept an optional trailing port number such as `send 239.255.42.99 12 39022` as long as both peers match). See the [Serialization Guide](SERIALIZATION.md) when adding custom JSON fields and parsing responses.

> [!TIP]
> **Troubleshooting Multicast Timeouts**
> If the listener times out after 10 seconds, verify that: 1) you passed the matching family's interface index (`ipv4_index` vs. `ipv6_index`), 2) the sender ran within the 10-second listening window, 3) the adapter shows `up=1, multicast=1`, and 4) no VPN tunnel, WSL virtual switch, Wi-Fi client isolation policy, or host firewall is dropping multicast UDP traffic.

## 7. C++ setup and ownership

In C++11, `hx::SocketOptions` configures a borrowed `hx_os_socket *` without taking ownership:

```cpp
#include <hx/SocketOptions.hpp>

hx::Result<void> configureMulticast(hx_os_socket *socket,
	const hx_os_address &group, uint32_t index)
{
	hx::SocketOptions options(socket);
	auto result = options.multicastInterface(index);
	if (!result.ok())
		return result;

	result = options.multicastHops(1);
	if (!result.ok())
		return result;

	result = options.multicastLoopback();
	if (!result.ok())
		return result;

	return options.joinMulticast(group, index);
}
```

Bind the receiver socket as shown in Section 5 before receiving. Call `options.leaveMulticast(group, index)` when done (closing the socket also releases group memberships automatically). Remember that all option setters require exclusive access and return `HX_OS_BUSY` once a socket is attached to an `hx_os_loop`, even when the loop is idle—configure options first, then hand the socket to the loop.

## 8. Parse an endpoint without losing its IPv6 scope

Use `hx_os_endpoint_parse()` when a configuration string contains both a numeric IP address and a port (including optional IPv6 zone/scope IDs):
- IPv4 format: `192.168.1.20:9000`
- Scoped IPv6 format: `[fe80::1%7]:9000` (where `%7` selects interface index `7`; native interface names like `%eth0` are also accepted).

```c
hx_os_address destination;
hx_os_error error = {0};
char text[96];

hx_os_status status = hx_os_endpoint_parse(
	"[fe80::1%7]:9000", &destination, &error);
if (status == HX_OS_OK)
	status = hx_os_endpoint_format(
		&destination, text, sizeof(text), &error);
if (status == HX_OS_OK)
	puts(text);
```

This prints `[fe80::1%7]:9000` (`hx_os_endpoint_format()` always formats the zone as a numeric index). `hx_os_endpoint_parse()` is a strict numeric endpoint parser (up to 255 bytes): it does not perform DNS lookups or decode URI percent-encoding, IPv6 addresses require brackets `[...]`, and the port is mandatory.

For dual-stack address inspection and conversion, use these in-memory helpers:
- `hx_os_address_is_ipv4_mapped(&addr)`: Detects `::ffff:a.b.c.d` addresses.
- `hx_os_address_unmap_ipv4(&addr)` / `hx_os_address_map_ipv4(&addr)`: Converts between IPv4 and IPv4-mapped IPv6 in place while preserving the port.
- `hx_os_address_equal(&a, &b, 1)`: Compares addresses and ports, treating unscoped IPv4-mapped IPv6 addresses as equal to their IPv4 counterparts when the third argument is `1` (pass `0` to require identical families).

## 9. Find the route for this destination

Instead of guessing the first `up=1` adapter from the interface list, call `hx_os_route_query(network, &destination, &route, &error)` to ask the OS routing table which local source IP and interface index will be used to reach `destination` (without transmitting any application packet on the wire).

You can test route queries directly with the `--route` flag on `hx_os_network_interfaces`:

**Windows PowerShell:**

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe --route 127.0.0.1:9000
```

**Linux / WSL:**

```sh
./build/bin/hx_os_network_interfaces --route 127.0.0.1:9000
```

Example output: `Destination 127.0.0.1:9000 uses 127.0.0.1:0 on interface 1` (substitute a real LAN device's IP and port, or quote an IPv6 endpoint such as `"[fe80::1%7]:9000"`). In C++11, call `hx::NetworkInterfaces::routeTo(network, destination)`, which returns `hx::Result<hx_os_route_info>`.

> [!NOTE]
> Windows uses the native OS route lookup API. POSIX determines the kernel-selected source IP via a temporary UDP route association and matches it against the interface snapshot (returning `HX_OS_NOT_SUPPORTED` if the same source IP is assigned to multiple interfaces rather than guessing).

## 10. Refresh the list after a network change

When cables are unplugged, Wi-Fi networks switch, or VPNs connect, previously cached interface snapshots become stale. Run `hx_os_network_interfaces` with `--watch` to observe live change notifications:

```sh
./build/bin/hx_os_network_interfaces --watch
```

(On Windows, append `--watch` to the `.exe` command.) The program subscribes to OS network notifications, prints the initial snapshot, and waits up to 30 seconds: if a change occurs, it prints `Network changed; querying a fresh snapshot:` followed by the updated list; otherwise it prints `No interface changes within 30 seconds.` and exits cleanly.

In your own application, follow this sequence:
1. Create an `hx_os_interface_watch` handle.
2. Query your initial snapshot with `hx_os_interfaces_query()`.
3. Call `hx_os_interface_watch_wait_until()` with a monotonic deadline (pass `HX_OS_DEADLINE_NOW` to poll without blocking).
4. When `wait_until` returns `HX_OS_OK`, call `hx_os_interfaces_query()` to obtain a fresh snapshot.
5. Once the waiting thread has returned, close the subscription with `hx_os_interface_watch_close(&watch, error)` (always close a non-NULL watch handle even if creation failed during rollback).

> [!NOTE]
> **Coalesced Rescan Hints and Platform Backends**
> - Change watches use native OS mechanisms (signaled callbacks on Windows, Netlink on Linux/Android, and routing sockets on macOS) with **zero background polling threads and no unbounded event queues**.
> - Notifications act as **coalesced rescan hints**, not an ordered event log—multiple rapid changes merge into one wakeup, and routing or non-IP changes can also trigger a wakeup where the resulting IP snapshot appears unchanged. If Android sandbox policies block native Netlink subscriptions inside an unprivileged APK, forward platform `ConnectivityManager` callbacks from your host activity to trigger `hx_os_interfaces_query()`.

## 11. Tune socket buffers and restrict multicast sources

- **Tuning Kernel Send/Receive Buffers**:
  - Call `hx_os_socket_set_buffers(socket, 65536, 65536, error)` to request 64 KiB kernel send and receive buffers (passing `0` for either direction leaves that buffer unchanged).
  - Call `hx_os_socket_get_buffers()` to inspect the actual capacities granted by the OS (which may be clamped, rounded, or doubled on Linux).
  - In C++11, use `hx::SocketOptions(socket).buffers(65536, 65536)` to set capacities and `.buffers()` to read back `Result<SocketOptions::BufferSizes>` (`sendBytes` and `receiveBytes`). Note that these control OS transport buffers, not the application-level queue limits of `hx_frame`, `hx_ipc`, or `hx_tls`.
- **Source-Specific Multicast (SSM)**:
  - To receive multicast traffic only from a specific unicast sender IP, call `hx_os_socket_join_multicast_source(socket, &group, &source, index, error)` and leave with `hx_os_socket_leave_multicast_source()` using the same arguments (in C++11, `.joinMulticastSource(...)` and `.leaveMulticastSource(...)`).
  - For example, group `232.1.2.3` and source `192.168.1.20` filter for that sender on the chosen IPv4 interface. If the OS does not support source-specific multicast filtering, the call returns `HX_OS_NOT_SUPPORTED` rather than silently falling back to any-source reception.

## 12. Connect by name and select cancellable DNS

Call `hx_os_tcp_dial_until(network, host, port, NULL, deadline, &socket, error)` when you want the library to resolve a hostname, stagger IPv4 and IPv6 connection attempts (Happy Eyeballs), keep the first socket that connects, and close the remaining candidates automatically (`hx::TcpDial(owner).connect(...)` in C++11).
- **Staggered Candidate Pacing**: IPv4 and IPv6 candidates alternate with a default 250 ms stagger and at most two concurrent connection attempts in flight. While untried candidate addresses remain, a stalled attempt holds its slot for at most 2 seconds (or a proportional share of a shorter overall deadline) so unresponsive addresses cannot starve later candidates. Once all candidates have started, surviving attempts wait until the overall deadline.
- **System DNS vs. Cancellable `c-ares` Resolver**: Passing `NULL` options uses the OS resolver, whose blocking lookup cannot be cancelled mid-flight (if you already have resolved addresses, call `hx_os_tcp_dial_addresses_until()` directly). When **DNS network waits must also respect deadlines and cancellation tokens**, create an `hx_os_resolver` and pass it via `hx_os_dial_options.resolver`. All TLS client connection APIs (`hx_tls_connect*` and `hx::Tls::connect()`) share this exact dialer; see the [TLS Client Guide](TLS.md#2-direct-c-and-c-calls).

### 12.1 Run the DNS example

The optional `HX_OS_ENABLE_CARES` module uses the pinned `c-ares` library prepared via the [dependency setup](DEPENDENCIES.md) (when disabled, `hx_os_resolver_available()` returns `0` and resolver creation returns `HX_OS_NOT_SUPPORTED`). Build the example in your existing build directory:

```sh
cmake --build build --config Release --target hx_os_dns_lookup
```

**Windows PowerShell:**

```powershell
.\build\bin\Release\hx_os_dns_lookup.exe 127.0.0.1
```

**Linux / WSL:**

```sh
./build/bin/hx_os_dns_lookup 127.0.0.1
```

Both commands print `IPv4 127.0.0.1 port=443` without requiring internet access. Replace `127.0.0.1` with a hostname to print its current IPv4/IPv6 records, or pass an optional second argument (such as `127.0.0.1:5353`) to query a specific numeric DNS server.

### 12.2 Own one resolver and reuse it

```c
hx_os_resolver *resolver = NULL;
hx_os_address_list *addresses = NULL;
hx_os_error error;

hx_os_status status = hx_os_resolver_create(NULL, NULL, &resolver, &error);
if (status == HX_OS_OK)
	status = hx_os_resolver_resolve_until(resolver, "localhost", 443,
		HX_OS_TCP, hx_os_deadline_after_ms(2000), NULL,
		&addresses, &error);

/* Check status, then inspect addresses with the address-list getters. */
hx_os_address_list_destroy(addresses);
if (resolver)
	(void)hx_os_resolver_destroy(&resolver, NULL);
```

- **Single Active Query & Safe Cancellation**: `hx_os_resolver_resolve_until()` drives `c-ares` non-blocking sockets on the calling thread without spawning background threads. Each resolver instance allows one active query at a time: concurrent calls return `HX_OS_BUSY` and same-thread re-entry returns `HX_OS_DEADLOCK`. Passing an `hx_os_cancel_token` allows another thread to cancel the lookup (checked within 50 ms during network waits), and all native callbacks retire before the function returns so timeouts never leave dangling references to caller buffers. The returned `hx_os_address_list` owns its memory independently and can outlive the resolver.
- **Bounds & Resolution Scope**: Lookups return up to 256 unique addresses, track at most 64 native sockets, and disable unbounded internal caching. By default, queries consult both the local `hosts` file and DNS (`use_hosts_file = 0` queries DNS only). Recreate the resolver if the host's DNS server configuration changes.
- **C++11 Wrapper**: Include `<hx/Resolver.hpp>` and use `hx::Resolver(owner).create()` and `.resolve(host, port)` (which returns an RAII `Result<hx::Addresses>`; call `.close()` explicitly to destroy the resolver).

### 12.3 Use an event loop or coroutine

When your thread needs to keep processing other I/O events while a DNS lookup is in flight, call `hx_os_loop_resolve()` directly on an `hx_os_loop` (no separate `hx_os_resolver` handle is required):

```c
hx_os_request *request = NULL;
hx_os_status status = hx_os_loop_resolve(loop, "localhost", 443,
	HX_OS_TCP, NULL, hx_os_deadline_after_ms(2000),
	resolved, &state, &request, &error);
```

Drive the loop with `hx_os_loop_run_until()` or `poll_until()`. Your completion callback (`resolved`) runs once on the polling thread; call `hx_os_request_take_addresses()` inside the callback to take ownership of the address list and destroy it when finished (see [dns_lookup.c](../examples/dns_lookup.c)). Another thread may call `hx_os_request_cancel(request)`, but you must keep polling until the completion callback returns before freeing `state`.

Inside a coroutine, call `hx_os_coro_resolve_cancellable(host, port, options, deadline, &addresses, &error)` to suspend the current coroutine while waiting for DNS (stackful coroutines call it directly; C state-machine coroutines use `HX_OS_CORO_AWAIT`):

```sh
./build/bin/hx_os_dns_lookup --async 127.0.0.1
./build/bin/hx_os_dns_lookup --coro 127.0.0.1
```

(On Windows, run `build/bin/Release/hx_os_dns_lookup.exe`.) In C++, use `hx::IoLoop::resolve(...)` for event loops, `hx::coro::Addresses::resolveCancellable(...)` for C++11 coroutines, or `co_await hx::coro::async::resolveCancellable(...)` for C++20 coroutines.
