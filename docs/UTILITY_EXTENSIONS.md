# Everyday data and I/O tools

[简体中文](UTILITY_EXTENSIONS.zh-CN.md)

**These tools solve separate tasks and need no combined initialization.** Compression processes bytes, URL tools manipulate addresses, and HTTP performs network transfers. Serial I/O needs a device protocol; shared memory needs an agreed data/synchronization layout. Pick a complete program below before its detailed limits. Build targets and locations are also listed in the [example directory](../examples/README.md#sdk).

Choose the action you need first. The table links to complete programs; the shorter blocks later on this page show the essential calls to place inside your own function. Start with the [project setup](USAGE.md#project) if you have not linked the library yet.

| Task | Complete program | What it shows |
| --- | --- | --- |
| Compress JSON bytes and restore them | [compress_memory.c](../examples/compress_memory.c) | Before/after sizes and the restored JSON |
| Add a device-name query parameter | [url_query.c](../examples/url_query.c) | The encoded URL and decoded name; no network request |
| Read a port and name from command-line arguments | [command_line.c](../examples/command_line.c) | Parsed options; `--help` prints usage |
| Send JSON to your own service | [http_json.c](../examples/http_json.c) | HTTP status and response; supply your endpoint |
| Write and remove a temporary file | [temporary_file.c](../examples/temporary_file.c) | Its temporary path and a removal confirmation |

Compression, URL and HTTP examples need the corresponding [dependency SDKs](DEPENDENCIES.md). From the library checkout, use an existing build directory configured with `HX_OS_BUILD_EXAMPLES=ON`. For example, build and run compression:

```sh
cmake --build build --config Release --target hx_os_compress_memory
./build/bin/hx_os_compress_memory
```

Replace `build` with your directory. A Windows Visual Studio build runs `.\build\bin\Release\hx_os_compress_memory.exe`. Other example target names are `hx_os_` followed by the source filename without its extension.

## 1. Compress bytes in memory

`hx_os_compress` / `hx_os_decompress` take a format, bytes, optional limits and an initialized `hx_bytes` output. This handles one Zlib, Gzip, XZ or Zstd frame; it is different from a ZIP archive containing files. Errors leave the previous output unchanged, including when input aliases output.

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

Defaults: output 16 MiB, native codec working-memory budget 64 MiB, level 3. Output may be raised to 64 MiB. Native codec memory is separate from the hx allocator; output growth can temporarily retain both old and new buffers. Decompression rejects trailing bytes and concatenated frames. Truncation, damaged input and limits are distinct failures. C++ uses `hx::Compression::compress/decompress` with `Result<Bytes>`. Run `hx_os_compress_memory`; tiny inputs can become larger after compression.

## 2. URLs and query parameters

`hx_os_url_parse` owns a parsed absolute hierarchical URL. `part` copies an encoded component, `query_get` copies a decoded UTF-8 value, `query_append` preserves duplicate keys, and `query_set` replaces all occurrences of that key. `query_each` visits up to 1024 pairs synchronously; callback views expire on return and callbacks cannot mutate/destroy that URL.

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

The resulting address ends in `name=Door%20A`. Query decoding treats `+` as a space; literal plus is `%2B`. Missing keys return `NOT_FOUND`, and `query_get` selects the first duplicate. The URL limit is 16 KiB. Operations on one URL require external serialization. The standalone percent encoder/decoder also accepts arbitrary bytes: encode accepts up to 16 KiB and produces at most 48 KiB; decode accepts up to 48 KiB of encoded text and produces at most 16 KiB. The `plus_as_space` flag is explicit. Malformed escapes return `SYNTAX`; decoded NUL is preserved as a byte. Each URL holds a balanced native curl initialization lease; coordinate native global allocator/cleanup changes with other SDK users. C++ uses `hx::Url`. Run `hx_os_url_query`; parsing performs no DNS or transfer.

## 3. Command-line options

Declare option names once, then parse and query them. `hx_os_cli_parse_process` reads UTF-16 arguments directly on Windows and uses `argc/argv` elsewhere. Prefer it to treating Windows `main` arguments as UTF-8. `hx_os_cli_parse` accepts an explicit UTF-8 argument list for other callers.

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

This is the body of a normal `main(int argc, char **argv)`. Accepted forms include `--port=9000`, `-p 9000`, grouped flags and `--` before positional arguments. Repeated values remain queryable by index; ordinary `value`/`i64` selects the last. Name `NULL` selects positional values. Unknown options, missing values and missing required fields are errors; the parser never exits the process. Limits: 256 specifications, 4096 arguments/occurrences, 1 MiB copied text. Parsed values own their strings; borrowed views last until destruction. Run `hx_os_command_line --port 9000 --name "Door A"`; use `--help` for usage. C++ uses `hx::Cli`.

## 4. HTTP and HTTPS

For a single GET, initialize `hx_os_http_response` and call `hx_os_http_get`. For repeated transfers, create one `hx_os_http` client and reuse `request` / `post_json`. Reuse preserves libcurl's connection cache. A response owns raw bytes, raw header blocks and the effective URL; convert body text explicitly when UTF-8 is expected.

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

The JSON helper adds `Content-Type: application/json`; create JSON through the document/codec API for dynamic values. HTTPS verifies certificates and hostname; `ca_file` adds an explicitly trusted CA file, without disabling verification. Windows can use its native trust store. Default response limits are 16 MiB body and 64 KiB headers, with a 5-second timeout, no redirect and no environment proxy. Explicit redirects are bounded and cannot downgrade HTTPS. Only HTTP/HTTPS schemes are permitted; credential-bearing URLs are rejected. A proxy can be supplied explicitly.

`OK` means a completed exchange, including HTTP 404/500: inspect `status_code`. Transport, certificate, cancellation and size-limit failures preserve the old response and report native CURL diagnostics. One client admits one request; concurrent calls return `BUSY`, self-reentry `DEADLOCK`. Different clients can run on separate threads. Deadlines/cancellation are cooperative: the default SDK's blocking DNS cannot be interrupted; use the [opt-in SDK and explicit DNS entry](HTTP_FILES.md#4-cancel-even-when-dns-receives-no-reply) when cancellation is needed, and caller callbacks/native TLS can delay return. No hidden worker is started. Exclude close from active callers and coordinate native libcurl/OpenSSL global cleanup across consumers. C++ uses `hx::Http` and `Result<HttpResponse>`.

Run `hx_os_http_json URL [trusted-ca.pem]` against your HTTP/HTTPS JSON endpoint. Local regression fixtures also exercise JSON upload, HTTP error status, response limits and trusted/untrusted HTTPS without external Internet services.

## 5. Temporary files and directories

`hx_os_temp_create_file` returns an object owning an open file and an exclusive random path. `create_directory` creates an empty private directory. Pass a trusted parent directory; `NULL` selects the platform temporary directory where available. Android requires an explicit authorized directory. `path` and `file` are borrowed.

`close` closes and removes the resource, using bounded no-follow recursion for a directory. Failure retains the owner for cleanup retry; some entries or the open file may already have been consumed. `keep` explicitly retains the path after close. `publish` closes then renames on the same filesystem; it does not copy across filesystems or implicitly request durability. Sync a file before publishing when required, and handle directory durability according to the existing file contract.

POSIX creation uses 0600/0700; Windows inherits parent ACLs. These objects do not secure an adversarial parent. Externally serialize file access, publication and cleanup. C++ `hx::Temp` borrows a durable caller-owned handle so an I/O failure remains retryable; its facade destructor does not delete a retained object. Run `hx_os_temporary_file` to create, write and remove a temporary JSON file.

## 6. Serial byte I/O

`hx_os_serial_open(NULL, "COM3", NULL, &serial, error)` or a POSIX device path opens and configures a serial endpoint. Defaults: 115200 baud, eight data bits, one stop bit, no parity or hardware flow control. Optional settings support 5–8 data bits, one/two stop bits, odd/even parity and RTS/CTS. Unsupported POSIX baud constants or hardware features fail explicitly. Coordinate other applications using the device; Windows opens it exclusively.

Use `read_until` and `write_until` with a monotonic deadline and optional cancellation token. Short transfers are valid; retry only the untransferred suffix. One reader and one writer may run concurrently, with each direction serialized. Windows cancellation waits for native I/O retirement before returning borrowed buffers; driver behavior can exceed the deadline. There is no worker, implicit message framing or background queue. `discard` explicitly drops native pending bytes; close does not silently drain device output. Exclude all I/O callers before close. C++ uses `hx::Serial` and progress-bearing `Result<size_t>`.

Run `hx_os_serial_message COM3` or `/dev/ttyUSB0` with a device that accepts a newline-delimited JSON request and replies the same way. This example's newline protocol belongs to the device workflow. Linux pseudoterminal tests validate software I/O and timeout behavior; they do not validate physical UART timing, parity or flow-control wiring.

## 7. Named shared memory

`hx_os_shared_memory_create` exclusively creates a zeroed writable region; `open` maps the same payload in another process. Names contain 1–64 ASCII letters/digits, `_`, `.` or `-`; payload is 1–64 MiB and 64-byte aligned. Native mapping and page storage are separate from context allocation accounting. The default Windows name is `Local\hxcp_NAME`, within the current session. POSIX uses `/hxcp_NAME`, normally accessible under `/dev/shm/hxcp_NAME` on Linux.

Android has no native `shm_open`; use `create_at/open_at(directory, name, ...)`, with an authorized directory shared by the participants. These file-backed operations also work on desktop platforms and use `directory/hxcp_NAME.shm`. The library does not guess Android app directories, cross sandbox permissions or elevate privileges. `native_available` and general `available` distinguish the backends.

The first 64 bytes are library metadata: ready is a native aligned 32-bit atomic at offset 0; `HXSM` is at offset 4, version 1 at offset 8, little-endian 64-bit payload size at offset 16, payload at offset 64. Remaining metadata bytes are reserved and zero. Acquire-ready before reading metadata; initialization-in-progress returns `WOULD_BLOCK`. The header is immutable after publication. This publishes mapping metadata only: synchronize application payload through a shared protocol or an external channel. Do not put process-local pointers, mutexes or non-lock-free atomic storage into shared memory.

POSIX names persist until explicit unlink; live views survive unlink. Windows native mappings vanish after the last handle closes and do not support explicit unlink. File-backed names persist until removed; deletion follows native filesystem rules. Close requires exclusion of all view users and retains the owner after an unmap failure. External truncation can fault a mapping; coordinate backing-file size and header writes. Stale incomplete regions are reported, never automatically overwritten. Shared memory is not a security boundary against a participant that can mutate it.

Run `hx_os_shared_memory` for native mapping, or `hx_os_shared_memory .` for file-backed mapping. The program starts a bounded child process which reads published JSON using a release/acquire application flag. C++ `hx::SharedMemory` borrows a durable owner for explicit close. Other languages must implement the same metadata and application synchronization rules.

## 8. Modules and direct SDK calls

| Feature | Switch / dependency |
| --- | --- |
| Memory compression | `HX_OS_ENABLE_COMPRESSION`; zlib, liblzma, zstd; independent of ARCHIVE |
| URL parsing | `HX_OS_ENABLE_URL`; libcurl |
| HTTP/HTTPS | `HX_OS_ENABLE_HTTP`; libcurl / OpenSSL |
| CLI | `HX_OS_ENABLE_CLI`; Windows Shell API for process arguments |
| Temporary resources | `HX_OS_ENABLE_TEMP`; RANDOM and FILE_UTILS; PATHS for default parent |
| Serial | `HX_OS_ENABLE_SERIAL`; native device APIs |
| Shared memory | `HX_OS_ENABLE_SHARED_MEMORY`; VIRTUAL_MEMORY and native lock-free 32-bit atomics |

Factories report `NOT_SUPPORTED` when their required modules are absent. CMake defaults enable these features, with an embedded minimal profile in `cmake/minimal.cmake`. Applications can also link [native dependency targets](NATIVE_DEPENDENCIES.md) and call their unmodified SDK APIs directly. Public examples are independent of the ignored local regressions.

On Android, HTTP/HTTPS callers supply an application-approved CA bundle through `ca_file` when the selected curl/OpenSSL SDK has no default CA bundle. Verification stays enabled; this does not bypass Android sandbox or application trust policy. The standalone TLS module has its own system-root adapter.

On Android or hosts without child-process spawning, start `hx_os_shared_memory --write demo .` and `hx_os_shared_memory --read demo .` as separate processes in the same authorized working directory. The writer publishes JSON and waits up to ten seconds for the reader acknowledgement, then cleans up. Use a different name for an independent session.
