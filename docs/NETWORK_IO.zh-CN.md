# 常用网络收发：UDP 网卡路由、批量/向量 I/O、双栈拨号与局域网 KCP

[English](NETWORK_IO.md) · [网络选项](NETWORK_OPTIONS.zh-CN.md) · [安装包接入](INSTALL.zh-CN.md)

在实际网络编程中，除了基础的 Socket 字节读写，工程往往还会遇到更具体的需求：多网卡设备需要知道 UDP 包是从哪张网卡收到的并原路回复、高频服务希望一次系统调用批量收发多条数据报或免拷贝拼接消息头与正文、客户端希望按域名同时尝试 IPv4/IPv6 连接，或者在可信局域网的多台机器之间建立可靠 KCP 会话。本文汇总了这些进阶网络 I/O 入口的用法、可运行示例与异常恢复准则。

## 1. 按需要选择入口

| 你要做什么 | 调用入口 | 由调用方持有的资源 |
| --- | --- | --- |
| 等待接收一条普通 UDP 数据报 | `hx_os_socket_receive_from_until` | `hx_os_socket` 句柄与接收缓冲区 |
| 知道 UDP 数据报从哪张网卡进入、发往本机哪个目的 IP | `hx_os_socket_receive_packet_until` | `hx_os_socket`、接收缓冲区及输出的 `hx_os_packet_info` |
| 指定本次 UDP 回复从哪张网卡或哪个源 IP 发出 | `hx_os_socket_send_packet_until` | 可选的单包路由参数 `hx_os_packet_info` |
| 单次调用批量发送或接收多条 UDP 数据报 | `hx_os_socket_send_batch_until` / `receive_batch_until` | 最多 64 项报文描述符数组及其缓冲区 |
| 不合并内存，直接按序发送分散的头部与正文（Scatter/Gather） | `hx_os_socket_sendv_until` / `receivev_until` | 按顺序借用的最多 64 段缓冲区数组 |
| 按主机名发起 TCP 连接，自动交替尝试 IPv4 / IPv6（Happy Eyeballs） | `hx_os_tcp_dial_until` | `hx_os_network` 租约与连接成功后返回的 `hx_os_socket` |
| 支持随时取消与截止时间的异步 DNS 解析 | `hx_os_resolver_resolve_until` | 显式创建的 `hx_os_resolver`（基于 c-ares）及返回的地址列表 |
| 在可信局域网的多台机器之间收发可靠 KCP 完整消息 | `hx_os_kcp_session_*` | 会话端点句柄（`hx_os_kcp_session`）与收到的消息对象 |

所有带 `_until` 后缀的接口统一接收本库的**单调时钟绝对截止时间（Monotonic Deadline）**。建议在一次操作开始前用 `hx_os_deadline_after_ms(...)` 计算一次截止时间，并在后续步骤中复用。
- 对 UDP 接口传入 `HX_OS_DEADLINE_NOW` 时，函数会先尝试执行一次非阻塞收发；若底层无就绪数据需要等待，则立即返回 `HX_OS_TIMEOUT`。
- **零字节 UDP 数据报**也是合法的独立消息。
- 原始 TCP Socket 始终是无消息边界的字节流；若需在 TCP 上按条收发完整消息，请搭配[消息帧（Frames）模块](FRAMES.zh-CN.md)。

## 2. 先跑通 UDP 数据报示例

在已有构建目录中编译 [udp_packet.c](../examples/udp_packet.c)：

```sh
cmake --build build --config Release --target hx_os_udp_packet
```

Windows（Visual Studio 生成器）下运行 `build/bin/Release/hx_os_udp_packet.exe`，Linux 下运行 `build/bin/hx_os_udp_packet`。该程序不需要指定固定端口、不需要启动第二个进程，也不依赖外部网络服务——它会向自己绑定的本机回环临时端口发送一条 JSON 数据报，接着读取该报文及其控制元数据（Ancillary Data），输出如下：

```text
JSON: {"device_id":7,"state":"online"}
Destination: 127.0.0.1
Interface: <本机回环网卡索引>
```

### 按收包网卡原路回复（多宿主设备场景）

当一台网关或工控机同时插着有线网口和 Wi-Fi 时，仅仅通过 `receive_from` 拿到对端源地址是不够的，你往往还需要知道“对方访问的是本机的哪一个 IP、从哪张物理网卡进来的”，并在回复时强制从同一张网卡发出，而不必修改 Socket 的全局默认路由。

在开启了报文元数据选项（`hx_os_socket_set_packet_info`）并收到上一条报文后，可以先检查 `received_info.flags`，确认包含网卡信息后再构造单包路由参数回发：

```c
hx_os_packet_info route = {0};
route.flags = HX_OS_PACKET_INTERFACE;
route.interface_index = received_info.interface_index;

status = hx_os_socket_send_packet_until(socket, &sender, &route,
	json, json_bytes, deadline, &sent, &error);
```

- **读取与设置前先查 `flags`**：操作系统未提供的元数据字段不会被库伪造；另外 `info.local.port` 固定为 `0`（若需知道本地绑定端口，请单独查询 Socket 绑定地址）。
- **指定单播源 IP**：若还需固定回复包的源 IP，可在 `route.flags` 中同时置位 `HX_OS_PACKET_LOCAL_ADDRESS` 并填入单播本地地址。**切勿将收包时的组播（Multicast）或广播（Broadcast）目的地址原样填作回复包的源 IP。**
- **C++11 封装**：C++ 代码可直接包含 `<hx/Datagram.hpp>`，使用借用底层 Socket 的 `hx::Datagram` 包装类完成同样操作。

## 3. 截断、批量与分段缓冲

- **UDP 截断（`HX_OS_LIMIT`）**：当接收缓冲区小于到达的 UDP 数据报时，接收函数返回 `HX_OS_LIMIT`。注意此时**该条数据报已从内核队列中被消费取走，超出缓冲区的尾部字节已被操作系统永久丢弃**（Windows 下同时在 `hx_os_error` 中保留 `WSAEMSGSIZE`）。调用方应查看实际拷贝的字节数与截断标志，按需丢弃该残包并继续接收下一条报文——重新调用接收函数不可能拿回上一条报文被截掉的后半段。
- **批量收发（Batch I/O）**：
  - `hx_os_socket_send_batch_until()` 返回遇到首个错误前**已成功发出的完整数据报条数**；重试时只需提交剩余未发出的条目。
  - `hx_os_socket_receive_batch_until()` 会阻塞等待直到**至少收到第 1 条数据报**，随后立即把当前内核缓冲区中已就绪的一批报文全部取出返回（最多 64 条），绝不会为了硬凑满 64 条而继续傻等。如果本批最后一条报文发生截断，该条已消费的报文同样计入完成数量，同时函数返回 `HX_OS_LIMIT`。
- **分散/聚集向量 I/O（Scatter/Gather Vectors）**：
  - `hx_os_socket_sendv_until()` 与 `hx_os_socket_receivev_until()` 按顺序处理最多 64 段非连续内存缓冲区，免去了应用层先分配大块内存把帧头和载荷拼在一起的拷贝开销。
  - 输出参数 `completed` 统计的是**跨所有缓冲区的累计完成总字节数**，而不是缓冲区段数。`sendv_until` 会持续发送直到全部字节写完或超时；`receivev_until` 则在读到任意正数字节后即可返回。若中途超时，调用方应根据 `completed` 推进字节偏移后再续传。

## 4. 安全修改已经接入 loop 的 socket

一旦某个 `hx_os_socket` 已经绑定到 `hx_os_loop` 事件循环上，直接调用普通的 `hx_os_socket_set_*` 配置接口都会返回 `HX_OS_BUSY`。

如果你需要在运行期修改已接入 Loop 的 Socket 参数：
1. 在该 Loop 所属的工作线程上，先等待或取消该 Socket 上挂起的在途异步操作，并消费完它们的完成回调。
2. 随后调用专用的 `hx_os_loop_socket_configure_tcp()` 或 `hx_os_loop_socket_multicast()`（如果从其他线程发起，可通过普通的 Loop `POST` 任务投递到 Loop 线程执行）。这两个入口会保留原生的完成端口/epoll 绑定关系，同时严格拦截仍有未退场 I/O 的修改请求。

> [!NOTE]
> **平台特定 Socket 选项说明**
> - `hx_os_socket_set_buffers()` 用于请求内核收发缓冲区容量，而 `hx_os_socket_get_buffers()` 读回操作系统的实际生效值（系统可能会对齐取整或像 Linux 一样按两倍记账）。
> - `hx_os_socket_set_tcp_user_timeout()` 用于配置 Linux / Android 下已发送但未获 ACK 数据的超时时间（`TCP_USER_TIMEOUT`），它不能替代连接建立截止时间或应用层心跳。
> - `hx_os_socket_set_udp_icmp_errors()` 用于控制 Windows UDP Socket 在收到对端 ICMP 端口不可达时是否向上报告连接复位错误（`SIO_UDP_CONNRESET`）。
> - 当当前平台不支持某项扩展时，接口会明确返回 `HX_OS_NOT_SUPPORTED`，绝不会拿名称相似但语义不同的系统选项凑数。

## 5. 接入现成的原生 socket 与收尾关闭

- **导入外部原生 Socket（`hx_os_socket_import`）**：
  - 传入 `HX_OS_SOCKET_DUP` 会为已处于非阻塞模式的原生 IP Socket 复制（Duplicate）出一个新的系统句柄并包装为 `hx_os_socket`（注意复制出的句柄与原句柄共享底层连接状态与内核选项，并非第二条独立连接）。
  - 传入 `HX_OS_SOCKET_TAKE` 则在导入成功后直接接管调用方传入的原生句柄所有权（若导入失败，原句柄仍由调用方保留）。
  - 导入前必须确保该 Socket 上没有正在进行的 I/O 操作，也未注册到不兼容的外部事件循环中。在 Windows 上，由于 Winsock 没有提供只读查询非阻塞标志的 API，调用方必须自行确保传入的 Socket 已开启非阻塞模式且具备 `WSA_FLAG_OVERLAPPED` 属性。
- **发送 FIN 并排空接收缓冲（`hx_os_socket_shutdown_drain_until`）**：
  - 该函数会显式向对端发送 TCP `FIN`（关闭写方向），并在给定截止时间内读取并丢弃对端发来的剩余字节，直到读到 `HX_OS_EOF`。
  - 仅在确认不再需要后续接收数据时才调用它。该函数**不会关闭 Socket 句柄本身**；若等待超时，原对象仍保持半关闭状态，可供继续重试排空或直接调用 `hx_os_socket_close()` 关闭。注意：收到 `HX_OS_EOF` 或排空成功仅代表传输层流结束，不代表对端业务落盘已完成。

## 6. 可信局域网 KCP 的 JSON 请求与回复

前面的 `hx_ipc` 仅允许本机回环通信。如果你需要在**可信局域网的多台机器之间**直接收发带自动重传、心跳与请求匹配能力的 KCP 消息，可以使用 `hx_os_kcp_session_*` 接口。

先在构建目录中编译 [kcp_session.c](../examples/kcp_session.c)：

```sh
cmake --build build --config Release --target hx_os_kcp_session
```

传入的地址字符串支持可选的 `kcp://` 前缀。服务端绑定端口 `0` 时，可通过 `hx_os_kcp_session_address()` 查询包含实际监听端口的完整 URI（若服务端绑定了 `0.0.0.0` 通配地址，客户端连接前需将其替换为服务端的局域网可达 IP）。

**Linux 下在两个终端中分别运行：**

```sh
# 服务端：最多等待 30 秒，处理完 1 条请求后自动退出
build/bin/hx_os_kcp_session server 0.0.0.0:39031

# 客户端：同机测试连 127.0.0.1；跨机器测试时改为服务端的局域网 IP
build/bin/hx_os_kcp_session client 127.0.0.1:39031
```

（Windows 下将程序路径改为 `build/bin/Release/hx_os_kcp_session.exe`，参数完全一致。）

运行后：
- 服务端打印：`Request: {"type":"status","device_id":7}`
- 客户端打印：`Reply: {"accepted":true,"device_id":7}`

### 会话生命周期与安全边界

- **线程与内存模型**：每个 `hx_os_kcp_session` 端点内部拥有 1 个后台 I/O 线程，负责管理有界数量的对端会话与消息队列。发送接口会拷贝输入消息，支持从任意业务线程直接调用；接收接口每次返回一条完整消息或连接事件，每条消息用完后必须释放一次。若连接等待超时，函数仍可能返回非空的会话对象——请继续等待或带截止时间关闭它，切勿直接丢弃指针。断线重连后不会自动重放旧请求。
- **跨语言线协议兼容**：局域网 KCP 会话完全复用 [HXIP v1 会话封装与 KCP 消息模式协议](IPC.zh-CN.md#8-给其他语言实现的-hxip-v1-协议)。如果用其他语言编写对端，必须同时实现外层 64 字节 HXIP 会话头与内层 KCP 消息模式，不能拿裸 KCP 数据包直接互通。
- **仅限可信内网**：`hx_os_kcp_session` 不提供加密或密码学身份认证，仅适用于受控的可信局域网（且本库不会替你修改系统防火墙规则）。如果跨公网或需要证书校验与加密传输，请使用 [TLS 模块](TLS.zh-CN.md)。

## 7. 域名与平台边界

- **国际化域名（IDN）转 ASCII（`hx_os_hostname_ascii`）**：将包含非 ASCII 字符的 UTF-8 域名转换为原始 Socket、TLS 和 DNS 接口所需的 Punycode ASCII 域名（纯本地转换，不发起网络查询）。非 ASCII 转换要求当前链接的 `libcurl` 构建包含 IDN 支持；若未包含则返回 `HX_OS_NOT_SUPPORTED` 并保持输出缓冲区不变。注意该函数仅执行后端的 IDNA 编码规则，不提供视觉相似字符（Homograph）防钓鱼检测。
- **网卡索引与移动平台限制**：网卡索引（Interface Index）和路由查询结果都是当前时刻的瞬态快照，不能当作设备的永久身份标识；收到网络变化通知后应重新查询。在 Android 平台上，应用需自行管理权限、Android `Network` 句柄绑定与 APK 生命周期（注意 Android 的 `Network` 句柄不是操作系统网卡索引）。

## 8. 断线、处理变慢和退出时怎么处理

当任何网络接口返回错误或超时时，第一步永远是确认**“底层已经完成了多少字节/条数”**以及**“句柄所有权是否仍在自己手中”**，然后再决定续传、重试还是关闭——**切勿一看到 `HX_OS_TIMEOUT` 就无脑把整条业务消息从头重发一遍。**

| 运行时异常场景 | 库的确定性行为 | 应用层正确的处理步骤 |
| --- | --- | --- |
| 对端宕机或中间链路静默丢包 | `hx_ipc` / `hx_os_kcp_session` 按配置的空闲超时报告断开，并在重连成功后生成全新会话 ID；原始 Socket 与 TLS 不做自动重连 | 处理断开事件并在新会话中重新握手确认设备身份；旧请求不会自动重放，需结合业务流水号与对端幂等去重规则决定是否重发 |
| DNS 服务器无响应 | 显式 `hx_os_resolver`（c-ares）支持截止时间与跨线程取消；系统默认 `getaddrinfo` 仍可能阻塞 | 对退出延迟敏感的场景请选用可取消的解析入口（详见 [DNS 解析指南](NETWORK_OPTIONS.zh-CN.md#123-事件循环和协程里怎么用) 与 [HTTP 取消配置](HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时)） |
| 接收方消费过慢（背压） | IPC 可靠发送队列满时返回 `HX_OS_LIMIT`；TLS 发送超时时仍可能通过 `accepted` 参数报告已接管的前缀字节数 | 让消费端持续取走并释放消息；对于同一条存活的 TLS 连接，超时后**只续传未被 `accepted` 的剩余尾部字节**（若全部字节均已被接管，直接调用 `flush_until` 即可） |
| 频繁建连与断连循环 | 消息、请求、连接各自拥有独立生命周期；旧会话的 `peer` 编号不能用于新会话 | 每条消息和请求结构体用完释放一次；客户端直到关闭成功后再丢弃句柄；TLS 服务端处理完一个客户端后及时关闭/中止该 peer 以腾出连接槽位 |
| 进程退出时仍有等待或重连退避 | 关闭操作会拒绝新请求并立即唤醒挂起的等待调用；TLS 正常关闭还会尝试排空已接管的待发数据 | 传入明确的截止时间并同时检查返回状态与句柄指针：若超时后指针仍非空，必须保留对象、自定义分配器及上下文以便重试关闭（仅在允许丢弃数据时才调用显式 `abort`，绝不强杀后台线程） |

> [!TIP]
> **为什么 TLS 超时后不能重发已 `accepted` 的字节？**
> 举例来说，当你调用 TLS 发送 8 MiB 数据时，如果函数因超时返回 `HX_OS_TIMEOUT`，但输出参数 `accepted` 为 `64 KiB`，说明前 64 KiB 明文已经被加密并交由 TLS 会话内部缓冲区接管。一旦对端恢复读取，你应当从 `64 KiB` 偏移处继续提交剩余的 `8 MiB - 64 KiB`，否则前 64 KiB 会在流中被重复发送两次！当然，无论是 TLS 的 `accepted` 还是 IPC/KCP 的通信层 ACK，都只代表通信层收妥，数据库入库或硬件动作是否完成仍需由应用层回复确认。

在部署到目标环境前，建议直接使用仓库自带的 [TCP 业务示例](TCP_BUSINESS.zh-CN.md)、[TLS 示例](TLS.zh-CN.md)、[可信局域网 KCP 示例](#6-可信局域网-kcp-的-json-请求与回复) 和 [HTTP 文件传输示例](HTTP_FILES.zh-CN.md) 进行慢速消费、对端重启与关闭重试验证。请注意区分宿主机模拟器、ARM 用户态 QEMU、实体开发板与正式签名 APK 的环境差异——如果某个环境因 QEMU 缺失特定 `setsockopt`、Android 沙箱拦截 Unix Socket 或 WSL2 镜像网络丢弃 IPv4 UDP 分片而受阻，应按实测如实记录并选用匹配的传输方式。
