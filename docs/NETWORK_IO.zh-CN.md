# 常用网络收发

[English](NETWORK_IO.md) · [网络选项](NETWORK_OPTIONS.zh-CN.md) · [安装包接入](INSTALL.zh-CN.md)

## 1. 按需要选择入口

| 你要做什么 | 调用入口 | 由你持有的对象 |
| --- | --- | --- |
| 等待一条 UDP 消息 | `hx_os_socket_receive_from_until` | socket 和接收缓冲区 |
| 知道消息从哪个网卡、发往本机哪个 IP | `hx_os_socket_receive_packet_until` | socket、缓冲区和返回的元数据值 |
| 指定 UDP 回复的来源网卡 | `hx_os_socket_send_packet_until` | 可选的本次报文路由值 |
| 一次发送／接收几条数据报 | `hx_os_socket_send_batch_until`／`receive_batch_until` | 最多 64 项及其缓冲区 |
| 直接发送分开的消息头和内容 | `hx_os_socket_sendv_until` | 按顺序借用的缓冲区 |
| 按域名连接，并尝试 IPv4／IPv6 | `hx_os_tcp_dial_until` | network 对象和返回的 socket |
| 能取消网络等待的 DNS 查询 | `hx_os_resolver_resolve_until` | 显式创建的 c-ares 解析器 |
| 可信局域网内的 KCP 完整消息 | `hx_os_kcp_session_*` | 会话对象和收到的消息 |

所有 `_until` 接受绝对单调截止时间。用 `hx_os_deadline_after_ms(...)` 建立一次，在整个操作中复用。UDP 的 `NOW` 会先尝试一次，需要等待时返回 `TIMEOUT`。零字节 UDP 数据报也是有效消息。原始 TCP 始终是字节流；需要消息边界时使用[消息帧](FRAMES.zh-CN.md)。

## 2. 先跑通 UDP 数据报示例

在已有构建目录编译完整的 [udp_packet.c](../examples/udp_packet.c)：

```sh
cmake --build build --config Release --target hx_os_udp_packet
```

Visual Studio 构建运行 `build/bin/Release/hx_os_udp_packet.exe`，Linux 运行 `build/bin/hx_os_udp_packet`。程序给自己绑定的临时回环端口发送 JSON，再读取消息和网卡信息，输出类似：

```text
JSON: {"device_id":7,"state":"online"}
Destination: 127.0.0.1
Interface: <本机回环网卡索引>
```

不需要固定端口、另一个进程或外部服务。示例在接收前开启报文信息。使用元数据前先检查对应 flags；系统没有给出的字段不会被伪造。`info.local.port` 为零，需要端口时再单独查询 socket 的绑定地址。

例如设备同时有网线和 Wi-Fi，只知道“谁发来的”还不够，还需要知道“对方访问了本机哪个 IP”。接收元数据可以提供这个区别。下面指定回复的网卡，不改变 socket 的全局路由设置：

```c
hx_os_packet_info route = {0};
route.flags = HX_OS_PACKET_INTERFACE;
route.interface_index = received_info.interface_index;

status = hx_os_socket_send_packet_until(socket, &sender, &route,
	json, json_bytes, deadline, &sent, &error);
```

这里的 `received_info` 和 `sender` 来自上一条数据报的接收结果；仅在接收结果确实包含网卡信息时设置 `INTERFACE`。如果要指定单播来源 IP，还可以设置 `LOCAL_ADDRESS`；组播或广播目的地址不能原样用作回复的来源 IP。指定路由不等于身份认证，也不保证送达。C++ 可以使用 `<hx/Datagram.hpp>` 中借用 socket 的 `hx::Datagram`。

## 3. 截断、批量与分段缓冲

`LIMIT` 表示一条数据报已经被取走，但缓冲区放不下；剩余内容被系统丢弃。查看实际复制字节数和截断标志，再接收下一条消息。Windows 同时保留 `WSAEMSGSIZE` 原生错误。重试不能拿回上一条消息被丢弃的后半段。

批量发送返回首次失败前实际完成的数据报数量，只重试后面的项。批量接收等到第一条消息后，立即取出当前可取得的一批，不为了凑够 64 项一直等。如果最后一条被截断，这条已经消费的消息也计入完成数量，同时返回 `LIMIT`。

流式向量接口按顺序处理最多 64 段内存，不要求先拼成一块。`completed` 是跨缓冲区累计的字节数，不是完成了几个缓冲区。`sendv_until` 尝试发送完整内容，`receivev_until` 有数据就返回；超时后按实际字节进度继续。这些入口减少业务自己拼接数据的工作，不能只凭封装方式宣称吞吐量更高。

## 4. 安全修改已经接入 loop 的 socket

普通选项 setter 在 socket 接入 loop 后仍返回 `BUSY`。在 loop 所属线程上，先完成或取消该 socket 的在途操作，并处理其完成通知，再调用 `hx_os_loop_socket_configure_tcp` 或 `hx_os_loop_socket_multicast`。其他线程可提交普通 `POST`，在回调里完成这项操作。这些入口保留原生 loop 关联，会拒绝尚未退场的 I/O，不另建一套队列。

`hx_os_socket_set_buffers` 请求缓冲区大小，`get_buffers` 读取系统实际值；系统可能取整或翻倍。`set_tcp_user_timeout` 配置 Linux／Android 的未确认数据等待，不替代连接截止时间或业务心跳。`set_udp_icmp_errors` 控制 Windows UDP 的复位错误报告。平台无法提供的扩展返回 `NOT_SUPPORTED`，不会把名字相似的原生选项当成相同行为。

## 5. 接入现成的原生 socket 与收尾关闭

`hx_os_socket_import(network, &native, HX_OS_SOCKET_DUP, &socket, &error)` 为已经非阻塞的 IP socket 创建另一个句柄。`TAKE` 则只在成功后接管原句柄。复制后的句柄仍共享端点状态和选项，不是第二条独立连接。输入不能有正在进行的 I/O 或不兼容的事件循环注册。Windows 还要求调用者保证非阻塞模式和 OVERLAPPED 能力，因为 Winsock 没有只读查询非阻塞模式的接口。

`hx_os_socket_shutdown_drain_until` 明确发送 FIN，并丢弃剩余接收内容直到 EOF。只有确认这些内容不再需要时才调用。函数不关闭 socket；超时后保留同一个半关闭对象，可重试或直接关闭。EOF 和排空成功都不证明对端业务已完成。

## 6. 可信局域网 KCP 的 JSON 请求与回复

编译 [kcp_session.c](../examples/kcp_session.c)：

```sh
cmake --build build --config Release --target hx_os_kcp_session
```

输入地址允许带 `kcp://` 前缀。`hx_os_kcp_session_address` 返回包含监听器实际端口的 URI，可直接交给客户端连接；监听通配地址时，连接前换成服务端可达的 IP。

Linux 分别在两个终端运行：

```sh
# 服务端最多等待 30 秒，处理一条请求后结束。
build/bin/hx_os_kcp_session server 0.0.0.0:39031

# 同机客户端；另一台机器访问时改成服务端的局域网 IP。
build/bin/hx_os_kcp_session client 127.0.0.1:39031
```

Windows 使用 `build/bin/Release/hx_os_kcp_session.exe`，其余参数相同。服务端输出 `Request: {"type":"status","device_id":7}`，客户端输出 `Reply: {"accepted":true,"device_id":7}`。示例回复属于应用层确认，与传输层“进入对端队列”的确认不同。库不会修改防火墙规则。

一个端点拥有一个 I/O 线程，管理有上限的对端和消息队列。发送会复制消息，可以从应用线程调用；接收返回一条完整消息或连接事件，每条消息使用后释放一次。连接超时仍可能返回非空会话对象，应继续等待或带截止时间关闭。重连不自动重放之前的请求。

远程会话复用已定义的 [HXIP 会话封装和 KCP 消息协议](IPC.zh-CN.md)。另一种语言的对端要实现这两层，不能把原始 KCP 引擎报文直接当成会话报文。会话不提供加密或身份认证，只用于可信网络；原有 `hx_ipc_*` 工厂仍限制本机回环。需要证书验证和加密的流通信应使用 TLS。

## 7. 域名与平台边界

`hx_os_hostname_ascii` 将 UTF-8 DNS 名转换为原始 socket、TLS 和 DNS 入口所需的 ASCII 名称，不发起查询。非 ASCII 转换需要所选 libcurl 构建包含 IDN 支持；缺少时返回 `NOT_SUPPORTED` 并保留输出。采用后端的 IDNA 规则，不提供相似字符欺骗检测。

网卡索引和路由都是快照，不是永久身份或可达性判断。收到网卡变化通知后重新查询。Android 应用负责权限、Network 选择和 APK 生命周期，Android Network 句柄不是网卡索引。UDP／KCP 无法修复持续丢弃分片的防火墙或虚拟网络路径；控制数据报大小，并按业务需要加入稳定 ID、确认、去重和持久化。

## 8. 断线、处理变慢和退出时怎么处理

接口返回错误后，先判断“已经完成了多少”和“对象是否还由自己持有”，再决定重试或关闭。不要因为返回超时，就把整条业务消息重新发送一遍。

| 遇到的情况 | 库的行为 | 业务应当怎样继续 |
| --- | --- | --- |
| 对端停止或链路长时间没有应答 | IPC／KCP 按配置的空闲检测与重连策略报告断开，重连建立新会话；普通 socket 和 TLS 不自动重连 | 处理断开事件；新会话重新确认设备身份。旧请求不会自动重放，需要业务 ID 和对端去重规则决定是否重试 |
| DNS 服务器没有回复 | 显式 c-ares 查询支持截止时间和取消；默认系统 DNS 仍可能阻塞 | 对退出时限敏感的调用选择可取消入口。HTTP 还需匹配的 SDK；详见 [DNS](NETWORK_OPTIONS.zh-CN.md#123-事件循环和协程里怎么用)和 [HTTP 配置](HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时) |
| 接收方处理得慢 | IPC 消息队列有上限，可靠发送可能返回 `LIMIT`；TLS 发送可能超时并返回非零 `accepted` | 让消费者继续取消息并释放消息对象。TLS 在同一存活连接上只续传未接受的后缀，不重复提交前缀；若全部字节已被接受，可继续 `flush_until` |
| 频繁连接、断开 | 消息、请求、连接分别有自己的所有者；旧 peer ID 不能用于新会话 | 每条消息释放一次；结束的请求释放；客户端关闭成功后才丢掉所有者。TLS 服务端按需关闭／中止已接纳 peer，给后续连接释放槽位 |
| 退出时仍在等待或重连退避 | 关闭会停止新工作并唤醒相关等待；TLS 正常关闭还会尝试排空已接受数据 | 使用一个明确截止时间；检查返回值和所有者指针。仍非空时保留对象、分配器及上下文以便重试。确实允许丢弃时，再显式中止；不强杀工作线程 |

例如 TLS 一次发送 8 MiB，返回超时且 `accepted` 为 64 KiB，表示这 64 KiB 已经由库接管。接收方恢复读取后，应从剩余位置续传；它仍不证明对端业务已经处理了这部分数据。IPC／KCP 的整条消息确认也只代表约定的通信层结果，平台入库或设备动作需要应用层回复。

重连测试应区分“对端明确关闭”与“报文被静默丢弃”。前者可能立即得到 EOF，后者需要等待空闲检测；断线检测时间不能直接当作资源关闭时间。恢复链路后，也不能把新会话当作旧请求的继续执行。

验证自己的部署时，复用 [TCP 业务示例](TCP_BUSINESS.zh-CN.md)、[TLS 示例](TLS.zh-CN.md)、[KCP 示例](#6-可信局域网-kcp-的-json-请求与回复)和 [HTTP 文件示例](HTTP_FILES.zh-CN.md)，分别检查接收变慢、对端重启和关闭重试。模拟器 shell、ARM 用户态 QEMU、实体设备与实际 APK 是不同环境：QEMU 缺少某个 socket 选项、Android 拒绝 Unix socket，或者 WSL mirrored 丢弃 IPv4 分片时，应明确记录未通过的路径，不能用其他传输成功代替。主机的 sanitizer 结果也不能替代目标设备验收。
