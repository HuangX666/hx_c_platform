# HTTP 文件传输与分块处理

[English](HTTP_FILES.md) · [JSON HTTP 请求](UTILITY_EXTENSIONS.zh-CN.md) · [协程教程](COROUTINES.zh-CN.md)

## 1. 下载文件，不必先把全部内容放入内存

下载固件、图片或导出文件时，可以使用 `hx_os_http_download_file`。它在目标所在目录创建随机临时文件，分块写入，收到成功的 HTTP 2xx 响应并关闭文件后再发布到目标路径。默认保留已有目标文件，只有明确传入 `replace=1` 才替换。

编译完整的 [http_file.c](../examples/http_file.c) 示例：

```sh
cmake --build build --config Release --target hx_os_http_file
```

本机演示可以先在一个目录放入内容为 `hello` 的 `hello.txt`，在该目录运行 `python -m http.server 8080 --bind 127.0.0.1`。Python 只是可选的演示服务端，不是 C 库的运行依赖。然后在另一个终端切换到另一个可写目录，运行：

```sh
# Linux；切换目录后，请使用示例程序的实际绝对路径。
/path/to/build/bin/hx_os_http_file download \
  http://127.0.0.1:8080/hello.txt downloaded.txt
```

Windows 使用 `build/bin/Release/hx_os_http_file.exe`。成功时输出 `HTTP 200: download downloaded.txt`，新文件内容为 `hello`。再次指定同一目标运行，会报告 `exists` 并保留已有文件。HTTPS 可以通过最后一个可选参数指定 CA 文件。

创建好可复用的 `hx_os_http *http` 后，C 语言下载动作就是：

```c
hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
hx_os_status status = hx_os_http_download_file(http, url, "firmware.bin",
	0, NULL, &response, &error);

if (status == HX_OS_OK)
	printf("Saved firmware.bin (HTTP %u)\n", response.status_code);

hx_os_http_response_destroy(&response);
```

默认流式内容上限为 1 GiB，沿用普通 HTTP 的五秒等待预算。按需要调整 `hx_os_http_stream_options.request.timeout_ms` 和 `max_download_bytes`；完整示例使用 30 秒。接口不隐含磁盘同步或断点续传保证。如果目标文件系统无法提供要求的“不覆盖发布”，返回 `NOT_SUPPORTED`；WSL 的 Windows 盘挂载可能出现这种情况，不能用“先判断存在、再重命名”这种有竞态的方式冒充同一保证。

失败时会尝试删除临时文件。如果清理本身失败，HTTP 客户端继续持有资源：通过 `hx_os_http_pending_file(http)` 查看路径，再重试 `hx_os_http_close(&http, &error)`。关闭失败保留客户端，不能丢掉最后一个 owner；清理未完成时新请求返回 `BUSY`。收到的 HTTP 错误响应可供检查，但不会把错误页面发布成要求的文件。

## 2. 上传文件与自行处理数据块

`hx_os_http_upload_file` 分块读取普通文件，使用文件的已知长度。调用期间不要修改文件。示例的 `upload` 模式使用 HTTP PUT：

```sh
build/bin/hx_os_http_file upload http://your-server/upload firmware.bin
```

该 URL 必须实现 PUT；上面的 Python 简单文件服务器不支持上传。普通上传会正常返回已完成的 HTTP 4xx／5xx 响应，所以还要检查 `response.status_code`。上传不跟随重定向，也不默默倒回并重发请求内容。

需要自定义存储或边接收边处理时，使用 `hx_os_http_request_stream`。提供 `write(user, data, bytes, error)` 消费每个完整的借用数据块，或提供 `read(user, buffer, capacity, completed, error)` 并指定 `upload_bytes`。声明的长度尚未读完就返回零字节，会报告 `TRUNCATED_INPUT`。已经执行的回调效果不能回滚。不提供 write 回调时，仍按原有内存上限收集响应。

回调在调用线程同步执行，不能重入同一客户端，也不能保存块指针供以后使用。进度回调可返回错误来请求停止。一起设置 `low_speed_bytes_per_second` 和 `low_speed_seconds` 可以启用 libcurl 的低速超时。可选 metrics 返回实际收发数量，以及从传输开始累计的 DNS、连接、TLS、首字节和总耗时，单位为毫秒。取消请求不证明远端上传操作已经撤销。

## 3. C++11 和协程调用

包含 `<hx/Http.hpp>`。`hx::Http::create()` 返回 `Result<Http>`；复用客户端，调用 `downloadFile(url, path)` 或 `uploadFile(url, path)`，检查结果和 HTTP 状态即可。`pendingFile()` 和显式 `close()` 保留同样的失败清理契约，不使用异常。

协程场景先显式创建一个 `hx_os_coro_http`。默认使用两个独立的 I/O 线程、最多十六个已接纳请求，以及 64 MiB 的输入／响应容量预留；这些上限可以调整，不会为每个请求新建线程。`hx_os_coro_http_request` 在挂起前复制请求；常用 GET 有直接入口。C++ 有栈协程可以包含 `<hx/CoroutineNetwork.hpp>`，调用 `hx::coro::Http(service).get(url, response)`。

完成、超时或取消会唤醒协程，不占用协程工作线程。若原生 DNS 仍未退出，只保留任务自己持有的缓冲区。必须持有 I/O service，直到 `hx_os_coro_http_close_until` 成功；关闭超时可重试。使用协程取消，或把外部取消 token 绑定到协程；复制的 HTTP options 要求 `cancel=NULL`。从普通 owner 线程关闭 HTTP service，再关闭协程运行时。这一层采用有界 I/O 池，不是 libcurl multi，也不承诺无限并发传输。

每个已接收请求按配置的最大响应大小预留预算，不是只计算当前已经收到的字节。因此字节预算可能先于请求条数用完：默认每份响应上限 16 MiB，加上输入和响应头开销后，64 MiB 的服务可同时接收三份请求。小型 JSON 响应可以把 `options.max_body_bytes` 设为合适的值，例如 64 KiB；也可以显式提高服务预算。容器扩容峰值及 curl／TLS 原生分配另计。
