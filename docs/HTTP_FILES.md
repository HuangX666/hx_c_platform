# HTTP File Transfers and Streaming

[简体中文](HTTP_FILES.zh-CN.md) · [JSON HTTP Requests](UTILITY_EXTENSIONS.md) · [Coroutine Guide](COROUTINES.md)

When downloading large firmware images, media assets, or data exports over HTTP, buffering the entire response body in RAM wastes memory, and writing directly over the destination path leaves a corrupted partial file if the connection drops mid-transfer. `hx_c_platform`'s streaming HTTP file APIs solve both problems by **streaming chunks into a randomized temporary file in the destination directory and atomically publishing it only after a complete HTTP 2xx response**.

## 1. Download a file without keeping it all in memory

Call `hx_os_http_download_file()` to stream a remote resource straight to disk. It creates a temporary file alongside the target path, writes chunks as they arrive, closes the handle, and publishes the file to the destination path only when the server returns an **HTTP 2xx** status. By default (`replace = 0`), existing destination files are preserved; pass `replace = 1` only when you want to overwrite an existing file.

Build the complete [http_file.c](../examples/http_file.c) example in your existing build directory:

```sh
cmake --build build --config Release --target hx_os_http_file
```

To test locally, create a file named `hello.txt` containing `hello` and start a local HTTP server in that directory (for example, `python -m http.server 8080 --bind 127.0.0.1`—Python is only used as a quick local test server, not a C library runtime dependency). In a second terminal, switch to another writable directory and run:

```sh
# Linux; use the absolute path to your built binary when running from another directory.
/path/to/build/bin/hx_os_http_file download \
  http://127.0.0.1:8080/hello.txt downloaded.txt
```

(On Windows, run `build/bin/Release/hx_os_http_file.exe`.)
- **First run**: Prints `HTTP 200: download downloaded.txt` and writes `hello` into `downloaded.txt`.
- **Second run to the same path**: Fails with `exists` and leaves the existing `downloaded.txt` untouched.
- **HTTPS**: Pass an optional trailing CA bundle path when downloading from an HTTPS endpoint.

In C, once you have created a reusable `hx_os_http *http` client handle, downloading a file takes just a few lines:

```c
hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
hx_os_status status = hx_os_http_download_file(http, url, "firmware.bin",
	0, NULL, &response, &error);

if (status == HX_OS_OK)
	printf("Saved firmware.bin (HTTP %u)\n", response.status_code);

hx_os_http_response_destroy(&response);
```

### Timeouts, Filesystem Atomicity, and Cleanup Rules

- **Default Limits**: Streaming downloads default to a `1 GiB` body cap and the standard 5-second HTTP timeout budget. Customize `hx_os_http_stream_options.request.timeout_ms` and `max_download_bytes` for large files (the example uses 30 seconds). Note that this API does not imply an automatic `fsync` or broken-download resume.
- **No-Overwrite Publication**: If the destination filesystem cannot guarantee atomic no-overwrite publication (which can happen on certain WSL Windows-drive mounts), the call returns `HX_OS_NOT_SUPPORTED` rather than falling back to a racy "check existence, then rename" sequence.
- **Temporary File Cleanup**: On transfer failure or non-2xx HTTP responses (whose error bodies are never published as the target file), the library removes the temporary file automatically. If removing the temporary file fails, the HTTP client retains that pending cleanup path: inspect `hx_os_http_pending_file(http)` and retry `hx_os_http_close(&http, &error)` (subsequent requests on that handle return `HX_OS_BUSY` until cleanup succeeds; never leak a client handle whose close failed).

## 2. Upload and process chunks

- **Chunked File Uploads (`hx_os_http_upload_file`)**:
  - Streams a regular file from disk in chunks using its known byte size (do not modify the file while the upload is in progress). The example's `upload` mode sends an HTTP `PUT` request:

```sh
build/bin/hx_os_http_file upload http://your-server/upload firmware.bin
```

  - Note that Python's built-in `http.server` from Section 1 does not implement `PUT`. Because completed HTTP 4xx/5xx responses return `HX_OS_OK` at the transport layer, always inspect `response.status_code`. File uploads **never follow HTTP redirects or silently rewind and replay the request body**.
- **Custom Streaming Callbacks (`hx_os_http_request_stream`)**:
  - For custom storage, on-the-fly hashing, or in-memory streaming uploads, call `hx_os_http_request_stream()`:
    - Supply a `write(user, data, bytes, error)` callback to consume each borrowed response chunk (if `write` is `NULL`, the response body is buffered in `response` up to the configured memory limit).
    - Supply a `read(user, buffer, capacity, completed, error)` callback together with `upload_bytes` to stream outgoing request data. Returning `0` bytes before `upload_bytes` have been produced fails immediately with `HX_OS_TRUNCATED_INPUT`.
  - **Callback Constraints**: Callbacks run synchronously on the calling thread and **must never re-enter the same `hx_os_http` client or retain borrowed chunk pointers after returning**. Progress callbacks can return an error status to abort a transfer cooperatively.
  - **Low-Speed Timeouts & Metrics**: Set both `low_speed_bytes_per_second` and `low_speed_seconds` to enable `libcurl`'s low-speed abort timer. Passing an optional `metrics` struct reports actual transferred bytes alongside cumulative DNS, TCP connect, TLS handshake, first-byte (TTFB), and total elapsed times in milliseconds.

## 3. C++11 and coroutine use

Include `<hx/Http.hpp>` and call `hx::Http::create()` to obtain an exception-free `hx::Result<hx::Http>`. Reuse that client across `downloadFile(url, path)` and `uploadFile(url, path)` calls, checking both the `Result` and `response.status_code`. `pendingFile()` and explicit `close()` preserve the exact same cleanup contract as C.

### Non-Blocking HTTP in Coroutines

Calling synchronous HTTP APIs directly inside a coroutine would block the underlying coroutine scheduler worker thread. Instead, create an explicit **`hx_os_coro_http` service bridge** once:
- **Bounded Worker Pool & Memory Budget**: By default, `hx_os_coro_http` runs **2 dedicated I/O threads**, admits up to **16 concurrent requests**, and enforces a **64 MiB combined input/response reservation budget** (all configurable without spawning a thread per request).
- **Issuing Requests**: In C coroutines, call `hx_os_coro_http_request()` (or the `get` shortcut), which copies the request before suspending the coroutine. In C++ stackful coroutines, include `<hx/CoroutineNetwork.hpp>` and call `hx::coro::Http(service).get(url, response)`. The coroutine suspends cleanly and resumes on completion, timeout, or cancellation without tying up a coroutine worker thread.

> [!IMPORTANT]
> **Reservation Accounting and Shutdown Order for `hx_os_coro_http`**
> - **Up-Front Response Budget Reservation**: Each admitted request reserves its **configured maximum response size (`max_body_bytes`) up front**, not just the bytes received so far. With the default `16 MiB` per-response limit plus input/header overhead, a `64 MiB` service admits at most 3 default-sized requests simultaneously. When issuing small JSON requests from coroutines, lower `options.max_body_bytes` (for example, to `64 KiB`) or raise the service's overall byte budget at creation.
> - **Cancellation & Shutdown**: Use coroutine cancellation (or bind an external cancel token to the coroutine; `options.cancel` inside the copied HTTP options must be `NULL`). At shutdown, close the HTTP service from a normal owner thread via `hx_os_coro_http_close_until()` (retrying on timeout) before closing the coroutine runtime.

## 4. Cancel even when DNS receives no reply

By default, the dependency SDK uses the OS resolver for maximum compatibility, which means a hung system DNS query cannot be interrupted mid-flight. When **HTTP transfers must respond to cancellation and deadlines even during unresponsive DNS lookups**, build the dependency SDK with `-DHX_DEPS_CURL_CARES=ON` per the [Dependency Guide](DEPENDENCIES.md#8-optional-cancellable-dns), then enable cancellable DNS on your client:

```c
hx_os_status status = hx_os_http_use_cancellable_dns(http, NULL, &error);
if (status == HX_OS_OK)
	status = hx_os_http_download_file(http, url, "firmware.bin",
		0, NULL, &response, &error);
```

- **Configuration & Scope**: Passing `NULL` uses the platform DNS servers discovered by `c-ares`, or you can pass a numeric server list such as `"127.0.0.1:5353"`. The setting applies only to this `http` client and clears its existing connection/DNS cache. If the linked `libcurl` was built without `c-ares`, `hx_os_http_use_cancellable_dns()` immediately returns `HX_OS_NOT_SUPPORTED`.
- **Coroutine HTTP Service**: Call `hx_os_coro_http_use_cancellable_dns(service, NULL, &error)` right after creating the service and before submitting requests (calling it while jobs are active returns `HX_OS_BUSY`). Both C++ `hx::Http` and `hx::coro::Http` expose `.useCancellableDns()`.

Test the integration with the existing example binaries by appending `--cancellable-dns` (after starting the local port-8080 server from Section 1):

```sh
./build/bin/hx_os_http_file download http://127.0.0.1:8080/hello.txt downloaded.txt --cancellable-dns
./build/bin/hx_os_coroutine_http http://127.0.0.1:8080/hello.txt --cancellable-dns
```

With the default non-`c-ares` SDK, passing `--cancellable-dns` reports `not supported`; omit the flag to use standard OS DNS resolution.
