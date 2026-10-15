# HTTP file transfers and streaming

[简体中文](HTTP_FILES.zh-CN.md) · [JSON HTTP requests](UTILITY_EXTENSIONS.md) · [Coroutine guide](COROUTINES.md)

## 1. Download a file without keeping it all in memory

Use `hx_os_http_download_file` for firmware, pictures or exported data. It opens a random temporary file in the destination's directory, writes chunks, closes it and publishes it only after a successful HTTP 2xx response. Existing destinations are preserved unless you explicitly pass `replace=1`.

Build the complete [http_file.c](../examples/http_file.c) program:

```sh
cmake --build build --config Release --target hx_os_http_file
```

For a local demonstration, put `hello.txt` containing `hello` in a directory and run `python -m http.server 8080 --bind 127.0.0.1` there. Python is just an optional demonstration server; it is not a runtime dependency of the C library. In another terminal, from a different writable directory:

```sh
# Linux; use your executable's absolute path when changing directory.
/path/to/build/bin/hx_os_http_file download \
  http://127.0.0.1:8080/hello.txt downloaded.txt
```

Windows uses `build/bin/Release/hx_os_http_file.exe`. The successful example prints `HTTP 200: download downloaded.txt`; the new file contains `hello`. Running it again with the same destination fails with `exists` and preserves that file. HTTPS accepts an optional final CA-file argument.

The equivalent C operation, after creating a reusable `hx_os_http *http`, is:

```c
hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
hx_os_status status = hx_os_http_download_file(http, url, "firmware.bin",
	0, NULL, &response, &error);

if (status == HX_OS_OK)
	printf("Saved firmware.bin (HTTP %u)\n", response.status_code);

hx_os_http_response_destroy(&response);
```

The call has a default 1 GiB streamed-body limit and the ordinary five-second HTTP budget. Set `hx_os_http_stream_options.request.timeout_ms` and `max_download_bytes` for your workload; the runnable example uses 30 seconds. No disk-sync or resume guarantee is implied. A filesystem that cannot provide the requested no-overwrite publication returns `NOT_SUPPORTED`; this can occur on WSL Windows-drive mounts. Do not replace this check with a racy “exists, then rename”.

Failures attempt to remove the temporary file. If cleanup itself fails, the HTTP client retains that resource: inspect `hx_os_http_pending_file(http)` and retry `hx_os_http_close(&http, &error)`. Close failure retains the client. Do not lose its last owner. New requests return `BUSY` until pending cleanup is resolved. A received HTTP error response may be inspected, but is never published as the requested file.

## 2. Upload and process chunks

`hx_os_http_upload_file` reads a regular file in chunks and supplies its known length. Keep the file unchanged during the call. The example's `upload` mode sends HTTP PUT:

```sh
build/bin/hx_os_http_file upload http://your-server/upload firmware.bin
```

That URL must implement PUT; Python's simple file server above does not. Generic upload returns completed HTTP 4xx/5xx responses normally, so check `response.status_code`. Uploads do not follow redirects or silently rewind/replay the body.

For application-defined storage or processing, use `hx_os_http_request_stream`. Supply a `write(user, data, bytes, error)` callback that consumes each entire borrowed chunk, or a `read(user, buffer, capacity, completed, error)` callback with a known `upload_bytes`. A read returning no bytes before that length is complete fails with `TRUNCATED_INPUT`. Callback effects cannot be rolled back. With no write callback the response is collected under the existing memory limit.

Callbacks run synchronously on the calling thread and cannot reenter the same client or retain chunk pointers. A progress callback can return an error to stop cooperatively. `low_speed_bytes_per_second` plus `low_speed_seconds` enables libcurl's low-speed timeout; set both or neither. Optional metrics report actual byte counts and cumulative DNS, connection, TLS, first-byte and total times in milliseconds. Cancellation does not prove a remote upload was undone.

## 3. C++11 and coroutine use

Include `<hx/Http.hpp>`. `hx::Http::create()` returns `Result<Http>`. Reuse that client with `downloadFile(url, path)` or `uploadFile(url, path)`; inspect the result and HTTP status. `pendingFile()` and explicit `close()` preserve the same cleanup contract. No exceptions are used.

For a coroutine runtime, explicitly create `hx_os_coro_http` once. Its defaults are two separate I/O threads, sixteen accepted requests and 64 MiB of reserved input/response capacity. The limits are configurable; the module does not start a worker per request. `hx_os_coro_http_request` copies the request before suspension; `get` is the common shortcut. C++ stackful callers can use `<hx/CoroutineNetwork.hpp>` and `hx::coro::Http(service).get(url, response)`.

The coroutine resumes on completion, timeout or cancellation; it does not block a coroutine worker. Any still-running native DNS call retains only job-owned buffers. The I/O service stays owned until `hx_os_coro_http_close_until` succeeds; a close timeout can be retried. Use coroutine cancellation, or bind an external cancellation token to the coroutine: the copied HTTP options require `cancel=NULL`. Close the HTTP service from a native owner thread, then close the coroutine runtime. This bridge uses a bounded I/O pool, not libcurl multi, and does not provide an unbounded number of simultaneous transfers.

Each accepted request reserves its configured maximum response size, not just the bytes currently received. The byte budget may therefore bind before the request-count limit: with the default 16 MiB response limit, the 64 MiB service admits three simultaneous requests after input/header overhead. For small JSON responses, set `options.max_body_bytes` to a suitable value such as 64 KiB; alternatively explicitly raise the service budget. Container growth peaks and native curl/TLS allocations are additional.
