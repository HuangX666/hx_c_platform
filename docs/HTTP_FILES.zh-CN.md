# HTTP 文件传输与分块处理

[English](HTTP_FILES.md) · [JSON HTTP 请求](UTILITY_EXTENSIONS.zh-CN.md) · [协程教程](COROUTINES.zh-CN.md)

在下载大体积固件包、图片资产或导出数据报表时，如果把整个 HTTP 响应体先全部读进内存再写盘，很容易在嵌入式或桌面端造成内存峰值暴涨；而且一旦网络中途断开，直接覆盖目标路径还会留下损坏的半截文件。`hx_c_platform` 的 HTTP 文件与流式接口通过**同目录随机临时文件分块落盘 + 校验 HTTP 2xx 后原子发布**，让你无需把大文件塞进内存即可安全完成下载、上传与自定义流式分块处理。

## 1. 下载文件，不必先把全部内容放入内存

使用 `hx_os_http_download_file()` 下载文件时，库会先在目标文件所在的目录下创建一个随机命名的临时文件，边接收数据块边写入磁盘；只有当收到成功的 **HTTP 2xx** 响应并正常关闭临时文件后，才会将其正式发布（重命名/链接）到目标路径。默认情况下（`replace = 0`），如果目标文件已存在，函数会拒绝覆盖并保留原文件；只有显式传入 `replace = 1` 时才会替换已有文件。

在已有构建目录中编译完整的 [http_file.c](../examples/http_file.c) 示例：

```sh
cmake --build build --config Release --target hx_os_http_file
```

为了在本机快速验证，你可以新建一个包含文本 `hello` 的 `hello.txt` 文件，并在该目录下启动一个临时 HTTP 服务（例如执行 `python -m http.server 8080 --bind 127.0.0.1`；注意 Python 仅作为本机演示用的临时文件服务器，并非 C 库的运行依赖）。随后在另一个终端切换到另一个可写目录，执行下载命令：

```sh
# Linux；切换工作目录后，请使用编译出的示例程序实际路径
/path/to/build/bin/hx_os_http_file download \
  http://127.0.0.1:8080/hello.txt downloaded.txt
```

（Windows 下使用 `build/bin/Release/hx_os_http_file.exe`。）
- **首次运行**：输出 `HTTP 200: download downloaded.txt`，生成的 `downloaded.txt` 内容为 `hello`。
- **再次对同一路径运行**：报告 `exists` 错误并完好保留已有的 `downloaded.txt`。
- **HTTPS 支持**：访问 HTTPS 地址时，可通过命令行最后一个可选参数传入自定义 CA 证书路径。

在 C 代码中，创建好可复用的 `hx_os_http *http` 客户端后，下载文件的典型调用如下：

```c
hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
hx_os_status status = hx_os_http_download_file(http, url, "firmware.bin",
	0, NULL, &response, &error);

if (status == HX_OS_OK)
	printf("Saved firmware.bin (HTTP %u)\n", response.status_code);

hx_os_http_response_destroy(&response);
```

### 超时、文件系统限制与失败清理契约

- **默认限额与超时**：流式下载默认允许最大 `1 GiB` 的响应体，并沿用普通 HTTP 请求默认 5 秒的总超时预算。实际工程中可通过 `hx_os_http_stream_options.request.timeout_ms` 和 `max_download_bytes` 按需调整（例如完整示例设为 30 秒）。注意该接口不隐含 `fsync` 落盘同步或断点续传保证。
- **不覆盖发布的原子性边界**：如果目标文件系统不支持原子性的“不覆盖发布”原语（例如在 WSL 中直接挂载访问某些 Windows 盘符路径时可能遇到限制），函数会明确返回 `HX_OS_NOT_SUPPORTED`，绝不会退化为有竞态漏洞的“先 `stat` 检查不存在、再 `rename` 覆盖”。
- **临时文件清理与重试**：当下载失败或收到非 2xx 错误响应（如 HTTP 404/500）时，库绝不会把错误页面发布成目标文件，而是自动尝试删除临时文件。如果删除临时文件本身因系统占用而失败，HTTP 客户端会继续持有该待清理路径：此时后续新请求会返回 `HX_OS_BUSY`，你可以通过 `hx_os_http_pending_file(http)` 查看残留路径，并再次调用 `hx_os_http_close(&http, &error)` 重试清理（关闭失败时请保留 `http` 句柄，切勿直接丢弃指针）。

## 2. 上传文件与自行处理数据块

- **分块上传普通文件（`hx_os_http_upload_file`）**：
  - 按固定分块大小读取本地普通文件并带上已知的文件总长度发送（在调用期间请勿从外部修改该文件）。示例程序的 `upload` 模式使用 HTTP `PUT` 方法：

```sh
build/bin/hx_os_http_file upload http://your-server/upload firmware.bin
```

  - 注意：第 1 节中的 Python `http.server` 不支持 `PUT` 上传，测试上传时请指向支持 `PUT` 的服务端地址。只要 HTTP 传输完整结束，即便服务端返回 HTTP 4xx / 5xx 状态码，函数也会正常返回 `HX_OS_OK` 并填充 `response.status_code` 供你检查。为保证安全，文件上传**不会自动跟随 HTTP 重定向，也不会暗中倒回文件游标重发请求体**。
- **自定义流式读写回调（`hx_os_http_request_stream`）**：
  - 如果你想边接收数据块边做流式哈希/解压，或者从内存生成器流式上传数据，请使用 `hx_os_http_request_stream()`：
    - 提供 `write(user, data, bytes, error)` 回调：逐块消费借用的响应数据（不提供 `write` 回调时，响应体仍按常规内存上限收集到 `response` 中）。
    - 提供 `read(user, buffer, capacity, completed, error)` 回调并指定总长度 `upload_bytes`：逐块提供上传数据。如果在达到声明的 `upload_bytes` 之前 `read` 回调就提前返回 `0` 字节，函数会立即中止并返回 `HX_OS_TRUNCATED_INPUT`。
  - **回调执行约束**：所有读写与进度回调均在当前调用线程上同步执行；回调内部**不得重入同一个 `hx_os_http` 客户端，也不得将借用的数据块指针保存下来延后使用**。进度回调可通过返回非零错误码主动请求中止传输。
  - **低速保护与耗时指标（Metrics）**：同时设置 `low_speed_bytes_per_second` 与 `low_speed_seconds` 可开启 `libcurl` 的低速防卡死超时；传入可选的 `metrics` 结构体指针还能获取实际收发字节数以及累计的 DNS、TCP 建连、TLS 握手、首字节（TTFB）与总耗时（毫秒）。

## 3. C++11 和协程调用

在 C++11 中，包含 `<hx/Http.hpp>` 并调用 `hx::Http::create()` 即可获得 `hx::Result<hx::Http>`（零异常设计）。复用该客户端实例直接调用 `downloadFile(url, path)` 或 `uploadFile(url, path)`，随后检查返回状态与 HTTP 状态码即可；`pendingFile()` 与显式 `close()` 遵循与 C 接口完全相同的清理契约。要把客户端交给其他所有者稍后重试，可调用 `release()`。若析构时仍无法删除临时文件，包装类会释放客户端资源并将文件留在磁盘。如需稍后删除，应先复制路径。C 调用方可用 `hx_os_http_close_keep_file()` 选择保留文件。

### 在协程中发起非阻塞 HTTP 请求

如果在协程（Coroutine）内部直接调用上面的同步 HTTP 函数，会阻塞底层的协程调度工作线程。因此在协程场景下，请先显式创建一个 **`hx_os_coro_http` 异步桥接服务**：
- **有界线程池与预留预算**：`hx_os_coro_http` 默认启动 **2 个独立的后台 I/O 线程**、最多同时接纳 **16 个请求**，并限制 **64 MiB 的输入与响应总内存预留预算**（均可配置，绝不会为每个请求新建线程）。
- **如何发起调用**：C 协程调用 `hx_os_coro_http_request()`（或常用的 `get` 快捷入口），它会在挂起当前协程前先拷贝请求参数；C++ 有栈协程可包含 `<hx/CoroutineNetwork.hpp>` 并调用 `hx::coro::Http(service).get(url, response)`。当请求完成、超时或被取消时，协程会自动恢复执行，挂起期间完全不占用协程工作线程。

> [!IMPORTANT]
> **协程 HTTP 服务的内存预算核算与关闭顺序**
> - **按最大响应上限预留预算**：为了严格防止多个并发大图/大文件响应同时到达时撑爆内存，每个被接纳的请求都会**按其配置的 `max_body_bytes` 上限预先扣除服务预算**，而不是只按当前已收到的字节数计账！由于单个请求的默认响应上限为 `16 MiB`（外加请求体与响应头开销），默认的 `64 MiB` 服务预算同时最多接纳 3 个默认上限的并发请求。如果你在协程里发送的是小型 JSON 请求，建议将 `options.max_body_bytes` 显式调小（例如设为 `64 KiB`），或者在创建服务时调大总预算。
> - **取消与关闭**：在协程 HTTP 中请使用协程自身的取消机制（传入的 HTTP `options.cancel` 必须保持为 `NULL`）。退出时，先在普通所有者线程上调用 `hx_os_coro_http_close_until()` 关闭 HTTP 服务（若超时则保留句柄重试），成功后再关闭协程运行时。已复制的固定正文可按普通 HTTP 请求的方法和正文重发规则跟随重定向。

## 4. 连 DNS 无响应也需要取消时

默认构建的依赖 SDK 使用操作系统原生 DNS 解析器，以便最大程度兼容系统解析规则，但系统同步 DNS 查询在遇到无响应 DNS 服务器时无法被中途取消。如果你要求**即使卡在 DNS 解析阶段也必须严格响应超时与取消令牌**，请先按[依赖构建指南](DEPENDENCIES.zh-CN.md#8-可选的可取消-dns)开启 `-DHX_DEPS_CURL_CARES=ON` 重新构建依赖 SDK，然后在创建好 HTTP 客户端后调用 `hx_os_http_use_cancellable_dns()`：

```c
hx_os_status status = hx_os_http_use_cancellable_dns(http, NULL, &error);
if (status == HX_OS_OK)
	status = hx_os_http_download_file(http, url, "firmware.bin",
		0, NULL, &response, &error);
```

- **参数与生效范围**：第二个参数传 `NULL` 表示使用 `c-ares` 自动读取的本机平台 DNS 配置；你也可以传入 `"127.0.0.1:5353"` 这样的数字 DNS 服务器列表（配置会被拷贝并仅作用于当前 `http` 客户端，成功后会自动清空该客户端既有的连接与 DNS 缓存）。如果当前链接的 `libcurl` 未编译 `c-ares` 支持，该函数会立即返回 `HX_OS_NOT_SUPPORTED`。
- **协程 HTTP 服务开启方式**：对于协程 HTTP 服务，请在 `hx_os_coro_http` 创建成功后、提交任何请求前调用一次 `hx_os_coro_http_use_cancellable_dns(service, NULL, &error)`（若已有在途任务则返回 `HX_OS_BUSY`）。C++11 中对应普通客户端与 `hx::coro::Http` 的 `.useCancellableDns()` 方法。

你可以直接复用前面编译好的示例程序，在末尾加上 `--cancellable-dns` 标志进行验证（请先启动第 1 节中的本地 8080 端口服务）：

```sh
./build/bin/hx_os_http_file download http://127.0.0.1:8080/hello.txt downloaded.txt --cancellable-dns
./build/bin/hx_os_coroutine_http http://127.0.0.1:8080/hello.txt --cancellable-dns
```

如果链接的是未启用 `c-ares` 的默认 SDK，加上 `--cancellable-dns` 会如实打印 `not supported`；去掉该标志即可按默认系统 DNS 运行。
