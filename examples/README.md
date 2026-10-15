# Runnable examples: start with a concrete task

[简体中文](README.zh-CN.md) · [Usage tutorial](../docs/USAGE.md) · [Library README](../README.md)

Programs are organized by task and show inputs, calls, results and cleanup; their guides identify supporting headers and plugin sources. Pick the closest task, read and run it, then substitute your data and processing. These are public usage examples and do not require private maintainer regressions.

The installed SDK includes these public sources and guides in `share/doc/hx_c_platform/examples`. When reading an installed package, use the [installed consumer setup](../docs/INSTALL.md), copying the selected source and its local headers. The target-build commands below apply to a full source checkout.

<a id="run"></a>

## 1. Build only the program you want

Start in the library checkout. `build` means an **existing build directory for the matching toolchain**; replace it with your path, such as `build-shared-msvc` or `build-shared-linux`. For a first build, complete [dependency setup](../docs/DEPENDENCIES.md); the default SDK modules need matching providers. For a small C-only start, use the [trimmed source setup](../README.md#section-2) with the selected example as main.

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_binary_stream
```

If your existing configuration is Debug, change both the build option and executable path to Debug. Do not share Windows/WSL build directories. With examples enabled, CMake finds a C++ compiler automatically and adds the C++20 example only if its `<coroutine>` probe succeeds; no separate switch is needed. Without a C++ compiler, only the C examples are generated.

Run inside that build directory so generated demo files remain there. Windows Visual Studio:

```powershell
cd build
.\bin\Release\hx_os_binary_stream.exe
```

Linux/WSL:

```sh
cd build
./bin/hx_os_binary_stream
```

No input file is needed. Output is `device=42 temperature=23.5`: the program writes an ID and temperature, then reads them using the same byte order. Replace both target and executable names with the selected item below. Windows Ninja normally places executables directly in `bin`.

Complete programs include creation and cleanup; shorter topic snippets may show only one step. Do not combine multiple main functions or omit error cleanup. Read the reported status on failure; disabled modules explicitly report unsupported.

<a id="inputs"></a>

## 2. Prepare input files and understand writes

Run these commands inside the build directory entered above. Prepare a demo file containing only `0123456789`:

```powershell
Set-Content -LiteralPath demo.txt -Value '0123456789' `
    -NoNewline -Encoding ascii
```

On Linux/WSL:

```sh
printf '0123456789' > demo.txt
```

Pass `demo.txt` to `text_file`, `read_lines`, `positioned_read`, `mapped_file` or `native_file`. Windows example: `.\bin\Release\hx_os_positioned_read.exe demo.txt`; Linux: `./bin/hx_os_positioned_read demo.txt`. Output is `34 35 36 37`, the bytes of 4567 starting at offset 4.

| Program | Input/arguments | Changes made |
| --- | --- | --- |
| `hx_os_text_file` | `demo.txt` or `demo.txt copied.txt` | A second path saves a copy and can replace that destination |
| `hx_os_read_lines` | `demo.txt` | Read-only; add several lines to observe line iteration |
| `hx_os_file_attributes` | `demo.txt` | Updates this demo file's modification time |
| `hx_os_find_files` | `../examples "*.c"` | Read-only scan; quote the pattern to prevent shell expansion |
| `hx_os_watch_directory` | An existing directory | Watches for five seconds; edit a file from another terminal |
| `hx_os_config_save` | No arguments, or `saved.json` | Prints JSON without a path; otherwise saves there |
| `hx_os_config_edit` | No arguments, or `--restore` | Uses preferences.ini here; backs up an existing original |
| `hx_os_buffered_file` | No arguments | Writes samples.jsonl |
| `hx_os_persistent_queue` | `publish records.db`, then `consume records.db` | Creates/updates SQLite storage; acknowledgments remove messages |

Restoration needs a backup from a previous successful save. The first save to a new path has no old version to back up. Use demonstration files for write examples rather than live configuration.

<a id="data"></a>

## 3. Text, arrays and containers

| When to use it | Source and build target | Main result |
| --- | --- | --- |
| [Encode bytes as Base64 and restore them](../docs/ENCODING.md) | [encoding.c](encoding.c) · `hx_os_encoding` | `Base64: AAH+/w==`; 4 bytes |
| [Unicode case folding](../docs/UNICODE.md) | [unicode_tools.c](unicode_tools.c) · `hx_os_unicode_tools` | `strasse` |
| [Split fields, parse integers, natural comparison](../docs/TEXT_UTILITIES.md) | [text_utilities.c](text_utilities.c) · `hx_os_text_utilities` | `Port: 443, offset: -42` |
| [C99 brace formatting](../docs/FORMAT.md) | [format.c](format.c) · `hx_os_format` | `张三: 3.75` |
| [FIFO integer storage](../docs/CONTAINERS.md) | [containers.c](containers.c) · `hx_os_containers` | `Queue result: 42` |
| [Ordered red-black traversal with duplicate keys](../docs/CONTAINERS.md) | [ordered_map.c](ordered_map.c) · `hx_os_ordered_map` | `a: 10`, `b: 20`, `b: 21` |
| [Sort, query, filter and total records](../docs/ALGORITHMS.md) | [algorithms.c](algorithms.c) · `hx_os_algorithms` | IDs 17, 23, 31; 2 pending |
| [C++ array and list algorithms](../docs/ALGORITHMS.md) | [algorithms_cpp.cpp](algorithms_cpp.cpp) · `hx_os_algorithms_cpp` | Ordered IDs |
| [Growable byte queue and two-span fixed ring](../docs/RING.md) | [byte_queue_ring.c](byte_queue_ring.c) · `hx_os_byte_queue_ring` | `ByteQueue: 5 bytes: hello`; `Ring: efxxxxxx` |
| [Track numbered job completion](../docs/BITSET.md) | [bitset.c](bitset.c) · `hx_os_bitset` | `Completed: 11 of 100` |
| [Cache recent results and expire entries](../docs/CACHE.md) | [lru_cache.c](lru_cache.c) · `hx_os_lru_cache` | sensor-b evicted; temporary expired |
| [Sampling, radix sorting, multi-pattern matching](../docs/FAST_TOOLS.md) | [fast_algorithms.c](fast_algorithms.c) · `hx_os_fast_algorithms` | `First error: byte 16` |
| [Varint, ZigZag and UUID v7](../docs/INTEGER_TOOLS.md) | [integer_tools.c](integer_tools.c) · `hx_os_integer_tools` | `300 uses 2 bytes; decoded: 300` |
| [Capture fields and replace text](../docs/REGEX.md) | [regex.c](regex.c) · `hx_os_regex` | `Door_01: #42` |
| [Store different types in one variable](../docs/VARIANT.md) | [variant.c](variant.c) · `hx_os_variant` | integer, text, structure |

<a id="config"></a>

## 4. Configuration, parameters and parsing

| When to use it | Source and build target | Main result |
| --- | --- | --- |
| [Read a port from built-in TOML](../docs/CONFIGURATION.md) | [configuration.c](configuration.c) · `hx_os_configuration` | `Server port: 8080` |
| [Print JSON or save to a supplied file](../docs/CONFIGURATION.md) | [config_save.c](config_save.c) · `hx_os_config_save` | Saved with a path |
| [Create INI, save backup, restore explicitly](../docs/CONFIG_EDIT.md) | [config_edit.c](config_edit.c) · `hx_os_config_edit` | `Saved preferences.ini` |
| [Round-trip structures and nested documents](../docs/SERIALIZATION.md) | [serialization.c](serialization.c) · `hx_os_serialization` | JSON by default |
| [Declare structure and field mapping together](../docs/SERIALIZATION.md) | [codec_macros.c](codec_macros.c) · `hx_os_codec_macros` | JSON; `port = 8080` |
| [Build a mixed-type nested tree](../docs/SERIALIZATION.md) | [nested_document.c](nested_document.c) · `hx_os_nested_document` | serialized JSON |
| [Edit settings and obtain snapshots](../docs/COMMON_UTILITIES.md) | [settings.c](settings.c) · `hx_os_settings` | values and JSON |
| [Pass named text, integer and byte fields](../docs/PARAMETERS.md) | [parameters.c](parameters.c) · `hx_os_parameters` | `Device 42 (camera): 4 binary bytes` |
| [Read custom syntax and skip an extension block](../docs/PARSING.md) | [parse_script.c](parse_script.c) · `hx_os_parse_script` | `Title: Camera` |
| [Expand runtime text constants and conditions](../docs/PREPROCESSING.md) | [preprocess_config.c](preprocess_config.c) · `hx_os_preprocess_config` | JSON: Camera, 8080, true |

<a id="threads"></a>

## 5. Threads, coroutines and notifications

| When to use it | Source and build target | Main result |
| --- | --- | --- |
| [Submit background work and retrieve its result](../docs/CPP_POOL.md) | [thread_pool.cpp](thread_pool.cpp) · `hx_os_cpp_thread_pool` | `Task result: 42` |
| [Start a coroutine and receive through a channel](../docs/COROUTINES.md) | [go.cpp](go.cpp) · `hx_os_go` | `Answer: 42` |
| [C99 stackless state machine](../docs/COROUTINES.md) | [coroutine.c](coroutine.c) · `hx_os_coroutine` | successive Count values |
| [Optional C++20 co_await syntax](../docs/COROUTINES.md) | [coroutine20.cpp](coroutine20.cpp) · `hx_os_cpp_coroutine20` | `Answer: 42` |
| [Batched producer/consumer records](../docs/QUEUES.md) | [message_queue.c](message_queue.c) · `hx_os_message_queue` | 6 device samples |
| [Cancel, wait and protect shared data](../docs/COORDINATION.md) | [coordination.c](coordination.c) · `hx_os_coordination` | `Processed ... records, then stopped cooperatively.` |
| [Share one initialization across workers](../docs/ONCE.md) | [once.c](once.c) · `hx_os_once` | 1 initialization; 4 worker lines |
| [Publish a result with release/acquire](../docs/ATOMICS.md) | [atomic_publication.c](atomic_publication.c) · `hx_os_atomic_publication` | `answer: 42` |
| [Stopwatch, backoff and token bucket](../docs/CONTROL.md) | [control.c](control.c) · `hx_os_control` | 3 retry delays; elapsed time |
| [Subscribe to every signal on a handler](../docs/SIGNALS.md) | [signal_observer.c](signal_observer.c) · `hx_os_signal_observer` | `ready: 42 items`; `saved: 42 items` |
| [Mutate parameters before observers read them](../docs/SIGNALS.md) | [signal_mutable.c](signal_mutable.c) · `hx_os_signal_mutable` | Observer/Caller: volume = 60 |
| [Interval statistics and approximate percentiles](../docs/PROFILER.md) | [profile_statistics.c](profile_statistics.c) · `hx_os_profile_statistics` | 12 samples; mean, deviation, P50/P95/P99 |
| [Named procedures, text splitting and time conversion](../docs/COMMON_UTILITIES.md) | [common_tasks.c](common_tasks.c) · `hx_os_common_tasks` | `procedure result: 42` |

<a id="io"></a>

## 6. Files, streams and platform objects

| When to use it | Source and build target | Main result |
| --- | --- | --- |
| [Check basic linking and UTF conversions](../docs/CONTRACTS.md) | [hello.c](hello.c) · `hx_os_hello` | Platform, byte/codepoint counts and UTF-16 units |
| [Complete one asynchronous timer request](../docs/PLATFORM_UTILITIES.md) | [async_timer.c](async_timer.c) · `hx_os_async_timer` | `Timer completed through ...`, with the native backend |
| [Read text and optionally save a copy](../docs/PLATFORM_UTILITIES.md) | [text_file.c](text_file.c) · `hx_os_text_file` | input text |
| [Read text line by line](../docs/LINE_READER.md) | [read_lines.c](read_lines.c) · `hx_os_read_lines` | one output line per input line |
| [Read four bytes at offset 4](../docs/PLATFORM_UTILITIES.md) | [positioned_read.c](positioned_read.c) · `hx_os_positioned_read` | demo.txt → `34 35 36 37` |
| [Map an existing file read-only](../docs/PLATFORM_UTILITIES.md) | [mapped_file.c](mapped_file.c) · `hx_os_mapped_file` | mapped size and CRC32 |
| [Explicitly update modification time](../docs/PLATFORM_UTILITIES.md) | [file_attributes.c](file_attributes.c) · `hx_os_file_attributes` | `Modification time updated; access time preserved.` |
| [Stream paths during a recursive scan](../docs/FILE_SEARCH.md) | [find_files.c](find_files.c) · `hx_os_find_files` | paths and Files count |
| [Watch a directory for five seconds](../docs/PLATFORM_UTILITIES.md) | [watch_directory.c](watch_directory.c) · `hx_os_watch_directory` | change events; Watch closed |
| [Borrow a native handle to query file size](../docs/NATIVE_INTEROP.md) | [native_file.c](native_file.c) · `hx_os_native_file` | `Native query: ... bytes` |
| [Write and read big-endian numbers](../docs/STREAMS.md) | [binary_stream.c](binary_stream.c) · `hx_os_binary_stream` | `device=42 temperature=23.5` |
| [Frame JSON with a length prefix and read it](../docs/FRAMES.md) | [framed_messages.c](framed_messages.c) · `hx_os_framed_messages` | `{"device":"door-1","request":42}` |
| [Write three JSONL records in the background](../docs/STREAMS.md) | [buffered_file.c](buffered_file.c) · `hx_os_buffered_file` | samples.jsonl; Saved 3 device readings |
| [Query OS version and sampled process CPU usage](../docs/PLATFORM_UTILITIES.md) | [system_info.c](system_info.c) · `hx_os_system_info` | OS name, Process CPU |
| [Measure elapsed time around a short wait](../docs/CONTROL.md) | [high_resolution_time.c](high_resolution_time.c) · `hx_os_high_resolution_time` | Requested 20 ms; measured value varies |
| [Request sleep inhibition for a work interval](../docs/LINUX_DESKTOP.md) | [sleep_inhibit.c](sleep_inhibit.c) · `hx_os_sleep_inhibit` | requested/released or unsupported |

<a id="sdk"></a>

## 7. Crypto, compression and external services

| When to use it | Source and build target | Main result |
| --- | --- | --- |
| [Encrypt and restore text with a password](../docs/CRYPTO.md) | [encrypt_text.c](encrypt_text.c) · `hx_os_encrypt_text` | `Decrypted: Hello, UTF-8!` |
| [Compute SHA-256 incrementally](../docs/INCREMENTAL_HASH.md) | [incremental_hash.c](incremental_hash.c) · `hx_os_incremental_hash` | 64-character digest of abc |
| [Compress bytes and restore JSON](../docs/UTILITY_EXTENSIONS.md) | [compress_memory.c](compress_memory.c) · `hx_os_compress_memory` | byte counts and original JSON |
| [Pack, list or extract files/directories](../docs/ARCHIVE.md) | [archive_files.c](archive_files.c) · `hx_os_archive_files` | entries and processed counts |
| [Encode URL query parameters](../docs/UTILITY_EXTENSIONS.md) | [url_query.c](url_query.c) · `hx_os_url_query` | URL and decoded name |
| [Post JSON to your HTTP/HTTPS endpoint](../docs/UTILITY_EXTENSIONS.md) | [http_json.c](http_json.c) · `hx_os_http_json` | HTTP status and response |
| [Store records and acknowledge successful handling](../docs/PERSISTENT_QUEUE.md) | [persistent_queue.c](persistent_queue.c) · `hx_os_persistent_queue` | 3 Handle recognition-... lines |
| [Send a JSON line to a serial device and read its reply](../docs/UTILITY_EXTENSIONS.md) | [serial_message.c](serial_message.c) · `hx_os_serial_message` | device reply or timeout |
| [Pass JSON between parent/child through shared memory](../docs/UTILITY_EXTENSIONS.md) | [shared_memory.c](shared_memory.c) · `hx_os_shared_memory` | `Child received: ...` |
| [Call the same native SDKs directly](../docs/NATIVE_DEPENDENCIES.md) | [native_dependencies.c](native_dependencies.c) · `hx_os_native_dependencies` | native results |

<a id="network"></a>

## 8. Bidirectional JSON: choose a protocol

For IP interface selection and a small multicast discovery datagram, build `hx_os_network_interfaces` and `hx_os_multicast_discovery`. The [network options guide](../docs/NETWORK_OPTIONS.md) shows the two-terminal commands, JSON payload and expected output, alongside keepalive, dual-stack and broadcast setup.

These programs include both endpoints and send/receive threads. Start locally, then adapt addresses and fields following the topic guide. Runners launch public programs, choose ports and check business results without private regression fixtures.

| Need | Runner | Guide |
| --- | --- | --- |
| Local processes exchanging device/request-tagged messages | run_ipc_business.py | [IPC business flow](../docs/IPC_BUSINESS.md) |
| TCP orders and asynchronous receipts | run_tcp_business.py | [TCP business flow](../docs/TCP_BUSINESS.md) |
| UDP measurements with missing/duplicate/late sequences | run_udp_business.py | [UDP business flow](../docs/UDP_BUSINESS.md) |
| Reliable UDP transport using KCP | run_kcp_business.py | [KCP business flow](../docs/KCP_BUSINESS.md) |
| Threaded sending and receiving on one TLS connection | run_tls_business.py | [TLS flow and certificates](../docs/TLS.md) |

Run scripts from the library checkout, for example `python examples/run_tcp_business.py --bin-dir build/bin/Release`. Linux uses `python3` and `build/bin`. Build the two endpoints listed in each guide first. TLS also needs a certificate, key and trusted CA. Supply a real HTTP service URL; example addresses do not provide a service.

**Choose the API layer first.** Use IPC when the library should manage device/request IDs, sessions and message queues. Use the independent TCP, UDP or TLS examples when integrating an existing wire protocol. Adding JSON to a socket does not implement HXIP.

| Example | What the peer must send |
| --- | --- |
| IPC device control | HXIP v1 frames with JSON bodies; see the IPC protocol guide |
| TCP orders | Two-byte big-endian body length, then UTF-8 JSON |
| UDP samples | One UTF-8 JSON document per datagram, with no length prefix |
| KCP blocks | One complete message-mode KCP message containing JSON; drive KCP continuously |
| TLS readings | Four-byte big-endian body length, then UTF-8 JSON inside TLS |

Python only replaces opening two terminals manually; it does not enter the business processes. For `Build the example first`, build the guide's two targets and check that the Windows Debug/Release path matches the configuration. `Listening:` means ready to accept; final success messages from both peers demonstrate the completed workflow. To move a C example into your own project, copy the companion headers listed in its guide along with main.

The serial example uses 115200 baud, eight data bits, one stop bit and no parity. It sends `{"request":42}` plus newline and expects one response line. Connect a compatible device or virtual serial pair first; starting the program alone does not establish communication. Shared memory, desktop sleep inhibition and Windows GPU queries have their own platform/permission requirements.

<a id="plugin"></a>

## 9. A public plugin and its loader

From the library checkout, build these two targets only:

```sh
cmake --build build --config Release \
  --target hx_os_demo_plugin hx_os_load_plugin
```

These continuations are for a POSIX shell; PowerShell can use one line or backtick continuations. Enter build, then on Windows run:

```powershell
.\bin\Release\hx_os_load_plugin.exe `
    .\bin\Release\hx_os_demo_plugin.dll
```

On Linux:

```sh
./bin/hx_os_load_plugin ./bin/libhx_os_demo_plugin.so
```

Output is `Compatible plugin: Example plugin 1.0`. Public [demo_plugin.c](demo_plugin.c) exports an ABI version and description. [load_plugin.c](load_plugin.c) opens it, checks the ABI, binds/calls the functions and unloads it. The library file is not an executable. Your replacement plugin must preserve agreed symbols and calling conventions; no private test module is required.

## 10. Adapt an example to application code

Replace inputs and values first, then the code that processes results. Preserve creation, error checks and release order. Stop new work and wait for workers before releasing active data; a timeout does not make that data safe to destroy. Times, UUIDs, ciphertext and CPU/GPU details vary by environment, so compare their meaning rather than fixed values.

Outputs here describe expected usage, while actual target support follows the [platform boundaries](../docs/PLATFORMS.md). Detailed capacity, concurrency, cancellation and unloading contracts remain in the later sections of each topic guide.
