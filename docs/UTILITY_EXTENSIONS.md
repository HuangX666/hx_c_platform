# Everyday data and I/O tools

[简体中文](UTILITY_EXTENSIONS.zh-CN.md)

This guide covers seven self-contained utility modules: **in-memory frame compression**, **URL parsing and query parameters**, **command-line option parsing**, **HTTP/HTTPS client transfers**, **temporary files and directories**, **serial port byte I/O**, and **named shared memory**. Each module works independently—you only initialize and call the specific tool your task requires.

Pick the task you want to accomplish from the table below to run a complete example program first, then jump to the corresponding section for API details, resource limits, and concurrency rules. Full build target names and runtime instructions are also listed in the [example directory](../examples/README.md#sdk). If you have not linked `hx_c_platform` into your project yet, start with the [project setup](USAGE.md#project) guide.

| Task | Complete Example Program | What You Will See When Running It |
| --- | --- | --- |
| Compress JSON bytes in memory and decompress them back | [compress_memory.c](../examples/compress_memory.c) | Original vs. compressed byte counts and the restored JSON string |
| Parse a URL and add or read query parameters | [url_query.c](../examples/url_query.c) | The percent-encoded URL and decoded device name (purely in memory, no network I/O) |
| Parse a port number and device name from CLI arguments | [command_line.c](../examples/command_line.c) | Parsed option values; passing `--help` prints generated usage text |
| Send a JSON `POST` request to an HTTP/HTTPS endpoint | [http_json.c](../examples/http_json.c) | HTTP status code and response payload (pass your own server URL on the command line) |
| Create, write, and clean up an exclusive temporary file | [temporary_file.c](../examples/temporary_file.c) | The generated temporary file path followed by confirmation of its removal |

> [!NOTE]
> The compression, URL, and HTTP examples require their corresponding third-party [dependency SDKs](DEPENDENCIES.md). From a build directory configured with `HX_OS_BUILD_EXAMPLES=ON`, build any example target using `hx_os_<filename>` (for example, `hx_os_compress_memory`):
>
> ```sh
> cmake --build build --config Release --target hx_os_compress_memory
>
> # Linux / macOS:
> ./build/bin/hx_os_compress_memory
>
> # Windows PowerShell (Visual Studio generator):
> .\build\bin\Release\hx_os_compress_memory.exe
> ```

## 1. Compress bytes in memory

Use `hx_os_compress` and `hx_os_decompress` (or `hx::Compression::compress` / `decompress` returning `hx::Result<hx::Bytes>` in C++11) when you need to compress or decompress an in-memory byte buffer using a single `Zlib`, `Gzip`, `XZ`, or `Zstd` frame (as opposed to a multi-file `.zip` archive):

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

- **Default budgets and memory accounting**: By default, compression uses level `3`, caps output at `16 MiB` (configurable up to `64 MiB`), and budgets `64 MiB` for the native codec's working memory. Native codec working memory is allocated outside the `hx_context` allocator, and growing the output `hx_bytes` buffer may briefly hold both the old and new buffers.
- **Transactional output and strict frame validation**: Any failure leaves the caller's existing `hx_bytes` output untouched, even if the input view aliases the output buffer. Decompression strictly validates a single frame, rejecting trailing garbage bytes or concatenated frames and distinguishing truncated input, corrupted data, and size-limit violations. Note that very tiny inputs can grow slightly after compression due to frame headers.

## 2. URLs and query parameters

Use `hx_os_url` (`hx::Url` in C++11) to parse, inspect, and safely modify URLs and query parameters in memory without manual string concatenation or network calls:

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

In the snippet above, the serialized URL in `address` ends with `name=Door%20A`.

- **Component extraction and query manipulation**:
  - `hx_os_url_parse` accepts absolute hierarchical URLs up to `16 KiB`.
  - `hx_os_url_part` copies an encoded URL component, while `hx_os_url_query_get` copies and percent-decodes a UTF-8 query parameter value (returning `HX_OS_NOT_FOUND` when the key is absent, or the first occurrence when duplicate keys exist).
  - `hx_os_url_query_append` adds a parameter while preserving existing duplicate keys; `hx_os_url_query_set` replaces all occurrences of that key.
  - `hx_os_url_query_each` synchronously iterates over up to `1,024` query key-value pairs. Callback string views are valid only until the callback returns, and callbacks must not mutate or destroy the URL being traversed.
- **Percent-encoding and `+` handling**: Inside query parameters, `+` decodes as a space while a literal plus sign is encoded as `%2B`. Standalone percent-encoding/decoding helpers also accept arbitrary byte buffers (preserving decoded `NUL` bytes) with an explicit `plus_as_space` parameter: encoding accepts up to `16 KiB` of raw input (producing at most `48 KiB`), decoding accepts up to `48 KiB` of encoded text (producing at most `16 KiB`), and malformed `%` escapes fail with `HX_OS_SYNTAX`.
- **Serialization and native lifetime**: Externally serialize operations on a single `hx_os_url` instance. Each live URL holds a balanced `libcurl` global initialization lease; coordinate any process-wide `libcurl` allocator or cleanup calls across other SDK consumers in your process.

## 3. Command-line options

`hx_os_cli` (`hx::Cli` in C++11) lets you declare your command-line options once and query typed values or generate `--help` usage text. Inside a standard `main(int argc, char **argv)`, prefer `hx_os_cli_parse_process`: on Windows it reads the process's native UTF-16 command line directly and converts it to UTF-8 (avoiding lossy narrow CRT `argv` code-page conversions), while on POSIX it parses `argc`/`argv`. Use `hx_os_cli_parse` when passing a custom array of UTF-8 strings:

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

- **Supported syntax**: Handles `--port=9000`, `-p 9000`, grouped short flags (such as `-vv`), and `--` to terminate option parsing before positional arguments.
- **Repeated options and positional arguments**: Options may appear multiple times; scalar getters (`hx_os_cli_value`, `hx_os_cli_i64`) return the last occurrence, while indexed getters let you inspect every occurrence. Pass `NULL` as the option name to query positional arguments.
- **Explicit errors and resource bounds**: Unknown options, missing option values, and missing required options return an explicit error status—**the parser never calls `exit()` behind your back**. Limits are `256` option specifications, `4,096` arguments/occurrences, and `1 MiB` of copied string storage. The parsed `hx_os_cli` object owns all copied strings, and borrowed views remain valid until `hx_os_cli_destroy`. Try running `hx_os_command_line --port 9000 --name "Door A"` or `hx_os_command_line --help`.

## 4. HTTP and HTTPS

For a one-off `GET` request, initialize `hx_os_http_response` and call `hx_os_http_get`. When making repeated requests, create an `hx_os_http` client (`hx::Http` and `hx::Result<hx::HttpResponse>` in C++11) and reuse it across `hx_os_http_request` / `hx_os_http_post_json` calls so `libcurl` can keep its connection cache warm:

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

`hx_os_http_post_json` automatically sets `Content-Type: application/json` (for dynamic payloads, build the JSON payload with the config document/codec API). A response owns the raw body bytes, raw header blocks, and effective URL; convert the body to UTF-8 explicitly when text is expected.

### Security Defaults and Redirect Rules

- **Mandatory TLS verification**: Only `http://` and `https://` URLs are accepted, and URLs with embedded user/password credentials are rejected. HTTPS always verifies both the certificate chain and hostname (verification cannot be disabled). Windows can use the native OS trust store, and `ca_file` lets you supply an explicit trusted CA bundle (on Android, where a standalone `curl`/`OpenSSL` SDK may not carry a default system CA bundle, pass your application-approved CA file via `ca_file`).
- **Default limits and proxies**: Defaults cap response bodies at `16 MiB` and headers at `64 KiB`, set a `5-second` timeout, disable automatic redirects, and ignore environment proxy variables (proxies must be configured explicitly).
- **Enabling bounded redirects**: To follow up to 3 redirects, initialize `hx_os_http_options` and pass `&options` instead of `NULL` (for streaming file downloads, set `request.redirects` in the stream options):

```c
hx_os_http_options options = HX_OS_HTTP_OPTIONS_INIT;
options.redirects = 3;
```

Redirects are bounded and **never downgrade an `https://` request to `http://`**. When redirects are enabled, HTTP `3xx` responses adjust the next request as follows (streaming uploads reject redirects because an arbitrary input stream cannot be rewound automatically):

| Redirect Status | Next Request Behavior When Redirects Are Enabled |
| --- | --- |
| `301` / `302` | `POST` becomes `GET` with no body; other methods (`PUT`, `PATCH`, etc.) keep their method and body |
| `303` | Becomes `GET` with no body (`HEAD` remains `HEAD`) |
| `307` / `308` | Preserves the original HTTP method and request body (e.g., resends a JSON `POST` to the new target) |

> [!IMPORTANT]
> **HTTP Status Codes, Concurrency, and Allocator Context Cleanup**
> - **Transport success vs. HTTP status code**: `HX_OS_OK` means a complete HTTP exchange finished, **including `HTTP 404` or `HTTP 500` responses**—always inspect `response.status_code`. Network failures, TLS verification errors, cancellations, and size-limit violations leave the previous `response` unchanged and preserve native `CURLcode` diagnostics in `hx_os_error`.
> - **Per-client serialization and cooperative cancellation**: A single `hx_os_http` client handles one request at a time (concurrent calls return `HX_OS_BUSY`, and re-entering from a callback returns `HX_OS_DEADLOCK`; separate clients can run concurrently on separate threads). Deadlines and cancellation are cooperative: the default SDK's blocking OS DNS resolver cannot be interrupted mid-lookup. If you need cancellation even when a DNS server never replies, see the [opt-in SDK and explicit DNS entry](HTTP_FILES.md#4-cancel-even-when-dns-receives-no-reply).
> - **Custom allocator cleanup order**: If you bind a response to a custom `hx_context` via `hx_os_http_response_init(&response, context)`, always **destroy `response` and close any `hx_os_http` client using that `context` before destroying the `context` itself**, including on error paths (such as an invalid download target path). Run `hx_os_http_json <URL> [trusted-ca.pem]` to test against your own endpoint.

## 5. Temporary files and directories

Use `hx_os_temp_create_file` and `hx_os_temp_create_directory` (`hx::Temp` in C++11) to create exclusively named temporary files or directories in a trusted parent directory. Run `hx_os_temporary_file` to see creation, writing, and cleanup end to end:

- **Exclusive creation and parent directory selection**: `hx_os_temp_create_file` opens a randomly named exclusive file and exposes borrowed `path` and `file` accessors. Passing `NULL` as the parent directory selects the OS temporary directory on desktop platforms, whereas **Android requires an explicit application-authorized directory path**. POSIX sets permissions `0600` (files) and `0700` (directories), while Windows inherits parent directory ACLs (do not point temporary creation at an untrusted adversarial parent directory).
- **Close, keep, and atomic publish**:
  - `hx_os_temp_close` closes any open file handle and removes the file or directory (using bounded, non-symlink-following recursive removal for directories). If removal fails, the handle is retained so the caller can retry cleanup (C++ `hx::Temp` likewise borrows a caller-owned pointer variable so its destructor never leaks or silently drops a retryable cleanup failure).
  - `hx_os_temp_keep` closes the handle while keeping the path on disk.
  - `hx_os_temp_publish` closes the file and renames it over the target path on the same filesystem. It does not copy across filesystems or implicitly call `fsync`; sync the file before publishing (and sync the parent directory if required by your durability contract). Externally serialize file I/O, publication, and cleanup on each temporary object.

## 6. Serial byte I/O

`hx_os_serial` (`hx::Serial` in C++11) opens, configures, and performs deadline-bounded byte I/O on hardware or virtual serial ports:

- **Opening and port parameters**: Call `hx_os_serial_open(NULL, "COM3", NULL, &serial, &error)` on Windows or pass a POSIX device path such as `"/dev/ttyUSB0"`. Defaults are `115200` baud, `8` data bits, `1` stop bit, no parity, and no hardware flow control. Options allow `5–8` data bits, `1` or `2` stop bits, odd/even parity, and `RTS/CTS` flow control; unsupported POSIX baud constants or hardware features fail explicitly without returning a serial handle (on Windows, the adapter captures the device-open error before freeing temporary wide-path memory so custom allocator free callbacks cannot clobber the diagnostic). Windows opens serial ports exclusively.
- **Transfers, full-duplex concurrency, and cancellation**:
  - `hx_os_serial_read_until` and `hx_os_serial_write_until` accept a monotonic deadline and an optional cancellation token (returning `hx::Result<size_t>` with byte progress in C++). Short reads and short writes are normal; retry only the untransferred suffix.
  - **Concurrent reader + writer**: One reader thread and one writer thread may use the same `hx_os_serial` instance concurrently (serialize calls within each direction, and exclude all I/O before calling `close`).
  - **Safe overlapped retirement on Windows**: When a Windows transfer times out or is cancelled, the library waits for the kernel driver to retire the overlapped I/O operation before returning so caller buffers are never corrupted after return (driver retirement latency can therefore exceed the requested deadline).
  - **Explicit buffer discard**: No background worker thread, message framing, or software queue is created. Call `hx_os_serial_discard` to drop pending OS driver buffers; `close` does not silently block to drain hardware transmit buffers.
- **Runnable example**: Run `hx_os_serial_message COM3` or `hx_os_serial_message /dev/ttyUSB0` against a device that speaks newline-delimited JSON requests and replies (note: Linux pseudoterminal `pty` tests verify software I/O and timeout state machines, not physical UART signal timing, parity bits, or `RTS/CTS` wiring).

## 7. Named shared memory

Use `hx_os_shared_memory` (`hx::SharedMemory` in C++11) when two processes on the same host need to share a `1B – 64 MiB` memory region:

- **Creation, opening, and OS backends**:
  - `hx_os_shared_memory_create` exclusively creates a zero-initialized writable mapping; `hx_os_shared_memory_open` maps the same region in another process.
  - **Names and alignment**: Names consist of `1–64` ASCII letters, digits, `_`, `.`, or `-`. The application payload is `1B – 64 MiB` and aligned to `64` bytes (OS page mappings live outside `hx_context` heap accounting).
  - **Desktop vs. Android backends**: Native mappings use `Local\hxcp_<NAME>` in the current Windows session and `/hxcp_<NAME>` on POSIX (typically `/dev/shm/hxcp_<NAME>` on Linux). Because Android has no native `shm_open`, Android applications must use the file-backed entry points `hx_os_shared_memory_create_at` / `open_at(directory, name, ...)` with an explicit directory shared by the participating processes (this file-backed mode also works on desktop OSes as `<directory>/hxcp_<NAME>.shm`). Query `hx_os_shared_memory_native_available()` and `hx_os_shared_memory_available()` to inspect backend support.
- **64-Byte Metadata Header Layout**: The first `64` bytes of the mapping form an immutable library header, and the caller's payload starts at byte offset `64`:

| Byte Offset | Width & Format | Description |
| --- | --- | --- |
| `0` | 32-bit aligned lock-free atomic integer | `ready` flag (published with Release semantics after setup; readers Acquire it and receive `HX_OS_WOULD_BLOCK` while initialization is still in progress) |
| `4` | 4 ASCII bytes | Magic bytes `"HXSM"` |
| `8` | 32-bit integer | Format version (`1`) |
| `16` | 64-bit little-endian integer | Payload size in bytes |
| `24..63` | 40 bytes | Reserved (zero-filled) |
| `64..` | `size` bytes | Application payload region (`data`) |

> [!WARNING]
> **Synchronization, Lifetime, and Rollback Cleanup**
> - **Application payload synchronization**: The header's `ready` flag only publishes the mapping metadata. Synchronize reads and writes to the payload itself using lock-free atomics or an external IPC channel. **Never place process-local pointers, process-private mutexes, or non-lock-free atomics inside shared memory.** Shared memory is not a security boundary against a peer that has write access or can truncate the backing file.
> - **Name persistence**: POSIX shared-memory names persist until explicitly unlinked (existing mapped views remain valid after `unlink`); Windows native mappings disappear automatically when the last handle closes and do not support explicit `unlink`; file-backed mappings persist on disk until deleted.
> - **Always inspect the output `owner` pointer, even when `create` / `open` fails**: On ordinary failures, the output `owner` is `NULL`. However, if setup fails after mapping memory and the OS refuses to unmap during rollback, the function preserves the creation/open error while returning a **non-`NULL` residual `owner`** (with `data == NULL` and `size == 0`—never read or write it). Retry cleanup with `hx_os_shared_memory_close` on that `owner`, do not reuse the pointer variable for a new open until cleanup consumes it, and keep its `hx_context` alive (note: `close` can consume `owner` while reporting a native handle-close error, so check both the status code and `owner != NULL` before retrying).
> - **Runnable example**: Run `hx_os_shared_memory` (native mapping) or `hx_os_shared_memory .` (file-backed mapping) on desktop platforms to spawn a child process that exchanges JSON via an atomic release/acquire flag. On Android or hosts without child-process spawning, run `hx_os_shared_memory --write demo .` and `hx_os_shared_memory --read demo .` as separate processes in the same authorized directory.

## 8. Modules and direct SDK calls

Each utility is controlled by its own CMake option (all `ON` by default, with `cmake/minimal.cmake` providing a stripped-down embedded profile; disabled modules return `HX_OS_NOT_SUPPORTED`):

| Feature | CMake Switch | Prerequisites & Native Dependencies |
| --- | --- | --- |
| Memory compression | `HX_OS_ENABLE_COMPRESSION` | `zlib`, `liblzma`, `zstd` (independent of `HX_OS_ENABLE_ARCHIVE`) |
| URL parsing | `HX_OS_ENABLE_URL` | `libcurl` |
| HTTP / HTTPS | `HX_OS_ENABLE_HTTP` | `libcurl`, `OpenSSL` |
| Command-line parser | `HX_OS_ENABLE_CLI` | Windows Shell API for native UTF-16 process arguments |
| Temporary resources | `HX_OS_ENABLE_TEMP` | `HX_OS_ENABLE_RANDOM`, `HX_OS_ENABLE_FILE_UTILS` (plus `HX_OS_ENABLE_PATHS` for default temp directory lookup) |
| Serial port I/O | `HX_OS_ENABLE_SERIAL` | Native OS serial device APIs |
| Named shared memory | `HX_OS_ENABLE_SHARED_MEMORY` | `HX_OS_ENABLE_VIRTUAL_MEMORY` and native lock-free 32-bit atomics |

When your application needs to call underlying third-party SDKs (`libcurl`, `OpenSSL`, `zlib`, `zstd`, `SQLite`, etc.) directly alongside these wrappers, link the exported [native dependency targets](NATIVE_DEPENDENCIES.md) so both layers share a single SDK build.
