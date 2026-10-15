# 常用数据与 I/O 工具

[English](UTILITY_EXTENSIONS.md)

**这些功能分别解决不同任务，不需要一起初始化。** 压缩处理内存字节，URL 工具只整理地址，HTTP 才会访问服务器；串口需要真实设备协议，共享内存需要双方约定数据和同步方式。先选下表中的完整程序，确认它的输入和结果，再读对应小节的上限与并发规则。全部示例的构建目标和运行位置也可从[示例目录](../examples/README.zh-CN.md#sdk)查找。

先选一个你要完成的动作。下表链接到完整程序；本文后面的短代码块只展示关键步骤，需要放进自己的函数中。还没接入库时，先按[入门教程](USAGE.zh-CN.md#project)配置工程。

| 要做什么 | 完整程序 | 运行时会看到什么 |
| --- | --- | --- |
| 把 JSON 字节压缩后再还原 | [compress_memory.c](../examples/compress_memory.c) | 压缩前后大小，以及还原后的 JSON |
| 给 URL 添加设备名参数 | [url_query.c](../examples/url_query.c) | 编码后的地址，以及读回的设备名称；不会访问网络 |
| 从命令行读取端口和名称 | [command_line.c](../examples/command_line.c) | 解析后的选项；`--help` 显示用法 |
| 向自己的服务发送 JSON | [http_json.c](../examples/http_json.c) | HTTP 状态与响应；地址必须换成自己的接口 |
| 写入临时文件并在用完后删除 | [temporary_file.c](../examples/temporary_file.c) | 临时路径，以及删除完成的提示 |

压缩、URL 和 HTTP 示例需要对应的[依赖 SDK](DEPENDENCIES.zh-CN.md)。在库源码目录，使用已配置且开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录，例如编译并运行压缩示例：

```sh
cmake --build build --config Release --target hx_os_compress_memory
./build/bin/hx_os_compress_memory
```

`build` 替换成已有目录；Windows Visual Studio 的运行路径是 `.\build\bin\Release\hx_os_compress_memory.exe`。其余示例使用 `hx_os_` 加文件名作为构建目标。

## 1. 在内存中压缩字节

`hx_os_compress`／`hx_os_decompress` 接收格式、字节数据、可选限制和已初始化的 `hx_bytes` 输出，处理单个 Zlib、Gzip、XZ 或 Zstd 压缩帧。它与包含文件的 ZIP 归档不同。失败保持原输出不变，输入直接引用输出内存时也适用。

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

默认输出上限 16 MiB、原生编解码工作内存预算 64 MiB、压缩级别 3；输出最多可配置到 64 MiB。原生内存不计入 hx 分配器，输出扩容期间可能同时持有新旧缓冲。解压拒绝尾随字节及多个串联压缩帧，截断、损坏和超限分别报告。C++ 使用 `hx::Compression::compress/decompress`，返回 `Result<Bytes>`。运行 `hx_os_compress_memory`；很短的数据压缩后可能变大。

## 2. URL 和查询参数

`hx_os_url_parse` 持有一个绝对、包含层级的 URL。`part` 复制编码后的组成部分，`query_get` 复制解码后的 UTF-8 参数，`query_append` 保留同名参数，`query_set` 替换该键的全部值。`query_each` 同步遍历最多 1024 对参数，回调中的视图只在回调内有效，不能修改或销毁正在遍历的 URL。

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

生成的地址末尾为 `name=Door%20A`。查询参数中的 `+` 解码为空格，真正的加号使用 `%2B`。缺失键返回 `NOT_FOUND`；读取同名参数时取第一项。URL 最长 16 KiB，同一对象的操作需要外部串行化。独立百分号编解码也支持任意字节：编码接受最多 16 KiB 原始输入，结果最多 48 KiB；解码接受最多 48 KiB 编码文本，结果最多 16 KiB。调用者明确选择 `plus_as_space`。无效转义返回 `SYNTAX`，解码出的 NUL 字节会保留。每个 URL 持有配对的原生 curl 初始化引用，宿主需协调其他 SDK 使用者的全局分配器／清理操作。C++ 使用 `hx::Url`。运行 `hx_os_url_query`，解析本身不会进行 DNS 或网络传输。

## 3. 命令行参数

选项名只声明一次，然后解析和查询。`hx_os_cli_parse_process` 在 Windows 上直接读取 UTF-16 命令行，其他系统使用 `argc/argv`；普通 `main` 优先用这个入口，避免把 Windows 本地编码参数误当作 UTF-8。需要解析指定参数列表时使用 `hx_os_cli_parse`，输入必须为 UTF-8。

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

上面放在普通 `main(int argc, char **argv)` 中。支持 `--port=9000`、`-p 9000`、合并短标志，以及 `--` 后的位置参数。同一选项可重复，普通 `value`／`i64` 取最后一次；按序号可以读取全部值，键名 `NULL` 表示位置参数。未知选项、缺值、缺少必需选项会报错，库不会直接退出程序。最多 256 项声明、4096 个参数／出现项、1 MiB 文本副本。解析对象持有字符串，借用视图有效到销毁。运行 `hx_os_command_line --port 9000 --name "门禁 A"`，用 `--help` 查看说明。C++ 使用 `hx::Cli`。

## 4. HTTP 和 HTTPS

单次 GET 可以初始化 `hx_os_http_response` 后调用 `hx_os_http_get`。重复请求则创建并复用 `hx_os_http`，调用 `request`／`post_json`；复用客户端会保留 libcurl 的连接缓存。响应持有原始内容、原始响应头和最终 URL，需要 UTF-8 文本时再显式转换。

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

JSON 入口会添加 `Content-Type: application/json`；动态字段建议用文档／codec 接口生成 JSON。HTTPS 同时验证证书和主机名，`ca_file` 可以明确指定信任的 CA 文件，不能关闭验证。Windows 可使用系统信任库。默认响应内容 16 MiB、响应头 64 KiB、等待 5 秒、不自动重定向、不使用环境代理。显式重定向有次数限制且不能从 HTTPS 降级；只允许 HTTP／HTTPS，不允许包含用户名的 URL。代理需显式配置。

`OK` 表示收到完整响应，包括 HTTP 404／500，业务应检查 `status_code`。传输、证书、取消和超限失败都保持原响应，并保留 CURL 原生诊断。同一个客户端一次只接受一个请求，并发返回 `BUSY`，重入返回 `DEADLOCK`；不同客户端可在不同线程使用。截止时间和取消为协作式：默认 SDK 的阻塞 DNS 不能被中断；需要可取消 DNS 时选择[可选 SDK 和显式启用入口](HTTP_FILES.zh-CN.md#4-连-dns-无响应也需要取消时)，原生 TLS 和调用者回调也可能延迟返回。不自动启动线程，关闭前需要排除调用者，并协调宿主使用的 libcurl／OpenSSL 全局清理。C++ 使用 `hx::Http` 及 `Result<HttpResponse>`。

运行 `hx_os_http_json URL [trusted-ca.pem]` 访问自己的 JSON 服务。本地回归夹具也覆盖 JSON 上传、HTTP 错误状态、响应上限和可信／不可信 HTTPS，不依赖外部互联网服务。

## 5. 临时文件和目录

`hx_os_temp_create_file` 返回持有已打开文件和随机排他路径的对象；`create_directory` 创建空目录。传入可信的父目录，`NULL` 在支持的平台选择系统临时目录；Android 必须明确传入有权限的目录。`path` 和 `file` 为借用。

`close` 关闭并删除资源；目录使用现有的有界、不跟随链接的递归清理。失败保留对象，允许重试，不过部分目录项或文件句柄可能已经处理。`keep` 明确保留路径；`publish` 关闭后在同一文件系统内移动，不跨盘复制，也不隐式请求持久化。需要持久化时先同步文件，并遵守现有目录持久化契约。

POSIX 文件／目录权限为 0600／0700，Windows 继承父目录 ACL，不用于保护恶意父目录。读写、发布和清理需要外部串行化。C++ `hx::Temp` 借用长期保留的句柄，让清理失败仍能重试；便利对象析构不会删除这个句柄。运行 `hx_os_temporary_file` 查看创建、写入和删除临时 JSON 文件的完整流程。

## 6. 串口字节收发

`hx_os_serial_open(NULL, "COM3", NULL, &serial, error)` 或 POSIX 设备路径会打开并配置串口。默认 115200 波特、8 数据位、1 停止位、无校验、无硬件流控。可配置 5–8 数据位、1／2 停止位、奇偶校验和 RTS／CTS；不支持的 POSIX 波特常量或硬件功能会报错。要协调其他设备使用者，Windows 使用排他打开。

`read_until`／`write_until` 接收单调截止时间和可选取消令牌，允许短读短写，只重试尚未完成的后缀。一个接收线程可与一个发送线程并行，各方向自行串行组织。Windows 取消后先等待原生操作退休再返回缓冲区，驱动可能让实际返回超过截止时间。不自动创建线程、消息帧或后台队列；`discard` 显式丢弃待处理字节，关闭不会偷偷等待设备发送缓冲。关闭前排除所有 I/O 调用。C++ 使用 `hx::Serial` 和携带实际字节数的 `Result<size_t>`。

运行 `hx_os_serial_message COM3` 或 `/dev/ttyUSB0`，设备需支持以换行结束的 JSON 请求及回复。示例的换行协议属于设备业务。Linux 伪终端验证软件收发和超时，不能当作实体 UART 时序、校验和流控线路验收。

## 7. 命名共享内存

`hx_os_shared_memory_create` 排他创建清零、可写的共享区，另一个进程用 `open` 映射。名称为 1–64 个 ASCII 字母、数字、`_`、`.`、`-`；数据区为 1–64 MiB，按 64 字节对齐。原生映射和页内存不计入 context 分配统计。Windows 默认名称是 `Local\hxcp_NAME`，作用于当前会话；POSIX 使用 `/hxcp_NAME`，Linux 上通常对应 `/dev/shm/hxcp_NAME`。

Android 没有原生 `shm_open`，需使用 `create_at/open_at(directory, name, ...)`，目录必须允许双方访问。这个文件映射入口也可用于桌面，文件名为 `directory/hxcp_NAME.shm`。库不猜测 Android 应用私有目录，不越过沙箱权限，也不提升权限。`native_available` 和 `available` 分别查询原生后端与通用可用性。

前 64 字节是库元数据：偏移 0 为本机对齐的 32 位原子 ready，偏移 4 是 `HXSM`，偏移 8 为版本 1，偏移 16 为小端 64 位数据区大小，偏移 64 才是业务数据，其余元数据保留并清零。通过 acquire 读取 ready 后再看元数据；创建尚未完成时返回 `WOULD_BLOCK`。发布后的头和文件大小必须保持不变。这个发布只说明映射准备好，业务内容还需要共享协议或外部通信同步；不要存进程内指针、普通进程内互斥锁或依赖本机内部锁的原子类型。

POSIX 名称保留到显式 unlink，现存映射仍可使用；Windows 原生对象在最后一个句柄关闭时消失，不支持显式 unlink。文件映射的名称保留到删除，删除服从系统规则。关闭前排除所有访问者，取消映射失败保留对象。外部截断可能让访问触发系统故障，因此需要协调文件大小和头写入。未完成的遗留对象会明确报告，不自动覆盖；可修改共享区的参与者之间也没有安全隔离。

运行 `hx_os_shared_memory` 使用原生映射，或 `hx_os_shared_memory .` 使用文件映射。程序启动有界等待的子进程，借助业务 release／acquire 标志读取 JSON。C++ `hx::SharedMemory` 借用可显式关闭的持久句柄。其他语言需遵守相同元数据及业务同步协议。

## 8. 模块及底层调用

| 功能 | 开关／依赖 |
| --- | --- |
| 内存压缩 | `HX_OS_ENABLE_COMPRESSION`；zlib、liblzma、zstd，独立于 ARCHIVE |
| URL 解析 | `HX_OS_ENABLE_URL`；libcurl |
| HTTP／HTTPS | `HX_OS_ENABLE_HTTP`；libcurl／OpenSSL |
| 命令行 | `HX_OS_ENABLE_CLI`；Windows 进程参数使用 Shell API |
| 临时资源 | `HX_OS_ENABLE_TEMP`；RANDOM、FILE_UTILS，默认父目录还需 PATHS |
| 串口 | `HX_OS_ENABLE_SERIAL`；原生设备接口 |
| 共享内存 | `HX_OS_ENABLE_SHARED_MEMORY`；VIRTUAL_MEMORY、本机无锁 32 位原子操作 |

缺少必要模块时创建入口返回 `NOT_SUPPORTED`。默认开启，`cmake/minimal.cmake` 提供嵌入式最小配置。业务也能链接[底层依赖目标](NATIVE_DEPENDENCIES.zh-CN.md)，直接调用未修改的 SDK API。公开示例与忽略的本地回归独立。

Android 的 HTTP／HTTPS 调用在所选 curl／OpenSSL SDK 没有默认 CA 文件时，需要通过 `ca_file` 提供应用认可的 CA 集合。证书验证始终开启，不越过沙箱或应用信任策略；独立 TLS 模块使用自己的系统根证书适配。

Android 或没有子进程启动能力的宿主，可在同一授权工作目录分别运行 `hx_os_shared_memory --write demo .` 和 `hx_os_shared_memory --read demo .`。写端发布 JSON，最多等待十秒的读端确认后清理。独立会话使用不同名称，不要求应用自己创建线程。
