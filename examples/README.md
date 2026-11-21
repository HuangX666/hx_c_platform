# Runnable Examples: Start with a Concrete Task

[简体中文](README.zh-CN.md) · [Getting-Started Guide](../docs/USAGE.md) · [Back to README](../README.md)

Every example in this directory is built around a specific engineering task and shows the complete lifecycle: **preparing inputs → calling the API → checking status/errors → releasing resources**. Pick the example closest to what you are building, run it to verify the output, and then substitute your own data and application logic.

> [!NOTE]
> In an installed SDK package, these public example sources and guides are located under `share/doc/hx_c_platform/examples`. If you are working from a prebuilt SDK rather than a full source checkout, follow the [Installed SDK Guide](../docs/INSTALL.md) to copy the selected `.c` / `.cpp` source and its companion headers into your project.

<a id="run"></a>

## 1. Build Only the Example You Need

Run the commands below from the root of the repository checkout (replace `build` with your existing build directory for the active toolchain, such as `build-msvc` or `build-linux`):

- **Full SDK builds**: Prepare the required native dependency SDKs first following the [Dependency Build Guide](../docs/DEPENDENCIES.md).
- **Minimal C-only builds**: If you only want to run a basic core C example without building external SDKs, you can also use the [starter CMake configuration](../README.md#section-2) and compile the selected example file as `main.c`.

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream
```

> [!TIP]
> - When `HX_OS_BUILD_EXAMPLES=ON` is enabled, CMake automatically checks for a C++ compiler and adds the C++20 coroutine example if `<coroutine>` is supported—no extra switch is required. If no C++ compiler is present, only the C examples are generated.
> - For Debug builds, change `Release` to `Debug` in both the build command and the executable path below. Keep Windows and WSL build directories separate.

Enter the build directory before running the executable so any temporary files created by the demo stay inside `build/`:

```powershell
# Windows (Visual Studio multi-config generator)
cd build
.\bin\Release\hx_os_binary_stream.exe
```

```sh
# Linux / WSL (or Windows Ninja single-config generator)
cd build
./bin/hx_os_binary_stream
```

`hx_os_binary_stream` requires no input files and prints `device=42 temperature=23.5` (writing a device ID and temperature in big-endian byte order and reading them back). To run any other example below, substitute its CMake target name and executable name.

<a id="inputs"></a>

## 2. Prepare Input Files for File and Configuration Examples

Run the commands below inside the `build` directory entered above. First, create a 10-byte ASCII test file named `demo.txt` containing `0123456789`:

```powershell
# Windows PowerShell
Set-Content -LiteralPath demo.txt -Value '0123456789' `
    -NoNewline -Encoding ascii
```

```sh
# Linux / WSL
printf '0123456789' > demo.txt
```

Pass `demo.txt` to the file examples. For instance, running `hx_os_positioned_read demo.txt` (`.\bin\Release\hx_os_positioned_read.exe demo.txt` on Windows or `./bin/hx_os_positioned_read demo.txt` on Linux) prints `34 35 36 37`—the hexadecimal bytes for `4567` starting at byte offset 4.

| Target Name | Command-Line Arguments | What It Does |
| --- | --- | --- |
| `hx_os_text_file` | `demo.txt` or `demo.txt copied.txt` | Prints the file text; when a second path is given, saves a copy to that path (overwriting any existing file) |
| `hx_os_read_lines` | `demo.txt` | Read-only line-by-line scan; add multiple lines to `demo.txt` to observe line iteration |
| `hx_os_file_attributes` | `demo.txt` | Updates `demo.txt`'s modification timestamp while preserving its access time |
| `hx_os_find_files` | `../examples "*.c"` | Read-only recursive directory scan; quote the glob pattern so the shell does not expand it early |
| `hx_os_watch_directory` | Path to an existing directory | Watches the directory for 5 seconds; create or edit a file in another terminal to see events |
| `hx_os_config_save` | No arguments, or `saved.json` | Prints JSON to stdout when run without arguments, or saves atomically to the given path |
| `hx_os_config_edit` | No arguments, or `--restore` | Reads/writes `preferences.ini` in the current directory and backs up any existing file before overwriting |
| `hx_os_buffered_file` | No arguments | Writes 3 JSONL records in the background to `samples.jsonl` |
| `hx_os_persistent_queue` | `publish records.db`, then `consume records.db` | Creates/updates the SQLite queue file `records.db` and removes messages once acknowledged |

> [!WARNING]
> - Before running `hx_os_config_edit --restore`, run `hx_os_config_edit` without arguments at least twice so an existing `preferences.ini` is backed up (saving a brand-new file for the first time has no prior version to back up).
> - Always use disposable demo files such as `demo.txt` when running examples that modify files or timestamps.

<a id="data"></a>

## 3. Text, Arrays, and Containers

| When to Use It & Guide Link | Source & Build Target | Expected Output Summary |
| --- | --- | --- |
| [Encode bytes as Base64 and decode them back](../docs/ENCODING.md) | [encoding.c](encoding.c) · `hx_os_encoding` | `Base64: AAH+/w==`; 4 decoded bytes |
| [Locale-independent Unicode case folding](../docs/UNICODE.md) | [unicode_tools.c](unicode_tools.c) · `hx_os_unicode_tools` | `strasse` (case-folds German `Straße`) |
| [Zero-copy field splitting, strict integers, natural sort](../docs/TEXT_UTILITIES.md) | [text_utilities.c](text_utilities.c) · `hx_os_text_utilities` | `Port: 443, offset: -42` |
| [C99 brace `{}` formatted printing](../docs/FORMAT.md) | [format.c](format.c) · `hx_os_format` | `张三: 3.75` |
| [FIFO integer queue operations](../docs/CONTAINERS.md) | [containers.c](containers.c) · `hx_os_containers` | `Queue result: 42` |
| [Ordered red-black tree map with duplicate keys](../docs/CONTAINERS.md) | [ordered_map.c](ordered_map.c) · `hx_os_ordered_map` | `a: 10`, `b: 20`, `b: 21` |
| [Sort, binary-search, filter, and aggregate C structs](../docs/ALGORITHMS.md) | [algorithms.c](algorithms.c) · `hx_os_algorithms` | Sorted IDs `17, 23, 31`; 2 pending items |
| [C++ array and list iterator algorithms](../docs/ALGORITHMS.md) | [algorithms_cpp.cpp](algorithms_cpp.cpp) · `hx_os_algorithms_cpp` | Ordered ID sequence |
| [Growable byte queue and two-span fixed ring buffer](../docs/RING.md) | [byte_queue_ring.c](byte_queue_ring.c) · `hx_os_byte_queue_ring` | `ByteQueue: 5 bytes: hello`; `Ring: efxxxxxx` |
| [Track numbered job completion with a dynamic bitset](../docs/BITSET.md) | [bitset.c](bitset.c) · `hx_os_bitset` | `Completed: 11 of 100` |
| [Bounded LRU cache with per-entry TTL expiration](../docs/CACHE.md) | [lru_cache.c](lru_cache.c) · `hx_os_lru_cache` | `sensor-b` evicted; `temporary` expired |
| [Random sampling, radix sort, and multi-pattern byte search](../docs/FAST_TOOLS.md) | [fast_algorithms.c](fast_algorithms.c) · `hx_os_fast_algorithms` | `First error: byte 16` |
| [Varint, ZigZag integers, and UUID v7 generation](../docs/INTEGER_TOOLS.md) | [integer_tools.c](integer_tools.c) · `hx_os_integer_tools` | `300 uses 2 bytes; decoded: 300` |
| [UTF-8 regex capture groups and text replacement](../docs/REGEX.md) | [regex.c](regex.c) · `hx_os_regex` | `Door_01: #42` |
| [Hold heterogeneous types and structs in one Variant](../docs/VARIANT.md) | [variant.c](variant.c) · `hx_os_variant` | Stores integer, string, and custom struct |

<a id="config"></a>

## 4. Configuration, Parameter Packs, and Parsing

| When to Use It & Guide Link | Source & Build Target | Expected Output Summary |
| --- | --- | --- |
| [Read a server port from built-in TOML](../docs/CONFIGURATION.md) | [configuration.c](configuration.c) · `hx_os_configuration` | `Server port: 8080` |
| [Print formatted JSON or save atomically to a file](../docs/CONFIGURATION.md) | [config_save.c](config_save.c) · `hx_os_config_save` | Prints JSON, or `Saved` when given a path |
| [Generate INI files, save backups, and restore explicitly](../docs/CONFIG_EDIT.md) | [config_edit.c](config_edit.c) · `hx_os_config_edit` | `Saved preferences.ini` |
| [Round-trip C structs and nested configuration documents](../docs/SERIALIZATION.md) | [serialization.c](serialization.c) · `hx_os_serialization` | JSON by default (or yaml/xml/toml) |
| [Declare structs and field schemas together with macros](../docs/SERIALIZATION.md) | [codec_macros.c](codec_macros.c) · `hx_os_codec_macros` | Serialized JSON and `port = 8080` |
| [Construct a mixed-type nested document tree in code](../docs/SERIALIZATION.md) | [nested_document.c](nested_document.c) · `hx_os_nested_document` | Serialized nested JSON |
| [Mutate runtime settings and take immutable snapshots](../docs/COMMON_UTILITIES.md) | [settings.c](settings.c) · `hx_os_settings` | Read values and exported JSON |
| [Pass named string, integer, and binary fields](../docs/PARAMETERS.md) | [parameters.c](parameters.c) · `hx_os_parameters` | `Device 42 (camera): 4 binary bytes` |
| [Tokenize custom syntax and skip nested extension blocks](../docs/PARSING.md) | [parse_script.c](parse_script.c) · `hx_os_parse_script` | `Title: Camera` |
| [Expand runtime constants and conditionals in config text](../docs/PREPROCESSING.md) | [preprocess_config.c](preprocess_config.c) · `hx_os_preprocess_config` | Expanded JSON (`Camera, 8080, true`) |

<a id="threads"></a>

## 5. Threads, Coroutines, Queues, and Notifications

| When to Use It & Guide Link | Source & Build Target | Expected Output Summary |
| --- | --- | --- |
| [Submit background work to a C++11 thread pool](../docs/CPP_POOL.md) | [thread_pool.cpp](thread_pool.cpp) · `hx_os_cpp_thread_pool` | `Task result: 42` |
| [Launch a stackful coroutine and receive via a channel](../docs/COROUTINES.md) | [go.cpp](go.cpp) · `hx_os_go` | `Answer: 42` |
| [Run a C99 stackless coroutine state machine](../docs/COROUTINES.md) | [coroutine.c](coroutine.c) · `hx_os_coroutine` | Successive `Count` values |
| [Use optional C++20 `co_await` coroutines](../docs/COROUTINES.md) | [coroutine20.cpp](coroutine20.cpp) · `hx_os_cpp_coroutine20` | `Answer: 42` |
| [Batch producer/consumer messages across threads](../docs/QUEUES.md) | [message_queue.c](message_queue.c) · `hx_os_message_queue` | 6 device sample records |
| [Coordinate cancellation, `WaitGroup`, and RW locks](../docs/COORDINATION.md) | [coordination.c](coordination.c) · `hx_os_coordination` | `Processed ... records, then stopped cooperatively.` |
| [Run one-time initialization across worker threads](../docs/ONCE.md) | [once.c](once.c) · `hx_os_once` | 1 init line; 4 worker lines |
| [Publish shared data with `release` / `acquire` atomics](../docs/ATOMICS.md) | [atomic_publication.c](atomic_publication.c) · `hx_os_atomic_publication` | `answer: 42` |
| [Stopwatch timing, exponential backoff, and token bucket](../docs/CONTROL.md) | [control.c](control.c) · `hx_os_control` | 3 retry delays and elapsed time |
| [Observe all signals emitted on a handler](../docs/SIGNALS.md) | [signal_observer.c](signal_observer.c) · `hx_os_signal_observer` | `ready: 42 items`; `saved: 42 items` |
| [Mutate shared signal arguments in callbacks](../docs/SIGNALS.md) | [signal_mutable.c](signal_mutable.c) · `hx_os_signal_mutable` | `Observer/Caller: volume = 60` |
| [Record hierarchical interval stats and P50/P95/P99](../docs/PROFILER.md) | [profile_statistics.c](profile_statistics.c) · `hx_os_profile_statistics` | 12 samples with mean, stddev, and percentiles |
| [Call named procedures, split text, and convert times](../docs/COMMON_UTILITIES.md) | [common_tasks.c](common_tasks.c) · `hx_os_common_tasks` | `procedure result: 42` |

<a id="io"></a>

## 6. Files, Binary Streams, and Platform Objects

| When to Use It & Guide Link | Source & Build Target | Expected Output Summary |
| --- | --- | --- |
| [Verify core linking and UTF-8 / UTF-16 conversion](../docs/CONTRACTS.md) | [hello.c](hello.c) · `hx_os_hello` | Platform name, UTF-8 byte/codepoint counts, UTF-16 bytes |
| [Complete an asynchronous timer request on an event loop](../docs/PLATFORM_UTILITIES.md) | [async_timer.c](async_timer.c) · `hx_os_async_timer` | `Timer completed through ...` (native backend name) |
| [Read a text file and optionally save a copy](../docs/PLATFORM_UTILITIES.md) | [text_file.c](text_file.c) · `hx_os_text_file` | Prints input file text |
| [Read UTF-8 text line by line with bounded length](../docs/LINE_READER.md) | [read_lines.c](read_lines.c) · `hx_os_read_lines` | Prints each line without trailing newlines |
| [Read 4 bytes at byte offset 4 without seeking](../docs/PLATFORM_UTILITIES.md) | [positioned_read.c](positioned_read.c) · `hx_os_positioned_read` | `demo.txt` → `34 35 36 37` |
| [Memory-map a file read-only and compute CRC32](../docs/PLATFORM_UTILITIES.md) | [mapped_file.c](mapped_file.c) · `hx_os_mapped_file` | Mapped byte size and CRC32 |
| [Update file modification time while preserving access time](../docs/PLATFORM_UTILITIES.md) | [file_attributes.c](file_attributes.c) · `hx_os_file_attributes` | `Modification time updated; access time preserved.` |
| [Stream matching paths during a recursive directory scan](../docs/FILE_SEARCH.md) | [find_files.c](find_files.c) · `hx_os_find_files` | Matching paths and total `Files` count |
| [Watch a directory for file changes for 5 seconds](../docs/PLATFORM_UTILITIES.md) | [watch_directory.c](watch_directory.c) · `hx_os_watch_directory` | Change events and `Watch closed` |
| [Borrow a native file handle to query file size](../docs/NATIVE_INTEROP.md) | [native_file.c](native_file.c) · `hx_os_native_file` | `Native query: ... bytes` |
| [Write and read big-endian numbers on a binary stream](../docs/STREAMS.md) | [binary_stream.c](binary_stream.c) · `hx_os_binary_stream` | `device=42 temperature=23.5` |
| [Frame JSON messages with a length prefix and read them](../docs/FRAMES.md) | [framed_messages.c](framed_messages.c) · `hx_os_framed_messages` | `{"device":"door-1","request":42}` |
| [Write 3 JSONL records asynchronously in the background](../docs/STREAMS.md) | [buffered_file.c](buffered_file.c) · `hx_os_buffered_file` | Writes `samples.jsonl`; prints `Saved 3 device readings` |
| [Query OS version and sampled process CPU usage](../docs/PLATFORM_UTILITIES.md) | [system_info.c](system_info.c) · `hx_os_system_info` | OS name and `Process CPU` |
| [Configure Windows WER LocalDumps for a customer EXE](../docs/LOCAL_DUMPS.md) | [local_dumps.c](local_dumps.c) · `hx_os_local_dumps` | No args: help; `query`: read-only; `enable`/`remove` require admin |
| [Measure real elapsed time around a short sleep](../docs/CONTROL.md) | [high_resolution_time.c](high_resolution_time.c) · `hx_os_high_resolution_time` | Requests 20 ms wait; prints measured duration |
| [Request scoped idle-sleep inhibition](../docs/LINUX_DESKTOP.md) | [sleep_inhibit.c](sleep_inhibit.c) · `hx_os_sleep_inhibit` | Prints acquired/released or `HX_OS_NOT_SUPPORTED` |

<a id="sdk"></a>

## 7. Cryptography, Compression, and External Services

| When to Use It & Guide Link | Source & Build Target | Expected Output Summary |
| --- | --- | --- |
| [Encrypt and decrypt UTF-8 text with a password](../docs/CRYPTO.md) | [encrypt_text.c](encrypt_text.c) · `hx_os_encrypt_text` | `Decrypted: Hello, UTF-8!` |
| [Compute SHA-256 incrementally across chunks](../docs/INCREMENTAL_HASH.md) | [incremental_hash.c](incremental_hash.c) · `hx_os_incremental_hash` | 64-character hex digest of `"abc"` |
| [Compress bytes in memory and restore the original JSON](../docs/UTILITY_EXTENSIONS.md) | [compress_memory.c](compress_memory.c) · `hx_os_compress_memory` | Compressed/decompressed sizes and JSON |
| [Pack, list, or extract files and directories](../docs/ARCHIVE.md) | [archive_files.c](archive_files.c) · `hx_os_archive_files` | Archive entry list and processed count |
| [Parse URLs and percent-encode query parameters](../docs/UTILITY_EXTENSIONS.md) | [url_query.c](url_query.c) · `hx_os_url_query` | Encoded URL and decoded parameter |
| [POST JSON to an HTTP/HTTPS endpoint](../docs/UTILITY_EXTENSIONS.md) | [http_json.c](http_json.c) · `hx_os_http_json` | HTTP status code and response body |
| [Enqueue, consume, and ACK persistent SQLite messages](../docs/PERSISTENT_QUEUE.md) | [persistent_queue.c](persistent_queue.c) · `hx_os_persistent_queue` | 3 `Handle recognition-...` lines |
| [Send a JSON line over a serial port and read a reply](../docs/UTILITY_EXTENSIONS.md) | [serial_message.c](serial_message.c) · `hx_os_serial_message` | Device reply (or timeout/error if no port attached) |
| [Exchange JSON between parent and child via shared memory](../docs/UTILITY_EXTENSIONS.md) | [shared_memory.c](shared_memory.c) · `hx_os_shared_memory` | `Child received: ...` |
| [Call the bundled SQLite, OpenSSL, curl, and utf8proc SDKs directly](../docs/NATIVE_DEPENDENCIES.md) | [native_dependencies.c](native_dependencies.c) · `hx_os_native_dependencies` | Native library call results |

<a id="network"></a>

## 8. End-to-End Client/Server JSON Examples

To enumerate local IP network interfaces and send a multicast discovery datagram, build `hx_os_network_interfaces` and `hx_os_multicast_discovery` (see [Socket Options and Discovery](../docs/NETWORK_OPTIONS.md) for the two-terminal commands as well as keepalive, dual-stack, and broadcast setup).

The five business examples below include both server and client endpoints with dedicated send/receive threads. For convenient single-machine testing, each workflow has a companion Python runner script that spawns both endpoints on an available local port:

| Business Workflow | Runner Script | Topic Guide |
| --- | --- | --- |
| Local IPC messages tagged with device and request IDs | `run_ipc_business.py` | [IPC Business Example](../docs/IPC_BUSINESS.md) |
| TCP order submission and asynchronous receipts | `run_tcp_business.py` | [TCP Business Example](../docs/TCP_BUSINESS.md) |
| UDP sensor samples with gap, duplicate, and late detection | `run_udp_business.py` | [UDP Business Example](../docs/UDP_BUSINESS.md) |
| Reliable UDP block transfer over KCP | `run_kcp_business.py` | [KCP Business Example](../docs/KCP_BUSINESS.md) |
| Multi-threaded JSON send/receive over one verified TLS connection | `run_tls_business.py` | [TLS Guide and Certificates](../docs/TLS.md) |

Build the two endpoint targets listed in the chosen guide, then run the script from the repository root:

```sh
# Windows (MSVC Release)
python examples/run_tcp_business.py --bin-dir build/bin/Release

# Linux / WSL
python3 examples/run_tcp_business.py --bin-dir build/bin
```

> [!IMPORTANT]
> **Choosing the Right Networking Layer and Wire Format**:
> - Choose the **IPC module (`HXIP v1`)** when you want the library to manage device IDs, request correlation IDs, sessions, and bounded message queues automatically.
> - Choose the **independent TCP, UDP, KCP, or TLS examples** when integrating with an existing wire protocol (copy both the `.c` file and the companion header listed in its guide):
>
> | Example | Peer Wire Format |
> | --- | --- |
> | **IPC Device Control** | `HXIP v1` frames carrying UTF-8 JSON bodies (see the [IPC Protocol Guide](../docs/IPC.md)) |
> | **TCP Orders** | 2-byte big-endian body length followed by UTF-8 JSON |
> | **UDP Samples** | One UTF-8 JSON document per UDP datagram (no length prefix) |
> | **KCP Blocks** | One complete message-mode KCP message containing UTF-8 JSON (drive KCP continuously) |
> | **TLS Readings** | 4-byte big-endian body length followed by UTF-8 JSON inside the TLS stream |

### Advanced Network I/O and Coroutine Examples

| Task | Source & Build Target | Arguments & Expected Output |
| --- | --- | --- |
| Read UDP destination address and receiving interface index | [udp_packet.c](udp_packet.c) · `hx_os_udp_packet` | No arguments; prints loopback JSON payload, destination address, and interface index |
| Resolve a hostname using cancellable DNS | [dns_lookup.c](dns_lookup.c) · `hx_os_dns_lookup` | `[--async\|--coro] HOST [DNS_IP:PORT]`; prints resolved numeric IP addresses |
| Trusted-LAN KCP session request/reply | [kcp_session.c](kcp_session.c) · `hx_os_kcp_session` | Run `server IP:PORT` then `client IP:PORT`; prints JSON request and reply |
| Stream a file over HTTP/HTTPS | [http_file.c](http_file.c) · `hx_os_http_file` | `download URL FILE [CA]` or `upload URL FILE [CA]`; prints HTTP status |
| Suspend a C99 coroutine on UDP I/O | [coroutine_udp.c](coroutine_udp.c) · `hx_os_coroutine_udp` | No arguments; prints a JSON datagram sent to itself |
| Suspend a C99 coroutine on an HTTP request | [coroutine_http.c](coroutine_http.c) · `hx_os_coroutine_http` | `URL`; prints HTTP status and response body |

- `hx_os_dns_lookup --async 127.0.0.1` and `--coro 127.0.0.1` demonstrate event-loop and coroutine DNS resolution with identical output.
- `hx_os_http_file` and `hx_os_coroutine_http` accept a trailing `--cancellable-dns` flag (requires building the `libcurl` SDK with `HX_DEPS_CURL_CARES=ON`; the default system-DNS SDK returns `HX_OS_NOT_SUPPORTED`). See [Socket Options](../docs/NETWORK_OPTIONS.md#123-use-an-event-loop-or-coroutine) and [HTTP Transfers](../docs/HTTP_FILES.md#4-cancel-even-when-dns-receives-no-reply) for complete commands.

<a id="plugin"></a>

## 9. Dynamic Library Loading: Plugin and Loader

From the repository root, build both the sample plugin (`hx_os_demo_plugin`) and the loader executable (`hx_os_load_plugin`):

```sh
cmake --build build --config Release --target hx_os_demo_plugin hx_os_load_plugin
```

Enter `build/` and pass the compiled shared library path to the loader:

```powershell
# Windows
.\bin\Release\hx_os_load_plugin.exe .\bin\Release\hx_os_demo_plugin.dll
```

```sh
# Linux / WSL
./bin/hx_os_load_plugin ./bin/libhx_os_demo_plugin.so
```

**Expected output**: `Compatible plugin: Example plugin 1.0`

- [demo_plugin.c](demo_plugin.c) exports an ABI version constant and description function.
- [load_plugin.c](load_plugin.c) loads the shared library at runtime, verifies the ABI version, binds and calls the exported symbols, and unloads the library cleanly.

## 10. Adapting an Example into Application Code

When moving an example into production code:

1. **Replace inputs and data first**, then substitute your application logic where the example prints results.
2. **Preserve creation, status checks, and cleanup ordering**: Stop new work admission and wait for background workers before freeing shared state. Never destroy borrowed task data after a timeout (`HX_OS_TIMEOUT`) while background work may still be active.
3. **Check platform and concurrency contracts**: Review [Platform Support](../docs/PLATFORMS.md) for OS-specific limits and the later sections of each topic guide for capacity, cancellation, and plugin-unloading rules.
