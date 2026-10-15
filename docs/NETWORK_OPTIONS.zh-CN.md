# 网络参数、网卡选择与组播发现

[English](NETWORK_OPTIONS.md) · [使用教程](USAGE.zh-CN.md) · [可运行示例](../examples/README.zh-CN.md) · [安装包接入](INSTALL.zh-CN.md)

## 1. 根据要做的事情选择接口

这些接口用于配置普通 TCP／UDP socket。应当在 socket 交给 I/O loop 之前调用，并保证没有其他线程同时使用它们。调用不会修改系统网络设置，也不会启动后台线程。`HX_OS_ENABLE_NETWORK` 和 `HX_OS_ENABLE_NET_OPTIONS` 均需开启，默认已开启。关闭任一项时，这些操作返回 `HX_OS_NOT_SUPPORTED`，`hx_os_interfaces_available()` 返回零。Windows 通过导出的 CMake 目标链接系统库 `iphlpapi`，不增加第三方依赖。

| 你要做的事情 | C 接口 | 调用完成了什么 |
| --- | --- | --- |
| 发现长期空闲的 TCP 连接已经断掉 | `hx_os_socket_configure_keepalive` | 开启保活，按秒设置空闲时间和探测间隔 |
| 一个 IPv6 socket 同时接收 IPv4 和 IPv6 连接 | `hx_os_socket_set_dual_stack` | 明确开启或关闭 IPv4 映射的 IPv6 通信 |
| 发送 IPv4 UDP 广播 | `hx_os_socket_set_broadcast` | 允许发送广播，但不会替你选择目的地址 |
| 接收某个组播组 | `hx_os_socket_join_multicast` / `leave_multicast` | 在指定网卡上加入／离开组播组 |
| 指定组播从哪张网卡发出 | `hx_os_socket_set_multicast_interface` | 使用对应地址族的网卡索引选择发送接口 |
| 限制组播传播范围／允许本机接收 | `hx_os_socket_set_multicast_hops` / `set_multicast_loopback` | 设置原生 TTL／跳数限制和本机回环行为 |
| 查看网卡和 IP 地址 | `hx_os_interfaces_query` | 返回由调用者持有的只读快照，不需要先创建网络对象 |

普通 socket 接收仍然返回字节或一份 UDP 数据报。TCP 需要完整消息时使用[消息帧](FRAMES.zh-CN.md)，需要请求对应和重连会话时使用 [IPC](IPC.zh-CN.md)。这里的参数不会自动增加加密、送达确认或业务去重。

## 2. 先看看本机有哪些网卡

复用已有构建目录，构建两个公开示例。把 `build` 换成自己的目录。以下 Windows 命令使用 Debug，请选择与依赖 SDK 匹配的配置，并同时修改 `--config` 和可执行文件路径。Linux 单配置构建继续使用目录缓存中原有的配置。

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Debug --target hx_os_network_interfaces
cmake --build build --config Debug --target hx_os_multicast_discovery
```

在项目根目录运行网卡查询：

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe
```

Linux／WSL：

```sh
./build/bin/hx_os_network_interfaces
```

输出大致如下，具体名称、索引和地址以自己的电脑为准：

```text
IP-visible interfaces: 2
eth0 (eth0)
  IPv4 index=2, IPv6 index=2, MTU=1500
  up=1, multicast=1, loopback=0
  192.168.1.20/24 (IPv4, scope=0)
  fe80::1234/64 (IPv6, scope=2)
```

选择具有所需地址族的网卡，通常还应满足 `up=1`、`multicast=1`。**IPv4 使用 IPv4 index，IPv6 使用 IPv6 index**，Windows 上这两个数值可能不同。不能默认第一项就是首选路由。完整 C 程序见 [network_interfaces.c](../examples/network_interfaces.c)。

例如，给应用做一个网卡选择列表，可以这样查询、读取和释放：

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

C++11 可以取得相同快照，并在对象离开作用域时自动释放：

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

名称和地址都由快照持有，快照释放后不能再使用这些指针。名称采用 UTF-8；Windows 提供适配器名称和便于显示的友好名称，POSIX 通常都使用网卡名。网卡索引和当前 IP 都不是永久设备标识。网络变化后应重新查询，这个接口不会持续推送变化。

查询只列出当前进程可见、具有 IP 地址的接口，不保证包含每一块物理网卡。`UP` 反映原生状态，不表示可以访问互联网。MTU／索引为 `0` 表示无法取得，前缀长度 `255` 表示未知，空列表也是有效结果。沙箱或 Android 权限限制可能让查询失败；`available()` 表示构建支持，不表示已有权限。接口不会同时返回 MAC 地址、路由优先级，也不负责 Android 指定网络绑定。

## 3. 一次调用设置 TCP 保活参数

对已经创建好的 TCP socket，下面的函数开启保活：空闲 30 秒后开始探测，后续探测间隔请求为 5 秒，探测次数保持操作系统当前值。调用者检查返回状态，成功后再继续连接配置：

```c
hx_os_status configure_device_connection(hx_os_socket *socket,
	hx_os_error *error)
{
	return hx_os_socket_configure_keepalive(socket, 30, 5, 0, error);
}
```

如果业务明确要求失败探测次数，可以把最后一个参数改成 `3` 等正数。不支持的设置返回 `HX_OS_NOT_SUPPORTED`，不会假装已经生效。Windows 7／8.1 支持设置这两个时长，但不能通过此接口指定探测次数，所以兼容这些系统时可以传 `0`。时长必须大于零，原生系统的数值上限仍然适用。

C++11 写法保持相同语义，只借用 socket，不接管所有权：

```cpp
#include <hx/SocketOptions.hpp>

hx::Result<void> configureDeviceConnection(hx_os_socket *socket)
{
	return hx::SocketOptions(socket).keepAlive(30, 5);
}
```

调用者检查 `result.ok()`，必要时从 `result.error()` 取得原生错误。关闭保活使用 `hx_os_socket_set_keepalive(socket, 0, error)`，或 `hx::SocketOptions(socket).keepAlive(false)`。

保活用于检测传输连接故障，不能证明设备业务线程还在正常工作，也不能证明命令已经处理完成。它不保证恰好在“空闲时间＋间隔×次数”后断开。一次配置内部需要多个原生调用，后面的设置失败时，前面的设置可能已经生效；不提供自动回滚。遇到失败应暂停后续使用，检查原因，再重新配置或关闭 socket。

## 4. 明确开启 IPv4／IPv6 双栈监听

先使用 `HX_OS_IPV6`、`HX_OS_TCP` 创建 socket，把尚未绑定的 socket 传给下面的函数，再使用普通 accept 接口接收连接。要同时接受两种地址族，应绑定 `::`，不是只监听本机 IPv6 的 `::1`：

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

调用者继续持有 socket，配置失败时负责关闭。传 `enabled=0` 表示只允许 IPv6。这项设置必须在 bind／connect 之前执行，不修改系统全局 IPv6 设置，也不会创建第二个 socket。C++ 对应 `hx::SocketOptions(listener).dualStack()` 或 `.dualStack(false)`，返回 `Result<void>`。

IPv4 客户端通过双栈 socket 接入时，地址仍按 IPv6 表示，例如 `::ffff:192.168.1.20`。不要因为 `family` 不是 `HX_OS_IPV4` 就误判为非法地址。这个 IPv6 socket 主动访问 IPv4 目标时，也应使用 IPv4 映射的 IPv6 地址。如果机器禁用了 IPv6，应报告失败，或者由业务明确选择创建两个独立 socket，库不会暗中改变策略。

## 5. 广播与组播分别怎么用

**广播**是把 IPv4 UDP 数据报发到广播地址。先调用 `hx_os_socket_set_broadcast(socket, 1, error)`，再使用 `hx_os_socket_send_to` 发往明确选择的地址，例如 `255.255.255.255`。设置成功并不代表已选定目的地址、已绑定端口或已成功送达。IPv6 使用组播，没有这种广播；把 IPv6／TCP socket 传给此便利接口属于用法错误。

**组播**是把消息发到一个组。接收端在指定网卡上加入这个组；发送端单独选择从哪张网卡发出。加入组不会自动绑定 UDP 端口，设置发送网卡也不会自动加入组。

| 步骤 | 接收端 | 发送端 |
| --- | --- | --- |
| 创建 | 地址族与组地址一致的 IPv4／IPv6 UDP socket | 与组地址使用相同地址族 |
| 本机回环 | 同机演示时开启 | 同机演示时开启 |
| 选择发送网卡 | 这个 socket 也需要发消息时才设置 | `set_multicast_interface(socket, index, error)` |
| 绑定 | 通配地址和发现服务端口 | 可以按需绑定源地址 |
| 组成员关系 | `join_multicast(socket, &group, index, error)` | 仅发送不需要加入 |
| 收发 | `receive_from`，遇到 WOULD_BLOCK 等待后重试 | `send_to`，遇到 WOULD_BLOCK 等待后重试 |
| 结束 | 离开相同组／网卡，再关闭 | 关闭 |

这些接口实现任意来源组播，不是按来源地址过滤的 SSM。加入／离开组只使用组地址，忽略 `group.port`；实际 UDP 端口由 bind／send 决定。索引 `0` 表示让操作系统选网卡。IPv6 的 `group.scope_id` 非零时可以补充未指定的网卡索引；两者都明确指定却不一致时返回错误。链路本地组建议明确选择 IPv6 网卡索引。

组播跳数范围是 `0..255`；`1` 限制转发在本地链路，`0` 限制在本机。这只是传播限制，不保证送达。Windows IPv4 的回环选项影响本机接收，POSIX 影响发送，所以跨平台同机演示在两端都开启。回环选项不是数据包过滤器，也不能替代加入组。

库不会自动设置 `SO_REUSEADDR`。示例只有一个监听者，不需要端口共享。如果业务确实需要多个监听者绑定同一端口，应在 bind 前显式调用 `hx_os_socket_set_reuseaddr`，并考虑原生平台的端口共享规则。重复加入不会被统一转换成成功，会保留原生组成员错误。

## 6. 跑一次 JSON 组播发现

[multicast_discovery.c](../examples/multicast_discovery.c) 是一个具有发送／监听两种模式的完整程序。先启动监听端，十秒内在另一个终端发送。两个进程各自持有 socket；遇到 WOULD_BLOCK 使用同一个有限截止时间等待，退出时释放资源。监听端只接收一份不超过 1024 字节的数据报，不会无限等待，也不会把被截断的数据交给后续处理。

先运行网卡查询示例，把下面的 `12` 替换成实际 IPv4 索引。同一台机器上的两个进程应选择同一张网卡；分开运行在两台机器时，各自使用本机索引，并确保处于能够互通组播的网络中。

Windows，终端一：

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe listen 239.255.42.99 12
```

Windows，终端二：

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe send 239.255.42.99 12
```

Linux／WSL 使用它自己的网卡索引，以下以 `2` 举例：

```sh
# Terminal 1
./build/bin/hx_os_multicast_discovery listen 239.255.42.99 2
# Terminal 2, within ten seconds
./build/bin/hx_os_multicast_discovery send 239.255.42.99 2
```

发送端预期输出：

```text
Sent 41 bytes: {"type":"discover","request_id":"demo-1"}
```

监听端预期输出，源地址和临时端口会随环境变化：

```text
Listening for one discovery datagram (10 seconds)...
Received from 192.168.1.20 port 54321: {"type":"discover","request_id":"demo-1"}
```

示例发送固定的有效 JSON，不把末尾 NUL 字符发送出去。接收端展示数据报字节，不负责校验任意对端发来的 JSON，也没有实现发现响应。增加自己的字段和解码逻辑时可继续看[序列化教程](SERIALIZATION.zh-CN.md)。发送成功表示本机已接受发送，不是监听端发回的确认。

IPv6 演示把组地址换成 `ff02::114`，索引换成对应网卡的 **IPv6 index**，程序会明确设置目标作用域。两种模式都允许最后增加端口，例如 `send 239.255.42.99 12 39022`，两边必须一致。这里的组地址用于演示，不表示已经为自己的业务协议注册。

如果接收超时，先检查地址族／索引是否选对、发送是否在监听的十秒内、网卡是否支持组播，以及网络是否允许组播。VPN、WSL 虚拟网络、无线 AP 或宿主防火墙都可能在 socket 配置成功后限制传输。示例报告错误，不会修改这些系统设置。一次超时不能证明库有缺陷，也不能证明报文已经到达对端机器。

## 7. C++ 配置与所有权

C++ 层提供同样的功能。对于已经创建好的 UDP socket 和解析好的组地址，可以用以下函数顺序配置，遇到第一处错误就停止，socket 仍由调用者持有：

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

接收端还需要按第 5 节绑定端口。结束时可以调用 `options.leaveMulticast(group, index)`，关闭 socket 也会释放成员关系。`SocketOptions` 不会关闭借用的 socket。所有设置调用都要求独占访问，socket 交给 loop 后返回 `HX_OS_BUSY`，即使 loop 当前空闲也一样。因此应先配置，再交给负责 I/O 的线程或 loop。网卡快照允许并发读取，但销毁前必须保证读取者已经停止使用。

## 8. 解析地址和端口，同时保留 IPv6 网卡范围

配置里同时包含数字地址和端口时，使用 `hx_os_endpoint_parse()`。它接受 `192.168.1.20:9000`、`[fe80::1%7]:9000`；其中 `%7` 表示网卡索引 7，也可以换成原生网卡名称。这个接口不查询 DNS，也不是 URL 解析器：IPv6 必须带方括号，端口不可省略，不会解码 URI 的百分号转义。

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

输出是 `[fe80::1%7]:9000`。格式化统一输出数字网卡索引。解析或格式化失败时，输出参数保留原值；解析输入最多 255 字节。

处理双栈对端时，`hx_os_address_is_ipv4_mapped()` 可以识别 `::ffff:192.168.1.20`；`hx_os_address_unmap_ipv4()` 把它还原为 IPv4，`hx_os_address_map_ipv4()` 做反向转换。两者保留端口，也允许原地转换。`hx_os_address_equal(&a, &b, 1)` 比较地址和端口，并把无 scope 的映射 IPv6 视为对应 IPv4；最后传 `0` 则要求地址族相同。IPv6 的 scope 也参与比较。这些工具不查询 DNS、不改变 socket 状态。

## 9. 查询到这台设备会使用哪张网卡

不要从快照里随意挑第一张 `UP` 网卡。调用 `hx_os_route_query(network, &destination, &route, &error)`，可以查询针对该目标选中的本地地址和网卡。它不会发送业务数据报，也不能证明远端设备在线；返回的源端口为零。

网卡示例新增了路由查询模式，使用已有可执行程序即可：

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe --route 127.0.0.1:9000
```

Linux/WSL：

```sh
./build/bin/hx_os_network_interfaces --route 127.0.0.1:9000
```

例如输出 `Destination 127.0.0.1:9000 uses 127.0.0.1:0 on interface 1`，表示到该目标使用本地 `127.0.0.1` 和索引为 1 的接口；实际索引以当前电脑为准。查询设备时替换为设备地址和端口，IPv6 使用带引号的 `"[fe80::1%7]:9000"` 形式。

Windows 使用原生路由查询；POSIX 通过临时 UDP 连接取得系统选中的源地址，再匹配 IP 网卡快照。如果同一个源地址对应多张网卡，返回 `HX_OS_NOT_SUPPORTED`，不会随意选一个。网络变化或枚举权限也可能导致失败，变化后应重新查询。无 scope 的映射 IPv6 目标会按 IPv4 查询。C++ 调用 `hx::NetworkInterfaces::routeTo(network, destination)`，返回 `Result<hx_os_route_info>`。

## 10. 网络变化后重新取得网卡列表

给现有示例加上 `--watch`：

```sh
./build/bin/hx_os_network_interfaces --watch
```

Windows 则给前面的 `.exe` 命令追加 `--watch`。示例先订阅，再打印初始快照，然后最多等待 30 秒的一次变化。有通知时打印 `Network changed; querying a fresh snapshot:` 并重新列出网卡；没有变化则打印 `No interface changes within 30 seconds.`，正常退出。示例本身不会修改系统网络设置。

业务调用顺序是：创建 `hx_os_interface_watch`，取得初始快照，使用单调截止时间调用 `hx_os_interface_watch_wait_until()`，成功后重新查询。等待者返回后，用 `hx_os_interface_watch_close(&watch, error)` 关闭订阅。`NOW` 表示只检查当前是否有通知，超时后对象仍可继续使用。

通知表示“应重新查询”，并不是逐条、有顺序的事件历史。多个变化可能合并，通知也可能涉及路由、无 IP 的接口或原生通知溢出，因此再次查询得到的快照可能没有变化。库不启动工作线程，不分配无界事件队列。Windows 的原生回调只负责唤醒事件，关闭时会等待这些短小的内部回调退出；Linux/Android 使用 netlink，macOS 使用路由 socket，不会偷偷降级为轮询。

Android 的普通应用策略可能允许枚举地址，却禁止原生订阅。此时应报告权限错误；宿主应用也可以接收平台提供的网络变化通知，再请求新的快照。shell／模拟器进程可运行，不代表普通 APK 拥有相同权限。创建失败通常返回空指针；若原生注册失败后的回滚本身也失败，会保留非空对象供关闭重试。因此即使创建失败，也应关闭非空结果。不能在另一个线程仍等待时销毁对象。

## 11. 调整 socket 缓冲区，以及只接收指定组播源

`hx_os_socket_set_buffers(socket, 65536, 65536, error)` 请求 64 KiB 的发送和接收缓冲区，某个方向传零表示保留该方向原值。`hx_os_socket_get_buffers()` 读回原生实际容量，不要假设一定等于请求值：系统可能取整、限制大小或采用不同的统计方式，例如 Linux 返回的数值可能加倍。设置按顺序进行，后续失败可能保留前面的修改。这里调整的是传输层缓冲区，不是 Frame／IPC／TLS 的业务队列上限。

C++ 的 `hx::SocketOptions(socket).buffers(65536, 65536)` 返回设置结果；不带参数的 `.buffers()` 返回 `Result<SocketOptions::BufferSizes>`，通过 `sendBytes` 和 `receiveBytes` 读取容量。

需要限制组播发送者时，调用 `hx_os_socket_join_multicast_source(socket, &group, &source, index, error)`，结束时用相同参数调用 `hx_os_socket_leave_multicast_source()`。例如组地址 `232.1.2.3`、源地址 `192.168.1.20` 表示在选中的 IPv4 网卡上只接收该发送者的组播。两者必须与 socket 地址族一致；源地址是单播地址，端口被忽略，加入操作不会替你绑定端口或选择发送网卡。原生系统不支持时返回 `HX_OS_NOT_SUPPORTED`，不会改成接收所有来源。C++ 对应 `.joinMulticastSource(group, source, index)` 和 `.leaveMulticastSource(...)`。

源地址过滤不等于身份认证，不能仅凭 IP 判断发送者可信。这些新增接口仍遵守既有平台范围；目标编译、shell 运行和真实应用权限需要分别验证。


## 12. 按名称连接，并选择可以取消的 DNS

想让库完成“解析名称、创建候选 socket、保留首个成功连接”时，调用 `hx_os_tcp_dial_until(network, host, port, NULL, deadline, &socket, error)`。IPv4／IPv6 地址交替尝试，默认相隔 250 ms，最多同时尝试两个；整个操作共用截止时间，没有胜出的 socket 会在返回前关闭。成功的 socket 归调用者持有，先关闭它，再销毁 `network`。C++11 对应显式所有者便利层 `hx::TcpDial(owner).connect(...)`。

默认沿用原来的系统名称解析，它的阻塞 DNS 调用无法被强制取消。已有地址时，可以直接使用 `hx_os_tcp_dial_addresses_until()`。如果 DNS 网络等待也需要响应取消，则显式创建 `hx_os_resolver`，把它赋给 `hx_os_dial_options.resolver`。连接调用期间借用这个对象，解析与连接共用 deadline 和 cancel token；两条路径都不会启动分离的后台线程。

前面的地址不响应时，后面的地址也会获得连接机会：在还有候选尚未开始时，每个无响应连接最多占用一个并发位置 2 秒；总预算较短时，再按剩余候选分配更短的时间，同时缩短启动间隔。库先检查已就绪的连接，再淘汰超时候选。所有地址都已开始后，仍存活的候选继续等到总截止时间。这样始终最多两个并发连接，但可能放弃较慢的地址；它不影响已建立连接的收发，也不是完整网络可达性保证。同一个拨号器也供扩展 TLS 连接使用。

### 12.1 运行 DNS 示例

可裁剪的 `HX_OS_ENABLE_CARES` 模块采用固定版本 c-ares，按照[依赖准备](DEPENDENCIES.zh-CN.md)使用子模块或 FetchContent。不可用时，`hx_os_resolver_available()` 返回零，创建返回 `HX_OS_NOT_SUPPORTED`。在现有构建目录编译公开示例：

```sh
cmake --build build --config Release --target hx_os_dns_lookup
```

Windows PowerShell：

```powershell
.\build\bin\Release\hx_os_dns_lookup.exe 127.0.0.1
```

Linux／WSL：

```sh
./build/bin/hx_os_dns_lookup 127.0.0.1
```

两边都会打印 `IPv4 127.0.0.1 port=443`，这个本地检查不需要互联网。把地址换成域名后，会列出它当前的 IPv4／IPv6 解析结果。可选的第二个参数指定数字 DNS 服务器地址，例如 `127.0.0.1:5353` 表示你自己运行的本地 DNS 服务；示例不会替你启动它。结果中的 443 只是附加的端口值，并没有发起连接。没有应答的查询按两秒总预算报告超时，仍受本地初始化和系统调度影响。

### 12.2 创建一个解析器并重复使用

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

解析器在调用线程上驱动 c-ares 的异步 socket，因此这个便利函数等待期间仍占用调用线程；它不是协程挂起，也不是 `hx_os_loop` 请求。同一个解析器一次只允许一个查询：并发调用返回 `BUSY`，同线程重入返回 `DEADLOCK`，不会在内部无限排队。传入 `hx_os_cancel_token` 后，可以从另一线程请求取消；网络等待时每隔不超过 50 ms 检查一次，实际响应仍受调度影响。取消回调在返回前退出，超时不会遗留借用调用方缓冲区的后台工作。销毁前停止发起新调用；结果列表独立持有内存，可以活得比解析器更久。

结果最多 256 个不同地址，最多跟踪 64 个原生 socket。默认关闭原生缓存，避免不断解析不同名称时缓存无限累积；c-ares 自身的解析和配置内存不计入 hx 分配器预算。名称使用 ASCII／Punycode，默认查询 hosts 文件及 DNS；`use_hosts_file=0` 只查询 DNS。这不等于复刻操作系统的全部 NSS 插件、VPN 解析策略或 mDNS 行为。配置及 hosts 文件读取不是硬实时操作，DNS 配置变化后应重新创建解析器。

C++ 包含 `<hx/Resolver.hpp>`，使用 `hx::Resolver(owner).create()` 和 `.resolve(host, port)`；后者返回 `Result<hx::Addresses>`，地址列表自动释放，解析器通过 `.close()` 显式关闭。仍可通过依赖目标直接使用原生 c-ares，但宿主需要协调原生调用者，以及共享同一 c-ares 二进制的 libcurl 调用者之间的全局初始化、清理和分配器修改。不要在 Windows `DllMain` 中初始化。

### 12.3 事件循环和协程里怎么用

需要让当前线程在等待期间处理其他事件时，调用 `hx_os_loop_resolve()`；不必创建 resolver 或填写 operation。`hx_os_loop_create_local()` 创建的 loop 也能使用它，传 `NULL` 选项采用 c-ares 默认配置。

```c
hx_os_request *request = NULL;
hx_os_status status = hx_os_loop_resolve(loop, "localhost", 443,
	HX_OS_TCP, NULL, hx_os_deadline_after_ms(2000),
	resolved, &state, &request, &error);
```

这里的 `resolved` 是完成回调，`state` 是你提供的结果对象。通过 `hx_os_loop_run_until` 或 `poll_until` 驱动 loop，回调在该轮询线程上执行一次。回调通过 `hx_os_request_take_addresses()` 取走结果，用完后销毁列表。完整定义与清理代码见 [dns_lookup.c](../examples/dns_lookup.c)。

其他线程可以调用 `hx_os_request_cancel(request)`。仍需轮询并等到回调返回，才能释放回调状态。关闭超时也必须保留 loop 和相关对象，稍后重试。提交失败没有回调。请求复制主机名及选项，使用原有 DNS 工作池、请求数量和字节预算；不会为每次解析创建线程。禁用 c-ares 返回 `NOT_SUPPORTED`，不偷偷退回阻塞解析。

协程内调用 `hx_os_coro_resolve_cancellable(host, port, options, deadline, &addresses, &error)`，等待期间会挂起当前协程。有栈协程直接调用；C 状态机使用 `HX_OS_CORO_AWAIT`，把结果和截止时间保存在持久状态。旧的 `hx_os_coro_resolve` 和 `HX_OS_OP_RESOLVE` 继续使用系统 DNS。

```sh
./build/bin/hx_os_dns_lookup --async 127.0.0.1
./build/bin/hx_os_dns_lookup --coro 127.0.0.1
```

Windows 把程序路径换成 `build/bin/Release/hx_os_dns_lookup.exe`。两种模式仍打印 `IPv4 127.0.0.1 port=443`；换成域名后按同一两秒预算解析，可追加自己的 `DNS_IP:PORT`。数字地址检查用于演示调用方式，不依赖外部 DNS 服务。

C++11 中 `hx::IoLoop::resolve(host, port, callback, deadline)` 向回调传入拥有结果的 `Result<hx::Addresses>`，返回可取消的 `IoRequest`；仍需调用 `runUntil` 和显式 `closeUntil`。协程使用 `hx::coro::Addresses::resolveCancellable(host, port, deadline)`，C++20 使用 `co_await hx::coro::async::resolveCancellable(...)`。这些便利层复用同一 C 实现。
