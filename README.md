# hx_c_platform

[![C99 core][c-badge]](CMakeLists.txt) [![C++11 facade][cpp-badge]](docs/CPP_POOL.md) [![Header-only C++ facade][header-badge]](docs/CPP_POOL.md) [![CMake 3.16+][cmake-badge]](CMakeLists.txt) [![First-party license pending][license-badge]](THIRD_PARTY_NOTICES.md) [![Linux GCC and Clang][linux-badge]](docs/PLATFORMS.md) [![macOS validation pending][macos-badge]](docs/PLATFORMS.md) [![Windows MSVC][windows-badge]](docs/PLATFORMS.md) [![Embedded Linux target pending][embedded-badge]](docs/PLATFORMS.md) [![Android API 21 ARM][android-badge]](docs/ANDROID.md)

[简体中文](README.zh-CN.md) · [Complete getting-started guide](docs/USAGE.md) · [Find a feature](#section-6) · [API reference](docs/REFERENCE.md)

Choose further tasks from the [runnable example directory](examples/README.md), which lists use cases, build targets, inputs and expected results.

<a id="section-1"></a>

## 1. What this library is for

C/C++ programs often need to read files, parse configuration, edit strings, sort arrays or send work to background threads. `hx_c_platform` provides these common operations through one interface and handles Windows, Linux and other platform differences internally.

For example, `hx_os_file_read_text()` reads a UTF-8 text file, and `hx_os_sort_i32()` sorts integers in ascending order. This page starts with small programs you can run, then points to more features.

```mermaid
flowchart LR
    A["Your C / C++ application"] --> B["Files, configuration, strings, containers, tasks"]
    B --> C["hx_c_platform platform adapters"]
    C --> D["Windows / Linux / Android / macOS"]
```

The compiled core uses C99. Optional C++11 wrappers provide automatic resource management and explicit results. Ordinary use needs no global initialization and has no Qt or OBS runtime dependency. The C++20 coroutine layer is opt-in. See [platform support](docs/PLATFORMS.md) for baselines and limits.

<a id="section-2"></a>
<a id="section-3"></a>

## 2. Prepare a runnable project

Use CMake 3.16+ and a C compiler: an MSVC developer terminal on Windows, or GCC/Clang on Linux. This source-integration example places the third-party library in your project's `deps` directory:

```sh
mkdir hx_demo
cd hx_demo
mkdir deps
cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

Create `CMakeLists.txt` in `hx_demo` with the following content. Use the complete C program in section 3.1 as `main.c`.

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES C)

foreach(module CRYPTO ARCHIVE COMPRESSION HTTP URL UNICODE PERSISTENT_QUEUE)
	set(HX_OS_ENABLE_${module} OFF CACHE BOOL "")
endforeach()

add_subdirectory(deps/hx_c_platform)

add_executable(hx_demo main.c)
target_link_libraries(hx_demo PRIVATE hx_c_platform::hx_c_platform)
set_target_properties(hx_demo PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS NO)
```

This starter configuration keeps files, strings, built-in configuration parsing, containers and thread pools, while disabling modules that require a separately prepared native SDK. When you need HTTP, cryptography or compression, prepare that SDK using the [dependency guide](docs/DEPENDENCIES.md) and enable the corresponding modules.

Build from `hx_demo`:

```sh
cmake -S . -B build
cmake --build build --config Release
```

Run all commands below from the `hx_demo` directory. Select one complete program as `main.c` at a time; replace it and rebuild to try another task. For C++, save `main.cpp` and use the [complete C++ CMake setup](docs/USAGE.md#cpp-project). If you already have an `include/lib/bin` SDK, use the [installed-package setup](docs/INSTALL.md).

<a id="section-4"></a>

## 3. Common operations: run first, then use your own data

### 3.1 Read a text file: C

Suppose your program needs to read `hello.txt` and display its contents. The library opens, reads and closes the file, then stores its text in `text`.

**Prepare the input:** create UTF-8 `hello.txt` in `hx_demo`:

```text
Hello, hx_c_platform!
```

**Complete program:** save as `main.c` and build with section 2's setup.

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;

	hx_os_status status =
		hx_os_file_read_text("hello.txt", &text, NULL);

	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	else
		fprintf(stderr, "Read failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

The three read arguments mean:

| Argument | Purpose here |
| --- | --- |
| `"hello.txt"` | Input path; replace it with your own, such as `"data/config.txt"` |
| `&text` | String that receives the contents |
| `NULL` | This example checks the returned status without requesting detailed native errors |

`HX_STRING_INIT` initializes an empty string. `HX_OS_OK` means the read succeeded. `hx_string_c_str(&text)` returns a character pointer usable by `puts()`. Finally, `hx_string_destroy()` releases the text; the read function has already closed the file.

**Run from `hx_demo`:** use the Windows command or the Linux command below.

```powershell
.\build\Release\hx_demo.exe
```

```sh
./build/hx_demo
```

Normal output is `Hello, hx_c_platform!`. If the file is not found, check that `hello.txt` is in your terminal's current directory: relative paths start there.

For your application, change the path and replace `puts()` with the work that uses the contents. Destroy the string after that work finishes. [More file operations and the C++ version](docs/USAGE.md#files).

### 3.2 Clean up user-entered text: C

Given `"  DEVICE_A  "`, remove surrounding spaces and obtain lowercase `device_a`. Replace `main.c` and rebuild; no input file is needed.

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string name = HX_STRING_INIT;

	hx_os_status status =
		hx_string_assign_cstr(&name, "  DEVICE_A  ", NULL);
	if (status == HX_OS_OK) {
		hx_string_trim_ascii_in_place(&name);
		hx_string_lower_ascii_in_place(&name);
		puts(hx_string_c_str(&name));
	}
	else
		fprintf(stderr, "String failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&name);

	return status == HX_OS_OK ? 0 : 1;
}
```

Output is `device_a`. `assign_cstr` copies the input; `trim_ascii_in_place` removes surrounding ASCII whitespace; `lower_ascii_in_place` converts ASCII capitals. All three calls use the same string, which is destroyed once at the end.

This suits English device names, tags and ordinary ASCII input. Non-ASCII characters remain intact. For normalization or Unicode case folding, use the [Unicode tools](docs/UNICODE.md).

### 3.3 Read a port from JSON configuration: C++11

Create `config.json` in `hx_demo`:

```json
{"port": 8080}
```

Use the [complete C++ build configuration](docs/USAGE.md#cpp-project) and save this as `main.cpp`. It loads the file, then reads `port`.

```cpp
#include <hx/Config.hpp>
#include <cstdio>

int main()
{
	auto config = hx::ConfigDocument::load("config.json");
	if (!config) {
		std::fprintf(stderr, "Config failed: %s\n",
			hx_os_status_name(config.status()));
		return 1;
	}

	auto port = config.value().integer("/port");
	if (!port) {
		std::fprintf(stderr, "Port failed: %s\n",
			hx_os_status_name(port.status()));
		return 1;
	}

	std::printf("Port: %lld\n", static_cast<long long>(port.value()));

	return 0;
}
```

Output is `Port: 8080`. `load` returns a result containing success/failure and, on success, the document. `if (!config)` checks failure; `config.value()` obtains the document after that check. `integer("/port")` reads the root object's integer `port`; `/port` is its location inside JSON.

A missing field or wrong type takes the error branch. Write the number as `8080`; the string `"8080"` has a different type. C++ objects release their resources when they leave scope. [Complete C version, nested objects and error handling](docs/USAGE.md#configuration).

### 3.4 Sort an existing integer array: C

Sort the ordinary C array `{5, 1, 3}` in ascending order. Save this as `main.c`, build and run:

```c
#include <hx_algorithm.h>
#include <stdio.h>

int main(void)
{
	int32_t values[] = {5, 1, 3};

	hx_os_status status = hx_os_sort_i32(values, 3, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Sort failed: %s\n",
			hx_os_status_name(status));
		return 1;
	}

	printf("%d %d %d\n", (int)values[0], (int)values[1],
		(int)values[2]);

	return 0;
}
```

Output is `1 3 5`. `values` is the array to modify, `3` is its element count, and the final `NULL` omits detailed errors. The sorted values stay in the original array. It is a normal local array and requires no library destroy call.

See [algorithms](docs/ALGORITHMS.md) for structure sorting, binary search, filtering and C++ iterators. Use [containers](docs/USAGE.md#containers) when you need a dynamically growing collection.

### 3.5 Run a calculation on a background thread: C++11

For example, calculate `21 × 2` on a worker, then display `42` on the main thread. The complete sequence is:

1. Create a pool with two workers.
2. Submit the calculation and argument `21`.
3. Wait for and obtain its result.
4. Explicitly shut down the pool.

Use the complete contents of [thread_pool.cpp](examples/thread_pool.cpp) as `main.cpp`, build with the C++ setup and expect:

```text
Task result: 42
```

`submit()` accepts the work, `getUntil()` waits for the result, and `shutdownUntil()` closes the pool. The example chooses a two-second budget; applications choose their own. Timeout means work may remain active. A service must retain `owner` and task data while arranging further handling or retrying close.

The [step-by-step tutorial](docs/USAGE.md#thread-pool) includes a complete program, explains each object, and covers failure and shutdown.

### 3.6 Print text and numbers with `{}`: C99

To display a voltage with two decimal places, save this as `main.c`:

```c
#include <hx_print.h>

int main(void)
{
	hx_os_status status = HX_PRINTLN("{}: {:.2f}",
		hx_fmt_str("Voltage"), hx_fmt_f64(3.75));

	return status == HX_OS_OK ? 0 : 1;
}
```

Output is `Voltage: 3.75`. `{}` takes the next argument; `{:.2f}` prints a floating-point value with two decimal places. `hx_fmt_str` identifies a string argument and `hx_fmt_f64` a floating-point argument. Include the header and link normally; this example needs no source generation.

C++ selects formatting from argument types. See [formatting](docs/FORMAT.md) for custom structures and more placeholder forms.

<a id="section-5"></a>

## 4. Remember these rules first

- **Check a C status before using output.** `HX_OS_OK` means success; other statuses depend on the operation. `hx_os_status_name()` returns readable English text.
- **Check a C++ result before obtaining its value.** `if (!result)` checks failure; call `result.value()` after success. Errors are explicit and require no exception handling.
- **Release C resources with their matching function.** Strings use `hx_string_destroy()`, configuration documents use `hx_os_config_release()`, and other interfaces document their cleanup function.
- **Finish closing objects that contain workers or I/O.** After a timeout they may still be active; retain them and any data borrowed by their work.

See [first-use questions](docs/USAGE.md#questions) for concrete explanations.

<a id="section-6"></a>

## 5. Where to find more features

For one-call text/file encryption, SHA-256, HMAC and signatures, see [cryptography with complete C/C++ examples](docs/CRYPTO.md).

| What you need | Where to start |
| --- | --- |
| Native IPC device control | [Independent IPC example](docs/IPC_BUSINESS.md) |
| TCP order submission and receipts | [Independent TCP example](docs/TCP_BUSINESS.md) |
| UDP samples and feedback | [Independent UDP example](docs/UDP_BUSINESS.md) |
| Keepalive, IPv6 dual stack, broadcast, multicast and network interfaces | [Socket options and discovery](docs/NETWORK_OPTIONS.md) |
| KCP reliable block transfer | [Independent KCP example](docs/KCP_BUSINESS.md) |
| Verified TLS connections and threaded JSON send/receive | [TLS guide and runnable programs](docs/TLS.md) |
| Pack/unpack files and directories, encrypted ZIP and optional encrypted 7z | [Archive guide and runnable program](docs/ARCHIVE.md) |
| IPC, duplex messages, concurrent callers and other languages | [IPC tutorial and protocol](docs/IPC.md) |
| Brace formatting, automatic C arguments and expressions | [Formatting](docs/FORMAT.md) |
| Complete text, configuration, container and pool programs | [Task tutorials](docs/USAGE.md) |
| Nested file search and background scanning | [Recursive search tutorial](docs/FILE_SEARCH.md) |
| Full feature list, limits and build switches | [Detailed reference](docs/REFERENCE.md) |
| Encoding, translation, directory visits and lexing | [Common utilities](docs/COMMON_UTILITIES.md) |
| JSON/YAML/XML/INI/TOML/ENV/properties | [Configuration formats](docs/CONFIGURATION.md) |
| Structures, deeply nested mixed values and all-format output | [Serialization tutorial](docs/SERIALIZATION.md) |
| Different value types and custom structures | [Variant](docs/VARIANT.md) |
| Base64 / Base64URL and hexadecimal bytes | [Encoding helpers](docs/ENCODING.md) |
| Compact integers, UUID parsing and UUID v7 | [Integer tools](docs/INTEGER_TOOLS.md) |
| Bounded UTF-8 line-by-line input | [Line reader](docs/LINE_READER.md) |
| Borrowed splitting, strict integers and natural string order | [Text utilities](docs/TEXT_UTILITIES.md) |
| Stopwatch, bounded backoff and token-bucket limits | [Timing and flow control](docs/CONTROL.md) |
| Shared cancellation, WaitGroup and reader/writer locking | [Thread coordination](docs/COORDINATION.md) |
| Sort/search ordinary arrays, structures and C++ iterator ranges | [Generic algorithms](docs/ALGORITHMS.md) |
| Other containers and element copy/release | [C containers](docs/CONTAINERS.md) |
| Cancellation, close retries and C++ results | [Thread pool](docs/CPP_POOL.md) · [Result](docs/CPP_RESULTS.md) |
| UTF-8 filenames with FILE, native wchar_t conversion | [Text interoperation](docs/TEXT_INTEROP.md) |
| Bounded byte buffering with bulk read, write and peek | [Byte queues](docs/BYTE_QUEUE.md) |
| Caller-owned ring storage and two-span borrowing | [Fixed byte rings](docs/RING.md) |
| Single/batch thread messages and SPSC/MPSC/MPMC queues | [Concurrent queues](docs/QUEUES.md) |
| Bounded LRU caching with per-entry expiry | [LRU caches](docs/CACHE.md) |
| Dynamic bitmaps, bit search and set operations | [Bitsets](docs/BITSET.md) |
| Binary protocols and bounded background file output | [Streams and file writer](docs/STREAMS.md) |
| INI editing, configuration backups and settings overlays | [Configuration editing](docs/CONFIG_EDIT.md) |
| Interval trees, owned snapshots and bounded P50/P95/P99 | [Profiler](docs/PROFILER.md) |
| Growable named values and binary parameter data | [Parameter packs](docs/PARAMETERS.md) |
| Observe signals, modify shared arguments and return growable procedure results | [Signals](docs/SIGNALS.md) |
| Runtime token checks, quoted strings and nested group skipping | [Parsing helpers](docs/PARSING.md) |
| Runtime constants, conditions and bounded configuration includes | [Preprocessing](docs/PREPROCESSING.md) |
| Windows DLL versions, window titles and monitor/GPU identification | [Windows utilities](docs/WINDOWS_UTILITIES.md) |
| Borrowing native file, socket, thread and process objects | [Native interoperability](docs/NATIVE_INTEROP.md) |
| Scoped sleep requests through existing Linux desktop services | [Linux desktop](docs/LINUX_DESKTOP.md) |
| Direct SQLite/OpenSSL/libcurl/archive SDK calls | [Native dependencies](docs/NATIVE_DEPENDENCIES.md) |
| Persistent messages, business acknowledgements and leases | [Persistent queue](docs/PERSISTENT_QUEUE.md) |
| Unicode normalization and case folding | [Unicode](docs/UNICODE.md) |
| Chunked SHA-256/SHA-512 and HMAC | [Incremental digest](docs/INCREMENTAL_HASH.md) |
| UTF-8 regex, captures, replacement and advanced syntax | [Regular expressions](docs/REGEX.md) |
| Complete messages over streams, sockets and TLS | [Message frames](docs/FRAMES.md) |
| Initialize shared state once | [One-time initialization](docs/ONCE.md) |
| Shuffle/sample, radix sorting, byte search and Bloom | [Binary/search tools](docs/FAST_TOOLS.md) |
| HTTP/HTTPS, compression, URL, CLI, temporary resources, serial and shared memory | [Everyday I/O tools](docs/UTILITY_EXTENSIONS.md) |
| Optional dependency sources and native builds | [Dependencies](docs/DEPENDENCIES.md) |
| Network, processes, pipes, watches, mappings and system info | [Platform utilities](docs/PLATFORM_UTILITIES.md) |
| Android and embedded builds | [Android](docs/ANDROID.md) · [Cross-compilation](docs/REFERENCE.md#section-28) |

<a id="section-7"></a>

## 6. Builds, platforms and licensing

Public examples are independent; enable `HX_OS_BUILD_EXAMPLES=ON` to build them. See [build/install options](docs/REFERENCE.md#section-27) and [platform support](docs/PLATFORMS.md). Maintainer regression sources, execution logs and temporary reports remain local. Remote CI is disabled.

The first-party distribution license is not yet selected. Third-party components retain their own licenses. Read [third-party notices](THIRD_PARTY_NOTICES.md) before use or redistribution.

[c-badge]: https://img.shields.io/badge/C-99-blue?logo=c
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-11-blue?logo=cplusplus
[header-badge]: https://img.shields.io/badge/header--only-C%2B%2B%20facade-yellowgreen
[cmake-badge]: https://img.shields.io/badge/CMake-3.16%2B-blue?logo=cmake
[license-badge]: https://img.shields.io/badge/license-pending-lightgrey
[linux-badge]: https://img.shields.io/badge/Linux-GCC%20%7C%20Clang-yellow?logo=linux
[macos-badge]: https://img.shields.io/badge/macOS-pending-lightgrey?logo=apple
[windows-badge]: https://img.shields.io/badge/Windows-MSVC-blue
[embedded-badge]: https://img.shields.io/badge/ARM%20Linux-ARMv7%20%7C%20AArch64-blue
[android-badge]: https://img.shields.io/badge/Android-API%2021%20ARM-blue?logo=android
