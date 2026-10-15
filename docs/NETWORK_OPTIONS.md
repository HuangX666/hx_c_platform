# Network options, interface selection and multicast discovery

[简体中文](NETWORK_OPTIONS.zh-CN.md) · [Usage](USAGE.md) · [Runnable examples](../examples/README.md) · [Installed SDK](INSTALL.md)

## 1. Pick the operation you need

These APIs configure ordinary TCP/UDP sockets. Use them before attaching the socket to an I/O loop and while no other thread is using it. They do not change the machine's network settings or start a background thread. Both `HX_OS_ENABLE_NETWORK` and `HX_OS_ENABLE_NET_OPTIONS` must be enabled (the default). Disabling either makes these operations report `HX_OS_NOT_SUPPORTED` and `hx_os_interfaces_available()` return zero. Windows links the system `iphlpapi` library through the exported CMake target; no new third-party dependency is needed.

| Need | C entry point | What the call does |
| --- | --- | --- |
| Detect an idle TCP connection that has disappeared | `hx_os_socket_configure_keepalive` | Enables keepalive and sets the idle/probe intervals in seconds |
| Listen for IPv4 and IPv6 on one IPv6 socket | `hx_os_socket_set_dual_stack` | Explicitly enables or disables IPv4-mapped IPv6 use |
| Send an IPv4 UDP broadcast | `hx_os_socket_set_broadcast` | Permits broadcast transmission; it does not choose a destination |
| Receive a multicast group | `hx_os_socket_join_multicast` / `leave_multicast` | Changes membership on a specified interface |
| Select where multicast leaves the host | `hx_os_socket_set_multicast_interface` | Selects an outgoing interface by its family-specific index |
| Limit multicast propagation / allow local reception | `hx_os_socket_set_multicast_hops` / `set_multicast_loopback` | Configures native TTL/hop-limit and loopback behavior |
| Show available IP interfaces and addresses | `hx_os_interfaces_query` | Returns an owned, immutable snapshot without requiring a network object |

Ordinary socket receive calls still return bytes or one UDP datagram. For complete TCP messages, use [message frames](FRAMES.md); for correlated requests and reconnecting sessions, use [IPC](IPC.md). These options do not add encryption, delivery acknowledgements or business-level deduplication.

## 2. Find an interface before choosing one

Build the two public examples in your existing directory. Replace `build` with that directory. The Windows commands below use Debug; use the configuration matching your installed dependency SDK, changing both `--config` and the executable path together. Linux single-configuration builds keep the configuration already selected in their cache.

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Debug --target hx_os_network_interfaces
cmake --build build --config Debug --target hx_os_multicast_discovery
```

From the checkout root, list interfaces:

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe
```

Linux/WSL:

```sh
./build/bin/hx_os_network_interfaces
```

Example output, with values that depend on your machine:

```text
IP-visible interfaces: 2
eth0 (eth0)
  IPv4 index=2, IPv6 index=2, MTU=1500
  up=1, multicast=1, loopback=0
  192.168.1.20/24 (IPv4, scope=0)
  fe80::1234/64 (IPv6, scope=2)
```

Choose an interface with the address family you need, normally `up=1` and `multicast=1`. Use its **IPv4 index for IPv4**, and **IPv6 index for IPv6**; these may differ on Windows. Do not assume the first entry is the preferred route. The complete C program is [network_interfaces.c](../examples/network_interfaces.c).

For an application UI or device selector, this is the C ownership pattern:

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

C++11 provides the same snapshot with automatic destruction:

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

Names and addresses are borrowed from the snapshot and remain valid until its destruction. Names are UTF-8; Windows exposes a stable-looking adapter name and a friendly display name, while POSIX normally uses the interface name for both. Neither an interface index nor its current address is a permanent device ID. Query again after a network change; this is not a monitoring subscription.

Only interfaces with IP addresses visible to the process are listed, not every physical adapter. `UP` reflects native state, not Internet reachability. MTU/index `0` means unavailable, and prefix length `255` means unknown. An empty snapshot is valid. Sandboxes or Android access restrictions can make enumeration fail; `available()` reports build support, not permission. No MAC address, route ranking or Android network binding is implied.

## 3. Configure TCP Keepalive in one call

For an already created TCP socket, the following helper enables probes after 30 seconds of inactivity, then requests 5-second probe intervals. The probe count remains the operating system's current value. Check the returned status before continuing setup:

```c
hx_os_status configure_device_connection(hx_os_socket *socket,
	hx_os_error *error)
{
	return hx_os_socket_configure_keepalive(socket, 30, 5, 0, error);
}
```

Use a positive final argument, such as `3`, if your application requires exactly three failed probes. Unsupported settings return `HX_OS_NOT_SUPPORTED`; they are not silently ignored. Windows 7/8.1 supports configuring the durations through this API but not an explicit probe count, so `0` is useful for that baseline. Durations must be positive; native upper limits still apply.

C++11 keeps the same behavior and borrows the socket without owning it:

```cpp
#include <hx/SocketOptions.hpp>

hx::Result<void> configureDeviceConnection(hx_os_socket *socket)
{
	return hx::SocketOptions(socket).keepAlive(30, 5);
}
```

The caller checks `result.ok()` and can read `result.error()` for native details. To disable keepalive again, call `hx_os_socket_set_keepalive(socket, 0, error)` or `hx::SocketOptions(socket).keepAlive(false)`.

Keepalive detects transport failure; it does not prove the device application is responsive or that a command was processed. It also does not guarantee a disconnect at exactly `idle + interval * count`. The configuration uses several native operations: a later failure can leave earlier settings changed. Do not assume rollback; stop setup, inspect the error, then reconfigure or close the socket.

## 4. Enable an IPv4/IPv6 listener explicitly

Create a socket using `HX_OS_IPV6` and `HX_OS_TCP`. Pass that still-unbound socket to this helper, then use the normal accept interface. Bind to `::`, not `::1`, when accepting both address families:

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

The caller keeps ownership and closes the socket on setup failure. `enabled=0` requests IPv6 only. This must happen before bind/connect, and does not alter global IPv6 policy or create an extra socket. C++ uses `hx::SocketOptions(listener).dualStack()` or `.dualStack(false)` and returns `Result<void>`.

An IPv4 peer accepted through the dual-stack socket remains represented as IPv6, for example `::ffff:192.168.1.20`. Do not compare its family directly with `HX_OS_IPV4` and discard it. Outgoing IPv4 destinations used with this IPv6 socket must also be IPv4-mapped IPv6 addresses. If IPv6 is disabled on the machine, report the error or explicitly create separate sockets according to your application's policy.

## 5. Broadcast and multicast are different operations

**Broadcast** sends an IPv4 UDP datagram to a broadcast destination. Enable `hx_os_socket_set_broadcast(socket, 1, error)`, then send to an explicitly chosen address such as `255.255.255.255` using `hx_os_socket_send_to`. A successful option call does not select a destination, bind a port or establish delivery. IPv6 has multicast instead of broadcast; passing an IPv6/TCP socket is an invalid use of this convenience API.

**Multicast** sends to a group. A receiver joins the group on an interface; a sender independently chooses its outgoing interface. Joining does not bind the UDP port, and setting the outgoing interface does not join the group.

| Step | Receiver | Sender |
| --- | --- | --- |
| Create | IPv4/IPv6 UDP socket matching the group | Same family as the group |
| Configure loopback | Enable for same-machine demonstrations | Enable for same-machine demonstrations |
| Select outgoing interface | Only needed if this socket also sends | `set_multicast_interface(socket, index, error)` |
| Bind | Wildcard address and the discovery port | Optional source binding |
| Membership | `join_multicast(socket, &group, index, error)` | Not required just to send |
| Traffic | `receive_from`; on WOULD_BLOCK, wait and retry | `send_to`; on WOULD_BLOCK, wait and retry |
| Finish | Leave the same group/interface, then close | Close |

The APIs implement any-source multicast, not source-specific multicast filters. Membership uses the group address; `group.port` is ignored by join/leave, while bind/send use the actual UDP port. Index `0` delegates interface choice to the OS. For IPv6, a nonzero `group.scope_id` supplies a missing interface index; conflicting explicit values fail. For link-local groups, choose an explicit IPv6 interface index.

Multicast hop limit accepts `0..255`; `1` limits forwarding to the local link, while `0` keeps traffic on the host. This is a limit, not a delivery guarantee. On Windows IPv4, the loopback option affects local reception; on POSIX it affects sending. Enable it on both endpoints in portable same-machine examples. The option is not a packet filter or a substitute for membership.

There is no automatic `SO_REUSEADDR`. The example uses one listener and does not need sharing. If your application deliberately needs multiple listeners on the same port, explicitly call `hx_os_socket_set_reuseaddr` before bind and account for the native platform's port-sharing behavior. Joining twice is not normalized into success; native membership errors are retained.

## 6. Run a JSON multicast discovery message

[multicast_discovery.c](../examples/multicast_discovery.c) is a standalone two-mode program. Start the listener first and send from a second terminal within ten seconds. Each process owns its socket, handles WOULD_BLOCK with the same finite deadline, and closes its resources. The listener receives one datagram of at most 1024 bytes; it does not wait forever or process truncated data.

First run the interface example. Replace `12` below with its IPv4 index. Both processes on the same machine should use the same interface. On separate machines, each uses its own index on the same reachable multicast network.

Windows, terminal 1:

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe listen 239.255.42.99 12
```

Windows, terminal 2:

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe send 239.255.42.99 12
```

Linux/WSL uses its own interface index, here illustrated as `2`:

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

Expected listener output, with the sender address and ephemeral port varying:

```text
Listening for one discovery datagram (10 seconds)...
Received from 192.168.1.20 port 54321: {"type":"discover","request_id":"demo-1"}
```

The payload is fixed valid JSON, transmitted without a trailing NUL. The receiver displays the datagram bytes; it does not validate arbitrary peers' JSON or implement a discovery reply. Use the [serialization guide](SERIALIZATION.md) when adding your own fields and decoding received messages. A sender's success means local send acceptance, not an acknowledgement from a listener.

For IPv6, replace the group with `ff02::114` and the index with the interface's **IPv6** index. The program sets the destination scope explicitly. Both modes accept an optional final port, for example `send 239.255.42.99 12 39022`; both sides must agree. Group addresses here are demonstration values, not a registration for your application's protocol.

If reception times out, first check the chosen family/index, the listener startup window, interface multicast support and the network's multicast policy. A VPN, WSL virtual network, Wi-Fi access point or host firewall can limit delivery even after successful socket setup. The program reports the failure and does not change those system settings. A timeout does not establish that the API implementation is broken, or that the packet reached the other host.

## 7. C++ setup and ownership

The C++ facade exposes the same calls. For an existing UDP socket and a parsed group, a helper can stop at the first error while leaving socket ownership with its caller:

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

Bind the receiver as shown in section 5 before using it. Use `options.leaveMulticast(group, index)` when ending membership; closing the socket also releases memberships. `SocketOptions` never closes its borrowed socket. All setters require exclusive access and return `HX_OS_BUSY` after loop attachment, even when the loop is idle. Configure first, then hand the socket to its I/O owner. Snapshot readers can run concurrently, but snapshot destruction must wait for readers to stop.
