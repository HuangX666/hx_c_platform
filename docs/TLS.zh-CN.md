# TLS 加密连接与分线程收发 JSON

[English](TLS.md) · [README](../README.zh-CN.md) · [数据加密](CRYPTO.zh-CN.md)

下方命令复用[已配置的示例目录](../examples/README.zh-CN.md#run)，build 与 Release 按自己的配置替换。先准备与 localhost 匹配的演示证书，再运行两端；仅有加密库并不能替代证书和信任配置。

## 1. 构建并运行完整示例

本例让一个发送线程和一个接收线程共用 TLS 连接：客户端发送三条温度读数，服务端回复三条确认，并对第二条高温数据主动发送告警。你会看到业务数据、匹配编号和主动通知，而不是只看到“连接成功”。

这里的 TLS 是 TCP 上的传输层安全协议；线程局部存储使用另一组 `hx_os_tls_key` 接口。对端可以使用其他语言的标准 TLS 1.2／1.3 实现，并遵守本例的消息帧格式。

### 1.1 复用已有构建目录

先按[依赖指南](DEPENDENCIES.zh-CN.md#4-构建依赖)准备 SDK。`HX_OS_ENABLE_TLS` 依赖 CRYPTO、NETWORK、LOOP、FILE_UTILS；本例的 JSON 还需要 CONFIG、CONFIG_SERIALIZE、SETTINGS。这些模块默认开启。如果沿用首页的精简配置，需先给该构建配置好 `HX_OS_DEPENDENCY_PREFIX`，并显式重新开启 CRYPTO 和 TLS；仅初始化子模块不能替代 SDK 构建。

关闭必要模块后，`hx_os_tls_available()` 返回 0，操作返回 `HX_OS_NOT_SUPPORTED`。仅启用模块不会在程序加载时启动线程，创建连接或监听器时才创建所需资源。

下文全部在库源码根目录执行。`build` 是已有的构建目录，Windows 和 WSL 各用各自的目录。Debug 构建将 Release 同时替换为 Debug；无需新建 TLS 构建目录。

首次为精简配置接入 SDK 时，把这三个参数加在下一条 `cmake -S` 命令后面，或在 cmake-gui 中填写同名配置；之后不用每次重复：

| 参数 | 填什么 |
| --- | --- |
| `-DHX_OS_DEPENDENCY_PREFIX=...` | 已编译 SDK 的 prefix 绝对路径，例如 `E:/sdk/hx/prefix`，不是 deps 源码路径 |
| `-DHX_OS_ENABLE_CRYPTO=ON` | 开启 OpenSSL 支持 |
| `-DHX_OS_ENABLE_TLS=ON` | 开启 TLS 连接接口 |

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_tls_json_server
cmake --build build --config Release --target hx_os_tls_json_client
```

### 1.2 准备本机演示证书

下面用 OpenSSL 命令行生成一天有效的自签名证书；文件放进已有 build 目录，不写入系统信任库。`openssl` 需在 PATH 中，或替换为 SDK 内的实际可执行文件，例如 PowerShell 的 `& .\build-deps\prefix\bin\openssl.exe`。已经有证书时跳过生成，用自己的证书、私钥和 CA 路径运行第 1.3 节。以下文件名专供演示，重新生成会覆盖同名文件。

Windows PowerShell（反引号续行，不能在反引号后加空格）：

```powershell
openssl req -x509 -newkey rsa:2048 -nodes -days 1 `
    -subj /CN=localhost `
    -addext subjectAltName=DNS:localhost,IP:127.0.0.1 `
    -keyout build/demo-key.pem -out build/demo-cert.pem
```

Linux / WSL:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -days 1 \
  -subj /CN=localhost \
  -addext subjectAltName=DNS:localhost,IP:127.0.0.1 \
  -keyout build/demo-key.pem -out build/demo-cert.pem
```

### 1.3 启动两端并核对结果

仍在库源码根目录运行，不要 cd 到证书目录。运行器自动选择空闲端口、启动服务端、等待监听就绪，再启动客户端；Python 只用于运行器，两个 C 程序本身不依赖它。

Windows Visual Studio：

```powershell
python examples/run_tls_business.py --bin-dir build/bin/Release `
    --certificate build/demo-cert.pem --private-key build/demo-key.pem `
    --ca build/demo-cert.pem
```

Linux / WSL:

```sh
python3 examples/run_tls_business.py --bin-dir build/bin \
  --certificate build/demo-cert.pem --private-key build/demo-key.pem \
  --ca build/demo-cert.pem
```

成功输出中包含以下两行，`[TLS receive]` 行展示实际 JSON。三条读数是 20、81、22，设备名是 `温度传感器🌐`：

```text
TLS CLIENT PASS: 3 JSON readings, 3 replies, 1 alarm
TLS SERVER PASS: 3 JSON readings processed
```

要观察两个窗口中的收发，可以不用 Python。Windows 在终端一运行：

```powershell
.\build\bin\Release\hx_os_tls_json_server.exe 39004 `
    build/demo-cert.pem build/demo-key.pem
```

等它输出 `Listening:`，在终端二运行：

```powershell
.\build\bin\Release\hx_os_tls_json_client.exe 39004 build/demo-cert.pem
```

Linux／WSL 两个终端对应的命令为：

```sh
./build/bin/hx_os_tls_json_server 39004 build/demo-cert.pem build/demo-key.pem
./build/bin/hx_os_tls_json_client 39004 build/demo-cert.pem
```

客户端连接 127.0.0.1，验证证书里的 localhost；自签名证书同时作为本次连接显式信任的 CA。证书验证失败时检查 CA 路径、SAN、有效期和本机时间，不要关闭验证。提示缺少 DLL 时检查所选配置的 bin 目录及匹配 SDK 的运行库，见[安装包部署](USAGE.zh-CN.md#installed-project)。

### 1.4 改成自己的业务

完整源码是 [tls_json_client.c](../examples/tls_json_client.c) 和 [tls_json_server.c](../examples/tls_json_server.c)。复制到自己的工程时还需要同目录的 tls_json.h、network_demo.h、business_json.h；每个程序分别作为一个 executable。它们链接同一个 C 库，不需要把 SDK 的内部初始化搬进业务代码。

先替换温度字段和告警处理，保留线程退出与关闭顺序。TLS 加密的是字节流，**不会自动识别 JSON 边界**；本例 tls_json.h 使用“4 字节大端正文字节数 + UTF-8 JSON”，对端也必须这样处理。它与 TCP 订单例子的 2 字节长度帧、IPC 的 HXIP 协议不同；不能只改端口就互通。

C 接口接收 UTF-8 路径；Windows 应用若接收非 ASCII 命令行路径，仍需将自己的原生命令行转为 UTF-8。本页证书文件名为 ASCII。生产环境使用正式的证书和密钥管理方式；演示文件用完可以删除。

## 2. C 和 C++ 怎样直接调用

主要步骤就是连接／监听、接受连接、发送、接收和显式关闭。C 调用者不需要自己准备 OpenSSL context、network、socket、loop、编码转换器或内部工作线程。

```c
hx_os_tls *connection = NULL;
hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
hx_os_error error;

options.ca_file = "ca.pem";
hx_os_status status = hx_os_tls_connect(
	"localhost", 4433, &options, &connection, &error);

/**
 * \brief Use connection only after checking status.
 *
 * \details On timeout/failure, an already accepted startup still leaves an
 * owner that must be closed or aborted.
 */
```

希望创建工作线程后立即返回时，用 `hx_os_tls_start_connect`／`start_listen`，随后通过 `wait_ready_until` 等待。`connect_until`／`listen_until` 封装这两个步骤，接收单调时钟截止时间；简单入口默认等待五秒。工作线程创建前的失败保持 `*out=NULL`，接受启动后的失败则保留对象。原生 DNS、证书处理和文件访问无法被强制打断。

不使用异常的 [C++11 包装](../include/hx/Tls.hpp)与 IPC 包装一样，借用由宿主持有的指针：

```cpp
hx_os_tls *owner = NULL;
hx::Tls connection(owner);
hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
options.ca_file = "ca.pem";

auto opened = connection.connect("localhost", 4433, &options);
if (opened) {
	const char data[] = "hello";
	auto sent = connection.send(data, sizeof(data) - 1);
	// sent.value() is the accepted byte count, including on failure.
}

auto closed = connection.closeUntil(hx_os_deadline_after_ms(2000));
/**
 * \brief If owner is still non-NULL, keep it alive and retry, or explicitly
 * abort.
 *
 * \details The facade destructor does not close, kill or detach its worker.
 */
```

上面是聚焦调用方式的片段。带完整检查的程序见业务示例。宿主不能在仍持有端点时直接让所有者离开作用域；调用失败不会自动销毁这个端点。

## 3. 同一连接怎样分线程发送和接收

```mermaid
flowchart LR
    S[业务发送线程] --> T[有界明文发送队列]
    T --> W[端点的一个 I/O 线程]
    W <--> N[原生 TCP 事件循环]
    N <--> P[其他标准 TLS 对端]
    W --> R[有界且已经验证的明文队列]
    R --> A[业务接收线程]
```

只有内部 I/O 线程操作该端点的 OpenSSL 连接对象。业务调用只在操作队列和等待条件时短暂持锁，原生 I/O 和分配器回调在锁外执行。多个发送者按完整调用串行进入，同时进行的接收独立等待；同一 peer 的第二个接收者返回 `BUSY`。不承诺 FIFO 公平性。

每个端点拥有一个工作线程。服务端在这个线程里处理有数量上限的连接，不为每个连接新建线程。事件循环复用现有 IOCP／epoll／kqueue 适配。OpenSSL 使用有界 BIO pair，避免把 Windows `SOCKET` 缩窄为 `int`，也避免直接写 POSIX 描述符而触发 SIGPIPE。

默认服务端最多 16 个 peer，每个 peer 的发送／接收明文队列各 64 KiB；客户端只有一个 peer。队列容量范围为 16 KiB～16 MiB，peer 数量为 1～1024，组合后须满足 256 MiB 的 peer 存储上限。每个存活 peer 另有四个 16 KiB 记录缓冲、两侧各 32 KiB 的 BIO、元数据、OpenSSL 内部分配和系统 socket 缓冲。未完成 loop 请求和 flush 标记同样有上限。接纳连接时才分配记录缓冲，空闲槽位不提前分配。OpenSSL 内存不受 hx 分配器预算约束。既有非 TLS 热路径保持不变；TLS 自身会复制排队数据、持锁并分配原生／请求对象，不能称为零分配，也不把这些设计当成吞吐提升证据。

`context=NULL` 采用不可变的默认分配器。自定义分配器必须线程安全、支持跨线程释放，并活到端点回收。内部 TLS 工作线程或回收过程重入阻塞 TLS 接口时返回 `DEADLOCK`；分配器回调不应管理这些对象的生命周期。这些同步业务调用会阻塞调用线程，不能当作协程挂起接口使用。

## 4. 字节、消息、部分完成与取消

TLS 提供字节流，一次读取不保证对应一条 JSON。示例的[小型分帧辅助](../examples/tls_json.h)使用“四字节大端长度 + UTF-8 JSON”，单条文档最多 1023 字节，收齐头部和正文后才解析。它与 IPC／TCP／UDP／KCP 的业务逻辑分开。其他语言只需实现这个示例的分帧格式，不需要接入 hx C ABI。

| 操作 | 返回含义与调用责任 |
| --- | --- |
| `send_until` | 把已接受明文复制到有界自有缓冲。成功包含把对应密文字节交给操作系统，不代表对端业务已确认。超时／错误时 `*accepted` 仍有效，只重试未接受的后缀。 |
| `flush_until` | 只等待调用瞬间已经接受的字节；后续发送不延长本次等待，也不表示对端处理完成。 |
| `receive_until` | 返回部分已经认证的明文及数量，或明确的错误、超时、EOF。输出缓冲由调用方持有。 |
| `receive_exact_until` | 在一个截止时间内收齐指定数量；失败保留已收到字节数，提前出现正常 EOF 时返回 `TRUNCATED_INPUT`。 |
| `cancel_waits` | 唤醒当前业务等待并报告取消；不撤回已接受数据，也不关闭连接，新调用可以继续。完成与取消竞争时，已经完成的操作可以获胜。 |
| `get_peer_info` | 复制协商版本、密码套件、对端地址、证书 SHA-256 指纹及验证标志，不暴露 OpenSSL 原生指针。 |

缓冲中已经通过认证的数据先交给调用方，再返回最终读取错误。只有收到 TLS `close_notify` 才报告普通 EOF；TCP 突然 EOF 报告 `TRUNCATED_INPUT`，连接复位则可以保留原生传输错误。`NOW` 立即检查，`INFINITE` 必须显式传入。截止时间到达不代表原生操作或远端业务已经撤销。

已接受的数据可能在发送超时后继续发出。如果失败前只接受了一部分业务帧，调用者应继续保持发送排他性，补齐后缀；或者放弃这条连接。不能让另一个生产者把其他消息插入这个前缀和后缀之间。有限时长的示例选择在出现部分帧失败时结束该会话。

服务端 `accept_until` 对每个成功握手的 peer 只返回一次。peer ID 是该端点内不重复使用的不透明标识；客户端用 `0` 指定唯一 peer。它不是在线路上传输的设备号、请求号或跨重启会话号，示例在 JSON 里携带请求／设备字段。已接纳 peer 一直可查询，直到显式关闭／中止或端点关闭。失败且未通知业务的握手会回收；存活 peer 表满时暂停接受新连接。不隐含自动重连和业务空闲超时策略。

## 5. 证书验证、信任来源与发布

证书失败保留原生 OpenSSL 错误，并通过 `error.cause_code` 保留 X.509 验证结果。客户端始终验证服务端证书链及 DNS／IP Subject Alternative Name。DNS／Punycode 名称使用 SNI，并拒绝部分通配符；数字 IP 使用 IP 验证。不自动做 IDNA 转换，也不回退到 Common Name。`server_name` 改变被验证的身份，不改变实际连接地址。TCP 失败时，在同一个连接／握手截止时间内顺序尝试解析得到的地址；证书失败不会回退到不验证模式。

提供 `ca_file` 时替换默认信任来源。服务端必须给出证书链和私钥 PEM 路径；客户端也可同时提供二者用于双向认证。服务端只有显式选择 `HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE` 才强制验证客户端证书。证书链上限为 1 MiB／64 项，CA 包为 4 MiB／4096 项，私钥为 64 KiB，UTF-8 密码为 1024 字节。加密 PEM 使用传入密码，不弹出交互式密码输入。相对路径在创建时根据当前工作目录固定，文件内容需要保持稳定直到启动完成。Windows 路径通过 hx 的 UTF-8 文件接口打开，再从内存解析；私钥临时缓冲的自有清理路径会擦除内容。

默认信任适配使用 Windows ROOT 存储、常见 Linux／BSD 系统 CA 包位置、macOS 系统信任锚，或 Android system／Conscrypt 证书目录。信任来源不可用时明确失败，不会静默关闭验证。没有系统 CA 包的嵌入式设备可传入明确的 CA 文件。非 macOS 的 Apple 默认信任适配返回 `NOT_SUPPORTED`，需要显式提供 CA。当前 Android 模拟器证据和动态库部署见 [Android 说明](ANDROID.zh-CN.md)；不同系统／API／应用沙箱仍需各自验收，macOS 验证暂缓。

验证引擎采用 OpenSSL X.509 策略，不复刻 Windows 证书扩展以外的存储属性，也不复刻 macOS Keychain 专有策略覆盖。本模块不自动下载 AIA，不提供吊销／OCSP 服务、证书固定策略或系统 UI。指纹供应用制定策略，不会自动改变信任结果。TLS 1.3 客户端握手完成本身不能证明服务端已接受客户端证书，后续 alert 仍可能报告拒绝。

只启用 TLS 1.2／1.3、现代认证密码套件和 OpenSSL 安全级别 2。不启用重新协商、会话恢复、early data、DTLS 或 QUIC；原始 TLS 也不包含 HTTP、JSON 请求路由和业务授权。

OpenSSL 继续默认动态链接。构建及安装会包含选定的 SSL 和 Crypto 两个运行库，静态核心消费者也需要对应 SDK 包；仍可显式选择 `HX_OS_OPENSSL_STATIC=ON`。端点的私有 context 只在所属工作线程使用，该线程退出前完成原生线程清理。卸载本库或 OpenSSL 前，停止业务调用、回收端点并等待工作线程真正退出。外部原始加密调用者仍遵守单独的[静态链接清理契约](CRYPTO.zh-CN.md#5-线程内存与插件卸载)。

## 6. 关闭、重试与示例

`close_until` 停止接受新业务，排空已接受的发送数据，交换 `close_notify`。显式关闭会丢弃未读取的业务数据。不配合的对端可以延迟正常关闭；到达截止时间时保留同一个所有者并继续关闭，可以重试，或者明确选择 `abort_until` 丢弃队列、取消传输请求，再等待它们真正退出。两者都不强杀或分离线程。原生错误也可能与“所有者已经变为 NULL”同时返回，应同时检查状态与所有权。

销毁端点前停止发起新调用，已经在途的调用会被唤醒并退出，同一时刻只允许一个关闭者。`peer_close_until`／`peer_abort_until` 对一个服务端 peer 应用相同规则。端点仍被持有时，分配器、其 user 数据以及库代码都必须保持有效。

目标平台的编译、部署要求及限制见[平台支持](PLATFORMS.zh-CN.md)。公开示例独立于维护者的本地回归。
