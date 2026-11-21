# 网络参数、网卡选择与组播发现

[English](NETWORK_OPTIONS.md) · [使用教程](USAGE.zh-CN.md) · [可运行示例](../examples/README.zh-CN.md) · [安装包接入](INSTALL.zh-CN.md)

在编写局域网设备发现、工业相机/传感器组播订阅、长连接保活或双栈（IPv4/IPv6）服务端时，往往需要对底层 Socket 进行精细的参数配置，并准确查出本机有哪些可用网卡、去往目标设备应该走哪张网卡，甚至在网线插拔后自动刷新网卡列表。本文介绍 `hx_c_platform` 提供的网络选项、网卡枚举、路由查询、网络变化监听以及可取消 DNS 解析（c-ares）接口。

## 1. 根据要做的事情选择接口

表中的 Socket 配置接口用于设置普通的 TCP / UDP Socket。**请在将 Socket 绑定到 `hx_os_loop` 事件循环之前调用它们，并确保调用期间没有其他线程并发操作同一个 Socket。** 这些函数只修改当前 Socket 句柄或只读查询系统状态，绝不会篡改操作系统的全局网络配置，也不会偷偷启动后台线程。

> [!NOTE]
> **构建开关与依赖说明**
> 本页功能要求 `HX_OS_ENABLE_NETWORK` 与 `HX_OS_ENABLE_NET_OPTIONS` 同时开启（默认均为 `ON`）。若关闭其中任一开关，相关配置函数统一返回 `HX_OS_NOT_SUPPORTED`，且 `hx_os_interfaces_available()` 返回 `0`。在 Windows 平台上，CMake 目标会自动链接系统自带的 `iphlpapi` 库，无需安装额外第三方依赖。

| 你要做的事情 | C 接口 | 调用完成了什么 |
| --- | --- | --- |
| 及时发现长期空闲的 TCP 连接已死锁或掉线 | `hx_os_socket_configure_keepalive` | 开启 TCP 保活（Keepalive），并以秒为单位设置空闲等待时间与探测间隔 |
| 让一个 IPv6 Socket 同时接受 IPv4 和 IPv6 连接 | `hx_os_socket_set_dual_stack` | 显式开启或关闭 IPv4 映射地址（`IPV6_V6ONLY=0`）的双栈模式 |
| 发送 IPv4 UDP 局域网广播 | `hx_os_socket_set_broadcast` | 开启广播发送权限（`SO_BROADCAST`）；目标广播地址仍由 `send_to` 指定 |
| 订阅或退订某个 UDP 组播组（Multicast Group） | `hx_os_socket_join_multicast` / `leave_multicast` | 在指定的本机网卡上加入或离开任意源组播组（ASM） |
| 指定发出的 UDP 组播包走哪张本机网卡 | `hx_os_socket_set_multicast_interface` | 按对应地址族（IPv4 或 IPv6）的网卡索引固定组播发送接口 |
| 限制组播跨路由跳数 / 控制本机回环接收 | `hx_os_socket_set_multicast_hops` / `set_multicast_loopback` | 设置组播 TTL/Hop Limit 上限以及本机发出的组播是否回环给本机 |
| 枚举本机当前可见的网卡与 IP 地址 | `hx_os_interfaces_query` | 返回由调用方独占持有的不可变网卡快照，无需预先创建 `hx_os_network` |

注意：配置完这些选项后，普通的 Socket 接收调用返回的依然是原始字节流或单条 UDP 数据报。如果你需要在 TCP 流上切分完整消息，请搭配[消息帧（Frames）](FRAMES.zh-CN.md)；如果你需要带请求匹配和自动重连的本地会话，请使用 [本地 IPC](IPC.zh-CN.md)。

## 2. 先看看本机有哪些网卡

复用已有的构建目录（将 `build` 替换为你自己的目录名），编译网卡查询与组播发现两个示例程序。下方 Windows 命令以 `Debug` 配置为例（请与你预编译的依赖 SDK 配置保持一致，同步修改 `--config` 与运行路径；Linux 单配置构建直接使用缓存中的配置）：

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Debug --target hx_os_network_interfaces
cmake --build build --config Debug --target hx_os_multicast_discovery
```

在仓库根目录下运行网卡枚举示例：

**Windows PowerShell：**

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe
```

**Linux / WSL：**

```sh
./build/bin/hx_os_network_interfaces
```

程序会打印当前机器上所有可见的 IP 网卡及其索引、MTU、状态标志与绑定地址（具体数值因机器而异）：

```text
IP-visible interfaces: 2
eth0 (eth0)
  IPv4 index=2, IPv6 index=2, MTU=1500
  up=1, multicast=1, loopback=0
  192.168.1.20/24 (IPv4, scope=0)
  fe80::1234/64 (IPv6, scope=2)
```

在为组播选择网卡时，应当挑选包含目标地址族（IPv4 或 IPv6）且满足 `up=1`、`multicast=1` 的网卡。**请特别注意：配置 IPv4 组播时必须传入 `ipv4_index`，配置 IPv6 组播时必须传入 `ipv6_index`**（在 Windows 上同一张网卡的这两个索引值可能不同），且切勿盲目假定列表中的第 1 张网卡就是默认路由网卡。完整源码见 [network_interfaces.c](../examples/network_interfaces.c)。

### 在 C 与 C++11 中读取网卡快照

如果你需要在自己的界面或配置逻辑中展示网卡列表，C 语言的查询、遍历与释放模式如下：

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

在 C++11 中，包含 `<hx/NetworkInterfaces.hpp>` 即可获取相同的快照，并在离开作用域时自动释放内存：

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
> **快照生命周期与字段语义边界**
> - **指针有效期**：网卡名称（UTF-8 编码）和地址数组均由 `hx_os_interface_list` 快照内部持有，调用 `hx_os_interface_list_destroy()` 后不得再解引用任何内部指针。在 Windows 上，`name` 是系统适配器标识，`display_name` 是人类可读的友好名称；在 POSIX 上二者通常均为网卡接口名（如 `eth0`）。
> - **快照代表什么**：查询仅列出当前进程权限可见且绑有 IP 地址的接口（空列表也是合法的正常结果）。`up=1` 仅反映网卡链路层/协议栈开启，不证明能够连通外网；`mtu=0` 或索引为 `0` 表示系统未提供该项信息，子网前缀长度 `255` 表示未知。该接口不采集物理 MAC 地址，也不负责 Android 系统的进程级 `Network` 绑定。

## 3. 一次调用设置 TCP 保活参数

对于已经创建好的 TCP Socket，调用 `hx_os_socket_configure_keepalive()` 即可一次性开启保活并设置探测时间。例如，下面的辅助函数设置在连接空闲 30 秒后开始发送保活探测包，后续每隔 5 秒探测一次，而最大失败探测次数保持操作系统默认值（传 `0`）：

```c
hx_os_status configure_device_connection(hx_os_socket *socket,
	hx_os_error *error)
{
	return hx_os_socket_configure_keepalive(socket, 30, 5, 0, error);
}
```

如果业务明确要求指定失败探测次数（例如连续 3 次无响应即断开），可将第 4 个参数设为 `3`。注意：Windows 7 / 8.1 系统 API 支持设置空闲时长与探测间隔，但不支持按 Socket 修改探测次数——为了兼容 Win7/8.1 基线，建议将该参数传 `0`（若在不支持的平台上强行传入非零探测次数，函数会如实返回 `HX_OS_NOT_SUPPORTED`，绝不假装成功）。空闲时长和探测间隔必须为正整数。

C++11 封装借用底层的 `hx_os_socket *` 而不接管其所有权：

```cpp
#include <hx/SocketOptions.hpp>

hx::Result<void> configureDeviceConnection(hx_os_socket *socket)
{
	return hx::SocketOptions(socket).keepAlive(30, 5);
}
```

如果需要关闭保活，调用 `hx_os_socket_set_keepalive(socket, 0, error)` 或 `hx::SocketOptions(socket).keepAlive(false)` 即可。

> [!IMPORTANT]
> TCP Keepalive 仅用于检测底层传输链路死连接，**不能证明对端进程的业务线程没有卡死，也不代表之前发送的业务命令已被执行**。此外，该配置函数内部会依次执行多个系统 `setsockopt` / `WSAIoctl` 调用；如果后半步调用失败，前半步修改可能已经生效（无自动回滚），此时应停止后续流程，检查错误后再决定重新配置或直接关闭该 Socket。

## 4. 明确开启 IPv4／IPv6 双栈监听

不同操作系统对 IPv6 Socket 是否默认同时接收 IPv4 流量（`IPV6_V6ONLY`）有着截然不同的默认行为。为了写出跨平台一致的双栈监听服务端，请先用 `HX_OS_IPV6` 和 `HX_OS_TCP` 创建 Socket，在 `bind` 之前显式调用 `hx_os_socket_set_dual_stack(listener, 1, error)`，然后绑定到 IPv6 通配地址 `::`（注意不要绑成仅限本机回环的 `::1`）：

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

传入 `enabled = 0` 则强制该 Socket 仅接受纯 IPv6 流量。C++11 中对应调用 `hx::SocketOptions(listener).dualStack()` 或 `.dualStack(false)`。

> [!TIP]
> **处理双栈 Socket 接收到的 IPv4 客户端地址**
> 当 IPv4 客户端连入双栈 IPv6 监听器时，`accept` 返回的对端地址依然是 `HX_OS_IPV6` 地址族，表现为 **IPv4 映射的 IPv6 地址**（例如 `::ffff:192.168.1.20`）。请勿因为检查到 `family != HX_OS_IPV4` 就误判为非法客户端！你可以使用第 8 节介绍的 `hx_os_address_is_ipv4_mapped()` 和 `hx_os_address_unmap_ipv4()` 轻松识别并还原它。

## 5. 广播与组播分别怎么用

- **IPv4 广播（Broadcast）**：先调用 `hx_os_socket_set_broadcast(socket, 1, error)` 解锁广播发送权限，随后通过 `hx_os_socket_send_to()` 向明确的广播地址（如子网广播或 `255.255.255.255`）发送 UDP 数据报。注意：IPv6 协议取消了广播机制（改用组播），因此向此函数传入 IPv6 或 TCP Socket 会直接报错。
- **UDP 组播（Multicast）**：组播将消息发往一个特定的组地址（如 `239.255.42.99` 或 `ff02::114`）。接收端需要在指定网卡上**加入该组（Join Group）**并绑定监听端口；发送端则独立指定**从哪张网卡发出（Multicast Interface）**。加入组不会自动替你绑定 UDP 端口，设置发送网卡也不会自动加入组：

| 操作步骤 | 组播接收端（Listener） | 组播发送端（Sender） |
| --- | --- | --- |
| 1. 创建 Socket | 创建与组播地址同地址族（IPv4 或 IPv6）的 UDP Socket | 创建与组播地址同地址族的 UDP Socket |
| 2. 本机回环开关 | 同机双进程测试时开启 `set_multicast_loopback(1)` | 同机双进程测试时开启 `set_multicast_loopback(1)` |
| 3. 选择发送网卡 | 仅当该 Socket 也要向外发组播时才需设置 | 调用 `hx_os_socket_set_multicast_interface(socket, index, error)` |
| 4. 绑定端口 | 绑定通配地址（`0.0.0.0` 或 `::`）与组播目标端口 | 可选（通常无需显式绑定固定端口） |
| 5. 组成员关系 | 调用 `hx_os_socket_join_multicast(socket, &group, index, error)` | 纯发送端无需加入组 |
| 6. 收发报文 | 调用 `receive_from`（遇到 `HX_OS_WOULD_BLOCK` 时等待截止时间重试） | 调用 `send_to` 发往组播地址与端口（遇到 `HX_OS_WOULD_BLOCK` 时等待重试） |
| 7. 退出清理 | 调用 `leave_multicast` 退出同一组与网卡，随后关闭 Socket | 直接关闭 Socket |

### 组播参数细节

1. **网卡索引与 IPv6 Scope**：`join_multicast` / `leave_multicast` 只读取 `group` 中的 IP 地址而忽略 `group.port`（实际收发端口由 `bind` 和 `send_to` 决定）。网卡索引传 `0` 表示交由操作系统选择默认网卡；对于 IPv6 链路本地组播（如 `ff02::...`），建议始终传入明确的 IPv6 网卡索引（若 `index` 为 `0` 但 `group.scope_id` 非零，库会自动采用 `scope_id`；若两者均显式指定但数值冲突则返回错误）。
2. **跳数限制（Hops / TTL）与跨平台回环差异**：`hx_os_socket_set_multicast_hops()` 接受 `0..255`（`1` 限制在当前本地子网链路内不跨路由器转发，`0` 限制在本机内部）。对于 `hx_os_socket_set_multicast_loopback()`，Windows IPv4 将其作用于**接收端 Socket**，而 POSIX 将其作用于**发送端 Socket**——因此在编写跨平台同机测试程序时，最稳妥的做法是在收发两端都开启它。
3. **关于端口复用（`SO_REUSEADDR`）**：本库不会在加入组播时暗中替你开启 `SO_REUSEADDR`。如果你的业务确实需要本机多个进程同时绑定同一个组播 UDP 端口，请在 `bind` 之前显式调用 `hx_os_socket_set_reuseaddr()`。

## 6. 跑一次 JSON 组播发现

[multicast_discovery.c](../examples/multicast_discovery.c) 是一个包含监听（`listen`）和发送（`send`）两种模式的完整组播发现程序。请先根据第 2 节 `hx_os_network_interfaces` 查出的结果，将下方命令中的网卡索引（如 Windows 的 `12` 或 Linux 的 `2`）替换为你机器上真实活动网卡的 **IPv4 index**。

**Windows PowerShell（终端一，启动监听端，等待 10 秒）：**

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe listen 239.255.42.99 12
```

**Windows PowerShell（终端二，在 10 秒内发送发现报文）：**

```powershell
.\build\bin\Debug\hx_os_multicast_discovery.exe send 239.255.42.99 12
```

**Linux / WSL（以网卡索引 `2` 为例）：**

```sh
# 终端一
./build/bin/hx_os_multicast_discovery listen 239.255.42.99 2
# 终端二（在 10 秒内执行）
./build/bin/hx_os_multicast_discovery send 239.255.42.99 2
```

运行后，发送端输出：

```text
Sent 41 bytes: {"type":"discover","request_id":"demo-1"}
```

监听端输出（源 IP 和临时端口随当前机器变化）：

```text
Listening for one discovery datagram (10 seconds)...
Received from 192.168.1.20 port 54321: {"type":"discover","request_id":"demo-1"}
```

如果你想测试 **IPv6 组播发现**，只需把组地址换为 `ff02::114`，并将索引换为该网卡的 **IPv6 index**（两种模式还支持在末尾追加自定义端口号，如 `send 239.255.42.99 12 39022`，收发双方端口须一致）。若需在收到发现包后进一步解析 JSON 字段，请参考[配置与序列化教程](SERIALIZATION.zh-CN.md)。

> [!TIP]
> **排查组播收不到包的原因**
> 如果监听端等待 10 秒后超时退出，请依次检查：1) 地址族与网卡索引（IPv4 vs IPv6 index）是否匹配；2) 发送命令是否在监听端启动后的 10 秒窗口内执行；3) 目标网卡是否显示 `up=1, multicast=1`；4) VPN 虚拟网卡、WSL 网络桥接、企业 Wi-Fi AP 隔离或系统防火墙是否拦截了 UDP 组播包。

## 7. C++ 配置与所有权

C++11 提供了零异常、不夺取 Socket 所有权的 `hx::SocketOptions` 包装类。对于已创建的 UDP Socket 和已解析的组播地址，可以按如下链式顺序配置：

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

配置完成后，接收端按第 5 节绑定端口即可开始收包；退出时可调用 `options.leaveMulticast(group, index)`（直接关闭 Socket 也会由内核自动释放组成员关系）。请记住：一旦 Socket 绑定到 `hx_os_loop` 事件循环后，哪怕当前事件循环处于空闲状态，直接调用这些普通 Setter 也会返回 `HX_OS_BUSY`——务必先完成配置，再将 Socket 交给事件循环。

## 8. 解析地址和端口，同时保留 IPv6 网卡范围

当配置文件中的端点字符串同时包含数字 IP 地址与端口号（甚至包含 IPv6 链路本地网卡 Zone ID）时，使用 `hx_os_endpoint_parse()` 可以一步完成解析：
- 支持 IPv4 格式：`192.168.1.20:9000`
- 支持带网卡范围（Scope/Zone）的 IPv6 格式：`[fe80::1%7]:9000`（其中 `%7` 代表网卡索引 `7`，也支持传入系统原生网卡名如 `%eth0`）。

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

上述代码会输出规范化后的 `[fe80::1%7]:9000`（格式化输出时统一使用数字网卡索引）。注意该函数是纯粹的数字端点解析器，不发起 DNS 查询，也不解析 URL 百分号转义（IPv6 地址必须带方括号 `[...]`，且端口号不可省略，输入上限为 255 字节）。

此外，针对双栈通信中的地址转换与比较，库还提供了四个纯内存工具函数：
- `hx_os_address_is_ipv4_mapped(&addr)`：判断是否为 `::ffff:a.b.c.d` 形式的映射地址。
- `hx_os_address_unmap_ipv4(&addr)` / `hx_os_address_map_ipv4(&addr)`：在 IPv4 与 IPv4-mapped IPv6 之间双向原地转换（自动保留端口号）。
- `hx_os_address_equal(&a, &b, 1)`：比较两个地址与端口是否等价（第 3 个参数传 `1` 时会自动将无 Scope 的映射 IPv6 地址与对应的 IPv4 地址视为相等；传 `0` 则要求地址族严格相同）。

## 9. 查询到这台设备会使用哪张网卡

在多网卡机器上，不要从网卡列表里随便抓第一张 `up=1` 的网卡去发包。调用 `hx_os_route_query(network, &destination, &route, &error)` 可以直接向操作系统路由表询问：“如果我要连接 `destination`，系统会选用哪张本机网卡和哪个本机源 IP？”（该查询不会向网络发送任何业务数据报）。

你可以直接用前面编译好的 `hx_os_network_interfaces` 加上 `--route` 参数体验路由查询：

**Windows PowerShell：**

```powershell
.\build\bin\Debug\hx_os_network_interfaces.exe --route 127.0.0.1:9000
```

**Linux / WSL：**

```sh
./build/bin/hx_os_network_interfaces --route 127.0.0.1:9000
```

输出示例：`Destination 127.0.0.1:9000 uses 127.0.0.1:0 on interface 1`（查询真实局域网设备时，将参数替换为设备 IP 与端口即可；IPv6 请加引号传入如 `"[fe80::1%7]:9000"`）。在 C++11 中，可直接调用 `hx::NetworkInterfaces::routeTo(network, destination)`，返回 `hx::Result<hx_os_route_info>`。

> [!NOTE]
> 在 Windows 上，底层直接调用系统原生路由查询 API；在 POSIX 系统上，库通过一次临时的内核态 UDP 路由绑定拿到系统选定的源 IP，再与当前网卡快照比对（如果同一个源 IP 同时绑在多张网卡上导致歧义，函数会明确返回 `HX_OS_NOT_SUPPORTED`，绝不盲猜）。

## 10. 网络变化后重新取得网卡列表

当用户插拔网线、切换 Wi-Fi 或连上 VPN 时，原有的网卡快照就会过期。给示例程序加上 `--watch` 参数可以体验实时网络变化监听：

```sh
./build/bin/hx_os_network_interfaces --watch
```

（Windows 下在 `.exe` 后追加 `--watch`。）程序会先注册系统网络变化订阅、打印初始网卡列表，随后阻塞等待最多 30 秒：如果在 30 秒内检测到网络变化，它会立即打印 `Network changed; querying a fresh snapshot:` 并重新枚举网卡；若 30 秒内无变化则打印 `No interface changes within 30 seconds.` 并正常退出。

在业务代码中接入网络监听的标准流程是：
1. 创建 `hx_os_interface_watch` 订阅对象。
2. 调用 `hx_os_interfaces_query()` 获取初始网卡快照。
3. 传入单调截止时间调用 `hx_os_interface_watch_wait_until()`（传 `HX_OS_DEADLINE_NOW` 可做非阻塞轮询）。
4. 一旦等待返回 `HX_OS_OK`，重新调用 `hx_os_interfaces_query()` 获取最新快照。
5. 退出时，确保等待线程已返回后调用 `hx_os_interface_watch_close(&watch, error)` 销毁订阅（注意：即便创建订阅时因系统回滚异常失败但返回了非空指针，也应调用 close 回收）。

> [!NOTE]
> **网络变化通知的合并（Coalescing）与平台机制**
> - 底层全部采用操作系统原生事件机制（Windows 使用原生通知回调唤醒事件、Linux/Android 使用 Netlink、macOS 使用 Routing Socket），**不启动后台轮询线程，也不维护无界事件队列**。
> - 每次唤醒都是一个“请重新扫描快照”的提示信号（Rescan Hint），而非逐条排列的事件流水账——短时间内的多次变动会被自动合并，有时路由变动也未必改变 IP 列表。在 Android 普通 APK 中，如果系统策略禁止直接创建底层 Netlink 订阅，接口会如实报告权限错误，此时宿主应用可监听 Android 系统的 `ConnectivityManager` 回调后再调用 `hx_os_interfaces_query()`。

## 11. 调整 socket 缓冲区，以及只接收指定组播源

- **调整内核收发缓冲区容量**：
  - 调用 `hx_os_socket_set_buffers(socket, 65536, 65536, error)` 请求将内核发送与接收缓冲区均设为 64 KiB（任一方向传 `0` 表示保持该方向原值不变）。
  - 调用 `hx_os_socket_get_buffers()` 可读回操作系统实际分配的字节数（不要假定返回值一定等于请求值，例如 Linux 内核会自动翻倍记账）。
  - C++11 中对应 `hx::SocketOptions(socket).buffers(65536, 65536)` 以及无参读取 `.buffers()`（返回包含 `sendBytes` 与 `receiveBytes` 的 `Result<SocketOptions::BufferSizes>`）。注意这里调整的是内核传输层缓冲区，而非 `hx_frame`、`hx_ipc` 或 `hx_tls` 的应用层队列预算。
- **特定源组播过滤（Source-Specific Multicast, SSM）**：
  - 如果你只希望接收特定发送方 IP 发出的组播数据，可调用 `hx_os_socket_join_multicast_source(socket, &group, &source, index, error)`（退出时以相同参数调用 `hx_os_socket_leave_multicast_source()`；C++11 对应 `.joinMulticastSource(...)` 与 `.leaveMulticastSource(...)`）。
  - 例如组播地址 `232.1.2.3` 搭配单播源地址 `192.168.1.20`，表示在指定网卡上仅接收来自 `192.168.1.20` 的组播包。若底层系统不支持特定源过滤，函数会明确返回 `HX_OS_NOT_SUPPORTED`，绝不会静默降级为接收所有来源。

## 12. 按名称连接，并选择可以取消的 DNS

当你需要连接一个域名（如 `"example.com"`），并希望库自动帮你完成“解析域名 → 交替尝试 IPv4 与 IPv6 候选地址（Happy Eyeballs） → 保留第一个成功连通的 Socket 并关闭其余候选”时，直接调用 `hx_os_tcp_dial_until(network, host, port, NULL, deadline, &socket, error)` 即可（C++11 对应 `hx::TcpDial(owner).connect(...)`）。
- **连接竞速与超时分配**：候选的 IPv4 与 IPv6 地址会交替发起连接（默认间隔 250 毫秒，最多同时保持 2 个并发连接尝试）。当排在前面的某个候选地址迟迟不响应时，它在还有未尝试候选地址的情况下最多只占用连接槽位 2 秒（若总截止时间更短则按比例均分），从而确保后面的有效地址不会被卡死在队列外；一旦所有候选地址均已发起尝试，存活的候选将继续等待直到总截止时间。
- **系统 DNS vs. 可取消的 c-ares 解析器**：默认情况下（`options` 传 `NULL`），域名解析走操作系统的同步 DNS 接口（无法在等待 DNS 响应中途被取消）；如果你手头已经有解析好的地址列表，可直接调用 `hx_os_tcp_dial_addresses_until()`。如果你要求**连 DNS 解析阶段的网络等待也必须严格响应截止时间与取消令牌（Cancel Token）**，只需显式创建一个 `hx_os_resolver` 并赋给 `hx_os_dial_options.resolver`（包括所有 TLS 客户端连接入口 `hx_tls_connect*` 和 `hx::Tls::connect()` 均共用同一套拨号器，详见 [TLS 客户端说明](TLS.zh-CN.md#2-c-和-c-怎样直接调用)）。

### 12.1 运行 DNS 示例

可裁剪的 `HX_OS_ENABLE_CARES` 模块集成了固定版本的 `c-ares` 异步 DNS 库（按[依赖准备指南](DEPENDENCIES.zh-CN.md)通过 Git 子模块或 FetchContent 获取；若禁用该模块，`hx_os_resolver_available()` 返回 `0`，创建函数返回 `HX_OS_NOT_SUPPORTED`）。在已有构建目录中编译公开示例：

```sh
cmake --build build --config Release --target hx_os_dns_lookup
```

**Windows PowerShell：**

```powershell
.\build\bin\Release\hx_os_dns_lookup.exe 127.0.0.1
```

**Linux / WSL：**

```sh
./build/bin/hx_os_dns_lookup 127.0.0.1
```

两条命令均会输出 `IPv4 127.0.0.1 port=443`（无需联网即可验证调用链路）。将 `127.0.0.1` 替换为任意真实域名，即可列出其当前的 IPv4 / IPv6 解析结果；还可以在第二个参数传入自定义的数字 DNS 服务器地址（如 `127.0.0.1:5353`）。

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

- **单查询串行与取消保证**：`hx_os_resolver_resolve_until()` 会在当前调用线程上驱动 `c-ares` 的非阻塞 Socket（不启动后台线程）。同一个 `hx_os_resolver` 同一时刻只允许执行一个查询：多线程并发调用返回 `HX_OS_BUSY`，同线程重入返回 `HX_OS_DEADLOCK`。传入 `hx_os_cancel_token` 后，其他线程可随时请求取消（网络等待期间至少每 50 毫秒检查一次取消状态），且在函数返回前所有底层回调均已安全退场，绝不会在超时后残留访问调用方栈内存的后台任务。返回的 `hx_os_address_list` 独立持有内存，生命周期可长于解析器本身。
- **容量与解析范围**：单次结果最多保留 256 个去重地址，最多同时跟踪 64 个原生 DNS Socket；默认关闭 `c-ares` 内部无界缓存以防长期运行内存膨胀。默认同时查询本机 `hosts` 文件与 DNS 服务器（设置 `use_hosts_file = 0` 可仅查 DNS）。若系统网络切换导致 DNS 服务器配置改变，请销毁并重建解析器。
- **C++11 封装**：包含 `<hx/Resolver.hpp>`，使用 `hx::Resolver(owner).create()` 和 `.resolve(host, port)`（返回自动管理内存的 `Result<hx::Addresses>`，退出前显式调用 `.close()` 关闭解析器）。

### 12.3 事件循环和协程里怎么用

如果你希望当前线程在等待 DNS 解析期间继续处理其他 I/O 事件，可以直接调用 `hx_os_loop_resolve()`，无需手动创建 `hx_os_resolver`：

```c
hx_os_request *request = NULL;
hx_os_status status = hx_os_loop_resolve(loop, "localhost", 443,
	HX_OS_TCP, NULL, hx_os_deadline_after_ms(2000),
	resolved, &state, &request, &error);
```

通过 `hx_os_loop_run_until()` 或 `poll_until()` 驱动事件循环后，完成回调 `resolved` 会在轮询线程上执行一次；在回调内调用 `hx_os_request_take_addresses()` 取走地址列表并在使用后销毁即可（完整代码见 [dns_lookup.c](../examples/dns_lookup.c)）。其他线程可随时调用 `hx_os_request_cancel(request)` 取消查询，但仍需继续轮询直到完成回调返回后才能释放回调上下文 `state`。

在协程中，调用 `hx_os_coro_resolve_cancellable(host, port, options, deadline, &addresses, &error)` 即可在等待 DNS 期间自动挂起当前协程（有栈协程直接调用，C 状态机协程配合 `HX_OS_CORO_AWAIT` 使用）：

```sh
./build/bin/hx_os_dns_lookup --async 127.0.0.1
./build/bin/hx_os_dns_lookup --coro 127.0.0.1
```

（Windows 下将路径换为 `build/bin/Release/hx_os_dns_lookup.exe`。）在 C++ 中，事件循环可调用 `hx::IoLoop::resolve(...)`，C++11 协程可调用 `hx::coro::Addresses::resolveCancellable(...)`，C++20 协程可直接 `co_await hx::coro::async::resolveCancellable(...)`。
