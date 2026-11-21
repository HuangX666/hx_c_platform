# 常用数据与 I/O 工具

[English](UTILITY_EXTENSIONS.md)

本页涵盖七项互不耦合的实用扩展模块：**内存帧压缩**、**URL 解析与查询参数**、**命令行参数解析**、**HTTP/HTTPS 客户端**、**安全临时文件与目录**、**串口字节收发**以及**跨进程命名共享内存**。每个模块都可以单独按需调用，不需要预先执行统一的全局初始化。

你可以从下表中挑选最贴近当前任务的完整可运行程序，先查看它的输入与运行效果，再阅读对应小节了解配额限制与并发边界。全部示例的构建目标和运行目录说明也可在[示例目录](../examples/README.zh-CN.md#sdk)中查阅；如果尚未将库接入自己的工程，请先参考[入门教程](USAGE.zh-CN.md#project)。

| 要做什么 | 完整示例程序 | 运行后会看到什么 |
| --- | --- | --- |
| 将内存中的 JSON 字节压缩后再解压还原 | [compress_memory.c](../examples/compress_memory.c) | 打印压缩前后的字节大小，以及完整还原出的 JSON 文本 |
| 解析 URL 并添加或读取查询参数 | [url_query.c](../examples/url_query.c) | 打印百分号编码后的完整 URL 与解码出的设备名称（纯内存解析，不发起网络请求） |
| 从命令行读取监听端口和设备名称 | [command_line.c](../examples/command_line.c) | 打印解析后的选项值；传入 `--help` 会输出自动生成的用法说明 |
| 向 HTTP/HTTPS 服务端发送 JSON 请求 | [http_json.c](../examples/http_json.c) | 打印 HTTP 状态码与响应体（运行前需将参数替换为你自己的服务端地址） |
| 创建排他临时文件、写入数据并在关闭时自动删除 | [temporary_file.c](../examples/temporary_file.c) | 打印生成的随机临时路径，并在关闭后确认文件已被清理 |

> [!NOTE]
> 内存压缩、URL 解析和 HTTP 示例需要预先准备好对应的第三方[依赖 SDK](DEPENDENCIES.zh-CN.md)。在已开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录中，可按 `hx_os_<源文件名>` 构建单个示例（以 `hx_os_compress_memory` 为例）：
>
> ```sh
> cmake --build build --config Release --target hx_os_compress_memory
>
> # Linux / macOS：
> ./build/bin/hx_os_compress_memory
>
> # Windows PowerShell（Visual Studio 生成器）：
> .\build\bin\Release\hx_os_compress_memory.exe
> ```

## 1. 在内存中压缩字节

当你需要在内存中直接压缩或解压一段二进制载荷（例如网络报文或缓存块），而不是打包多文件 `.zip` 归档时，可以使用 `hx_os_compress` 与 `hx_os_decompress`（C++11 对应 `hx::Compression::compress` / `decompress`，返回 `hx::Result<hx::Bytes>`）。该接口支持单帧 `Zlib`、`Gzip`、`XZ` 和 `Zstd` 格式（对应枚举如 `HX_OS_COMPRESS_ZSTD`）：

```c
hx_bytes compressed = HX_BYTES_INIT, restored = HX_BYTES_INIT;
const char text[] = "hello hello hello";

hx_os_status status = hx_os_compress(HX_OS_COMPRESS_ZSTD,
	(hx_os_bytes_view){text, sizeof(text) - 1}, NULL, &compressed, NULL);
if (status == HX_OS_OK)
	status = hx_os_decompress(HX_OS_COMPRESS_ZSTD,
		hx_bytes_view(&compressed), NULL, &restored, NULL);

hx_bytes_destroy(&restored);
hx_bytes_destroy(&compressed);
```

- **默认配额与内存边界**：默认压缩级别为 `3`，单次输出缓冲区上限为 `16 MiB`（可通过可选限制参数调高至最大 `64 MiB`），底层原生编解码器工作内存预算为 `64 MiB`。底层压缩库内部申请的工作内存不计入 `hx_context` 统计；输出 `hx_bytes` 在扩容过渡期间可能短暂同时持有新旧两块缓冲区。
- **失败保持原值与帧完整性**：任何压缩或解压失败都会保持调用方原有的 `hx_bytes` 输出内容不变（即使输入视图直接指向输出缓冲区自身也是安全的）。解压器严格校验单帧完整性，遇到尾部多余字节、多个串联压缩帧、数据截断、校验损坏或超出输出上限时会分别返回明确的错误状态。请注意，极短的输入数据在加上压缩帧头后体积可能比原文略大。

## 2. URL 和查询参数

如果你需要安全地拆解、拼接 URL 或读写 `?key=value` 查询参数，应使用 `hx_os_url`（C++11 为 `hx::Url`），避免手写字符串拼接引发转义漏洞。解析过程纯在内存中完成，不会发起 DNS 查询或网络连接：

```c
hx_os_url *url = NULL;
hx_string address = HX_STRING_INIT;

hx_os_status status = hx_os_url_parse(NULL,
	"https://example.com/devices?request=42", &url, NULL);
if (status == HX_OS_OK)
	status = hx_os_url_query_set(url,
		hx_string_view_from_cstr("name"),
		hx_string_view_from_cstr("Door A"), NULL);
if (status == HX_OS_OK)
	status = hx_os_url_part(url, HX_OS_URL_FULL, &address, NULL);

hx_os_url_destroy(&url);
hx_string_destroy(&address);
```

运行上述代码后，`address` 中生成的完整 URL 末尾会自动编码为 `name=Door%20A`。

- **组件提取与参数读写**：
  - `hx_os_url_parse` 仅接受包含层级结构的绝对 URL（最大长度 `16 KiB`）。
  - `hx_os_url_part` 提取并复制编码后的 URL 组成部分；`hx_os_url_query_get` 提取并百分号解码指定的 UTF-8 查询参数（若键不存在则返回 `HX_OS_NOT_FOUND`；若存在多个同名键则返回第一项）。
  - `hx_os_url_query_append` 会在保留已有同名参数的基础上追加新键值对，而 `hx_os_url_query_set` 会替换该键名下的所有已有值。
  - `hx_os_url_query_each` 支持同步遍历最多 `1,024` 对查询参数。回调收到的键值视图仅在当次回调期间有效，且回调内部不得修改或销毁正在遍历的 `hx_os_url` 对象。
- **空格与百分号编解码规则**：在查询参数中，`+` 会被解码为空格，字面加号需编码为 `%2B`。独立提供的百分号编解码工具函数支持任意原始字节（包含内嵌的 `NUL` 字节）并由调用方显式指定 `plus_as_space`：编码输入上限为 `16 KiB`（输出最多 `48 KiB`），解码输入上限为 `48 KiB`（输出最多 `16 KiB`），遇到非法 `%` 转义序列时返回 `HX_OS_SYNTAX`。
- **并发与底层生命周期**：同一个 `hx_os_url` 实例上的操作需由调用方串行化。每个存活的 `hx_os_url` 内部持有一份配对的 `libcurl` 全局初始化引用，若宿主程序在其他模块中也直接使用 `libcurl`，需协调好全局分配器与清理时机。

## 3. 命令行参数

`hx_os_cli`（C++11 为 `hx::Cli`）让你只需声明一次选项表，即可完成命令行解析、类型转换与 `--help` 帮助文本生成。在普通 `main(int argc, char **argv)` 中，推荐优先使用 `hx_os_cli_parse_process`——它在 Windows 上直接读取系统原生 UTF-16 命令行并转为 UTF-8，避免窄字符 CRT `argv` 因本地代码页丢失非 ASCII 字符；若需解析自定义的字符串数组，则调用 `hx_os_cli_parse`（输入必须为合法 UTF-8）：

```c
const hx_os_cli_option options[] = {
	{"port", "Listen port", 'p', HX_OS_CLI_VALUE, 0},
	{"verbose", "Show details", 'v', HX_OS_CLI_FLAG, 0}};
hx_os_cli *args = NULL;
int64_t port = 8080;

hx_os_status status = hx_os_cli_parse_process(NULL, argc,
	(const char *const *)argv, options, 2, &args, NULL);
if (status == HX_OS_OK && hx_os_cli_count(args, "port"))
	status = hx_os_cli_i64(args, "port", &port, NULL);

hx_os_cli_destroy(&args);
```

- **支持的语法格式**：支持长选项 `--port=9000`、短选项 `-p 9000`、合并短标志（如 `-vv`）以及 `--` 分隔符后的位置参数。
- **重复选项与位置参数读取**：同一个选项允许在命令行中出现多次；常规查询接口（如 `hx_os_cli_value`、`hx_os_cli_i64`）默认返回最后一次出现的值，也可按索引序号读取全部出现值。将键名传为 `NULL` 即可读取位置参数。
- **错误处理与配额上限**：遇到未知选项、选项缺少参数值或缺失必填选项时，解析函数会明确返回错误状态，**绝不会在库内部擅自调用 `exit()` 终止进程**。单次解析最多支持 `256` 项选项声明、`4,096` 个参数/出现项以及 `1 MiB` 的字符串副本总预算。解析结果对象（`hx_os_cli`）内部持有所有字符串内存，返回的借用视图在调用 `hx_os_cli_destroy` 之前保持有效。可运行 `hx_os_command_line --port 9000 --name "门禁 A"` 或加上 `--help` 体验完整效果。

## 4. HTTP 和 HTTPS

如果你需要发起单次 HTTP `GET` 请求，只需初始化 `hx_os_http_response` 并调用 `hx_os_http_get`。若需要向同一服务端连续发送多次请求，建议创建并复用 `hx_os_http` 客户端实例（C++11 为 `hx::Http` 及 `hx::Result<hx::HttpResponse>`），调用 `hx_os_http_request` 或 `hx_os_http_post_json`，从而复用底层 `libcurl` 的连接池缓存：

```c
hx_os_http *http = NULL;
hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;

hx_os_status status = hx_os_http_create(NULL, &http, NULL);
if (status == HX_OS_OK)
	status = hx_os_http_post_json(http,
		"https://example.com/records",
		hx_string_view_from_cstr("{\"request\":42}"),
		NULL, &response, NULL);
if (status == HX_OS_OK)
	printf("HTTP %u\n", response.status_code);

hx_os_http_response_destroy(&response);
hx_os_http_close(&http, NULL);
```

`hx_os_http_post_json` 会自动附加 `Content-Type: application/json` 请求头（若请求体包含动态字段，建议先用配置文档或序列化接口生成 JSON，避免手拼字符串）。响应对象持有原始响应体字节、原始响应头块和最终生效的 URL；若业务期望 UTF-8 文本，可在收到响应后显式校验转换。

### 安全策略与重定向规则

- **强制 TLS 验证与安全默认值**：仅允许 `http://` 和 `https://` 协议，拒绝在 URL 中内嵌用户名或密码凭据。HTTPS 始终强制验证服务端证书链与主机名（**不支持关闭证书验证**）；Windows 支持直接使用系统根证书库，也可通过 `ca_file` 显式指定受信任的 PEM 根证书文件（在所选 `curl`/`OpenSSL` SDK 不含默认系统 CA 包的 Android 环境中，需通过 `ca_file` 传入应用认可的 CA 证书包）。
- **默认配额与代理**：默认响应体上限为 `16 MiB`、响应头上限为 `64 KiB`、单次请求超时为 `5 秒`，默认**不自动跟随重定向**且**不读取环境变量代理**（如需代理必须通过选项显式配置）。
- **开启受控重定向**：如果希望跟随最多 3 次 HTTP 跳转，可初始化 `hx_os_http_options` 并将其地址传给请求接口（文件流式下载则设置流选项中的 `request.redirects` 字段）：

```c
hx_os_http_options options = HX_OS_HTTP_OPTIONS_INIT;
options.redirects = 3;
```

重定向设有次数上限，且**严禁从 HTTPS 降级跳转到 HTTP**。开启重定向后，不同 3xx 状态码对下一个请求方法与请求体的影响如下表所示（流式上传因无法自动回退输入流位置，不支持开启重定向）：

| 重定向状态码 | 开启跟随后，下一个请求的行为 |
| --- | --- |
| `301` / `302` | `POST` 转为无请求体的 `GET`；`PUT` / `PATCH` 等其他方法保留原方法与请求体 |
| `303` | 统一转为无请求体的 `GET`（`HEAD` 仍保持 `HEAD`） |
| `307` / `308` | 严格保留原 HTTP 方法与原始请求体（例如 `POST` JSON 会向新地址重发同一份 JSON） |

> [!IMPORTANT]
> **HTTP 状态码判定、并发与上下文释放顺序**
> - **区分网络传输成功与业务状态码**：只要完整收到了服务端返回的 HTTP 报文（哪怕是 `HTTP 404` 或 `HTTP 500`），函数都会返回 `HX_OS_OK`；业务代码必须检查 `response.status_code`。网络断开、TLS 证书错误、主动取消或响应体积超限则会返回错误状态码，保持原有的 `response` 内容不变，并在 `hx_os_error` 中保留原生 `CURLcode` 诊断。
> - **单客户端串行与协作式取消**：同一个 `hx_os_http` 实例同一时刻只允许执行一个请求（并发调用返回 `HX_OS_BUSY`，在回调中重入自身返回 `HX_OS_DEADLOCK`；不同线程可各自创建独立的 `hx_os_http` 实例）。超时与取消为协作式：默认 SDK 的系统阻塞式 DNS 解析无法被半途打断；若需要连 DNS 无响应时也能及时取消，请参阅[可选 SDK 和显式启用入口](HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时)。
> - **自定义分配器销毁顺序**：如果通过 `hx_os_http_response_init(&response, context)` 为响应绑定了自定义 `hx_context`，无论请求成功还是失败（包括下载目标文件路径无效等早期错误），都**必须先销毁 `response` 并关闭使用同一 `context` 的 `hx_os_http` 客户端，最后才能销毁 `context`**。你可以运行 `hx_os_http_json <URL> [trusted-ca.pem]` 测试自己的 JSON 接口。

## 5. 临时文件和目录

当业务需要创建安全的临时工作文件或临时目录（例如先写入临时文件、校验无误后再原子替换为正式文件）时，使用 `hx_os_temp_create_file` 或 `hx_os_temp_create_directory`（C++11 为 `hx::Temp`）。可运行 `hx_os_temporary_file` 查看创建、写入并自动清理临时 JSON 文件的完整流程：

- **排他创建与父目录选择**：`hx_os_temp_create_file` 会使用密码学随机数生成排他文件名并打开文件，返回同时持有打开文件句柄与路径的对象（通过借用访问器获取 `path` 和 `file`）。传入受信任的父目录路径；在桌面系统上传入 `NULL` 会自动选用系统临时目录，而在 Android 上则**必须显式传入应用拥有写权限的目录路径**。POSIX 下新建文件/目录权限固定为 `0600` / `0700`，Windows 下继承父目录 ACL（请勿将不受信任的公共恶意目录作为父目录）。
- **关闭清理、保留（Keep）与原子发布（Publish）**：
  - `hx_os_temp_close`：关闭文件并删除临时文件或递归清理临时目录（采用有界、不跟随符号链接的安全递归删除）。若清理中途遇到 I/O 错误，函数会保留 `hx_os_temp` 句柄供调用方稍后重试关闭（C++ `hx::Temp` 同样借用调用方持久持有的句柄变量，析构函数不会丢弃未清理成功的句柄）。
  - `hx_os_temp_keep`：关闭句柄但明确保留磁盘上的临时文件或目录不被删除。
  - `hx_os_temp_publish`：先关闭文件，再在同一文件系统内将其重命名（移动）到目标路径。注意 `publish` 不会跨文件系统拷贝数据，也不会隐式调用 `fsync`——若业务要求掉电持久化，请在调用 `publish` 之前先对文件执行同步刷盘，并按需同步父目录。对同一临时对象的读写、发布和关闭操作需由调用方在外部串行化。

## 6. 串口字节收发

`hx_os_serial`（C++11 为 `hx::Serial`）提供跨平台的串口配置与带截止时间的字节收发封装：

- **打开与参数配置**：调用 `hx_os_serial_open(NULL, "COM3", NULL, &serial, &error)`（Windows）或传入 `/dev/ttyUSB0` 等 POSIX 设备路径即可打开并配置串口。默认参数为：`115200` 波特率、`8` 数据位、`1` 停止位、无奇偶校验（None）、无硬件流控。你可以通过选项结构体配置 `5–8` 数据位、`1` 或 `2` 停止位、奇偶校验以及 `RTS/CTS` 硬件流控；若请求了当前系统不支持的波特率常量或硬件特性，函数会明确报错且不交付 `serial` 对象（Windows 会在释放临时路径内存前先锁存设备打开错误码，确保自定义分配器的释放回调不会冲掉真正的底层错误原因）。Windows 下串口以独占方式打开，请避免与其他串口软件争抢同一端口。
- **读写、全双工并发与取消边界**：
  - `hx_os_serial_read_until` 与 `hx_os_serial_write_until` 接收单调时钟截止时间和可选的取消令牌（C++ 返回携带实际传输字节数的 `hx::Result<size_t>`）。串口传输天然存在短读与短写，重试时只应提交尚未传输完毕的后缀字节。
  - **支持单读单写全双工并行**：允许一个接收线程与一个发送线程同时操作同一个 `hx_os_serial` 对象（同方向的读取之间或写入之间仍需串行化）。
  - **Windows 重叠 I/O 退役等待**：在 Windows 上触发超时或取消后，库内部会先等待底层驱动将挂起的原生 Overlapped I/O 安全退役，随后才返回，确保调用方栈上的借用缓冲区绝不会被驱动在后台继续覆写（因此实际返回时间受串口驱动响应速度影响，可能略晚于截止时间）。
  - **显式清理与关闭**：本模块不隐式启动后台线程、不维护内部排队缓冲，也不内置任何帧定界协议。调用 `hx_os_serial_discard` 可显式丢弃操作系统串口缓冲区中的待处理字节；调用 `close` 前必须确保所有读写调用均已返回，且 `close` 不会在后台偷偷阻塞等待硬件发送移位寄存器排空。
- **可运行示例**：连接好支持“以换行符 `\n` 结尾的 JSON 请求-响应”固件设备后，可运行 `hx_os_serial_message COM3` 或 `hx_os_serial_message /dev/ttyUSB0`（注：Linux 下的伪终端 `pty` 测试仅用于验证软件读写与超时状态机，不能替代真实物理 UART 的波特率时序、奇偶校验位及 `RTS/CTS` 硬件信号线验收）。

## 7. 命名共享内存

当同一台机器上的两个独立进程需要高速交换大块数据（`1 MiB` ～ `64 MiB`）时，可以使用 `hx_os_shared_memory`（C++11 为 `hx::SharedMemory`）：

- **创建、打开与平台后端**：
  - 创建方调用 `hx_os_shared_memory_create` 排他创建一块初始清零、可写的共享内存区；另一进程调用 `hx_os_shared_memory_open` 映射同一区域。
  - **命名与大小限制**：名称由 `1–64` 个 ASCII 字母、数字、`_`、`.` 或 `-` 组成；有效数据区（Payload）大小范围为 `1B – 64 MiB`，起始地址保证按 `64` 字节对齐（底层页映射内存由操作系统管理，不计入 `hx_context` 堆预算）。
  - **桌面与 Android 路径差异**：在桌面系统上，默认原生映射在 Windows 下使用当前会话命名空间 `Local\hxcp_<NAME>`，在 POSIX/Linux 下使用 `/hxcp_<NAME>`（通常对应 `/dev/shm/hxcp_<NAME>`）。由于 Android Bionic 不提供 POSIX `shm_open`，Android 应用必须使用基于授权目录的文件映射入口 `hx_os_shared_memory_create_at` / `open_at(directory, name, ...)`（该入口同样可在桌面系统上使用，对应文件名为 `<directory>/hxcp_<NAME>.shm`）。库不会猜测 Android 应用私有路径，也不会越权绕过沙箱。运行时可通过 `hx_os_shared_memory_native_available()` 和 `hx_os_shared_memory_available()` 分别查询原生共享内存与通用后端是否可用。
- **64 字节固定元数据头布局**：共享映射区的最前面 `64` 字节为库内部元数据头，偏移 `64` 字节处才是返回给业务的 `data` 指针（若跨语言对接，其他语言必须严格遵守同一头部布局）：

| 字节偏移 | 宽度与格式 | 含义 |
| --- | --- | --- |
| `0` | 32 位本机对齐无锁原子整数 | `ready` 标志（创建方完成初始化后通过 Release 语义置位；打开方通过 Acquire 语义读取，若尚未就绪则返回 `HX_OS_WOULD_BLOCK`） |
| `4` | 4 字节 ASCII | 魔数 `"HXSM"` |
| `8` | 32 位整数 | 头部格式版本号（固定为 `1`） |
| `16` | 64 位小端整数 | 业务数据区（Payload）字节大小 |
| `24..63` | 40 字节 | 保留字段（必须全部清零） |
| `64..` | `size` 字节 | 业务有效载荷数据区（Payload） |

> [!WARNING]
> **共享内存同步、生命周期与残留句柄清理**
> - **业务数据同步与禁忌**：元数据头的 `ready` 标志仅代表共享内存映射本身已建立完毕；业务进程读写 Payload 内容时，必须自行通过无锁原子协议或外部 IPC 信号同步。**切勿在共享内存中存放进程内裸指针、普通进程内互斥锁或非无锁（Non-lock-free）的 C/C++ 原子对象**。此外，对可写共享内存拥有访问权的进程之间不构成安全隔离边界，请勿让不受信任的外部进程随意截断底层映射文件。
> - **对象销毁与名称清理**：POSIX 原生共享内存名称会一直保留，直到显式调用 `unlink`（已建立的映射视图在 `unlink` 后仍可继续访问）；Windows 原生命名共享内存在最后一个句柄关闭时由内核自动回收，不支持显式 `unlink`；基于文件的映射（`create_at` / `open_at`）则会保留磁盘文件直到显式删除。
> - **报错后也必须检查输出 `owner`**：调用 `create` 或 `open` 失败时，正常情况下输出的 `owner` 指针为 `NULL`；但在极少数情况下，若启动中途失败且操作系统拒绝在回滚阶段解除内存映射（`unmap`），函数会在返回创建/打开错误码的同时，**交付一个非空的残留 `owner` 句柄**（此时其 `data` 为 `NULL`、`size` 为 `0`，严禁读写该映射）。调用方必须使用该残留 `owner` 调用 `hx_os_shared_memory_close` 重试清理，且在 `owner` 被彻底消费置空前，不得复用同一个指针变量再次打开，也不能销毁其关联的 `hx_context`（注：若 `close` 已经成功解除映射并消费了 `owner`，但底层关闭文件句柄报错，则无需再次重试关闭；重试前请同时检查返回状态码与 `owner != NULL`）。
> - **运行示例**：在桌面环境可直接运行 `hx_os_shared_memory`（原生映射）或 `hx_os_shared_memory .`（文件映射），程序会自动派生子进程并通过原子 Release/Acquire 标志交换 JSON 数据。在 Android 或不支持派生子进程的环境中，可在同一授权工作目录下先后运行两个独立进程：`hx_os_shared_memory --write demo .` 与 `hx_os_shared_memory --read demo .`（写端发布 JSON 后最多等待 10 秒读端确认，随后自动清理）。

## 8. 模块及底层调用

上述七项功能均可通过独立的 CMake 开关按需裁剪（默认全部开启，`cmake/minimal.cmake` 则提供面向嵌入式环境的最小裁剪配置；当功能被关闭或缺少前置模块时，创建入口会明确返回 `HX_OS_NOT_SUPPORTED`）：

| 功能模块 | CMake 开关 | 前置模块与底层依赖 |
| --- | --- | --- |
| 内存压缩 | `HX_OS_ENABLE_COMPRESSION` | `zlib`、`liblzma`、`zstd`（独立于 `HX_OS_ENABLE_ARCHIVE`） |
| URL 解析 | `HX_OS_ENABLE_URL` | `libcurl` |
| HTTP / HTTPS | `HX_OS_ENABLE_HTTP` | `libcurl`、`OpenSSL` |
| 命令行解析 | `HX_OS_ENABLE_CLI` | Windows 进程参数读取使用系统 Shell API |
| 临时文件与目录 | `HX_OS_ENABLE_TEMP` | `HX_OS_ENABLE_RANDOM`、`HX_OS_ENABLE_FILE_UTILS`（使用默认系统临时目录还需 `HX_OS_ENABLE_PATHS`） |
| 串口通信 | `HX_OS_ENABLE_SERIAL` | 操作系统原生串口设备 API |
| 命名共享内存 | `HX_OS_ENABLE_SHARED_MEMORY` | `HX_OS_ENABLE_VIRTUAL_MEMORY` 及平台原生 32 位无锁原子操作 |

如果你的业务代码除了使用上述封装接口外，还需要直接调用 `libcurl`、`OpenSSL`、`zlib`、`zstd` 或 `SQLite` 的原生 C API，可以直接链接本库导出的[底层依赖目标](NATIVE_DEPENDENCIES.zh-CN.md)，无需在工程里重复引入第二套第三方 SDK。
