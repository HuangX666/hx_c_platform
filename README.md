# hx_c_platform

[![C99 core][c-badge]](CMakeLists.txt) [![C++11 facade][cpp-badge]](docs/CPP_POOL.md) [![Header-only C++ facade][header-badge]](docs/CPP_POOL.md) [![CMake 3.16+][cmake-badge]](CMakeLists.txt) [![First-party license pending][license-badge]](THIRD_PARTY_NOTICES.md) [![Linux GCC and Clang][linux-badge]](docs/PLATFORMS.md) [![macOS validation pending][macos-badge]](docs/PLATFORMS.md) [![Windows MSVC][windows-badge]](docs/PLATFORMS.md) [![Embedded Linux target pending][embedded-badge]](docs/PLATFORMS.md) [![Android API 21 ARM][android-badge]](docs/ANDROID.md)

[简体中文](README.zh-CN.md) · [Getting-Started Guide](docs/USAGE.md) · [Runnable Examples](examples/README.md) · [Find a Feature](#section-6) · [API Reference](docs/REFERENCE.md)

<a id="section-1"></a>

## 1. What This Library Does

Cross-platform C and C++ applications repeatedly need the same building blocks: reading and writing files, parsing configuration files, manipulating UTF-8 strings, sorting arrays and containers, running work on background thread pools, and communicating over sockets or local IPC. `hx_c_platform` unifies these everyday system facilities behind one clean interface while handling Windows, Linux, Android, and macOS platform differences internally.

```mermaid
flowchart LR
    A["Your C / C++ Application"] --> B["Unified API: Files / Config / Text / Containers / Threads / Network"]
    B --> C["hx_c_platform Platform Adapters"]
    C --> D["Windows / Linux / Android / macOS"]
```

### Key Highlights

- **Compiled C99 Core + Header-Only C++11 Facade**: The core library is written in standard C99 with a stable C ABI. Optional headers under `include/hx/` add RAII resource management and explicit `hx::Result<T>` error handling (with zero `try`/`catch`/`throw` exceptions), plus opt-in C++20 coroutine adapters.
- **No Global Initialization or Heavy Frameworks**: Ordinary API calls work out of the box without global runtime setup or dependencies on Qt, Boost, or OBS. Custom allocators can be injected per subsystem via `hx_os_context`.
- **Modular CMake Feature Toggles**: Files, strings, built-in configuration parsers (JSON, INI, TOML, etc.), containers, and thread pools work out of the box. External SDK modules—such as cryptography (OpenSSL), archives and compression (libarchive/zlib/lzma/zstd), HTTP (libcurl), and persistent queues (SQLite)—can be enabled or stripped individually via CMake options.
- **Explicit Ownership and Timeout-Safe Lifetimes**: Every blocking or shutdown operation accepts an explicit monotonic deadline. A timed-out wait (`HX_OS_TIMEOUT`) never forcefully kills worker threads or frees memory that background tasks may still be accessing.

> [!TIP]
> - Want to build and run working examples right away? Browse the [Runnable Example Directory](examples/README.md), which lists each task, CMake target, input requirement, and expected output.
> - Investigating CPU hotspots or memory leaks? See the [perf, Valgrind, and WPR/WPA Analysis Workflow](docs/ANALYSIS.md).
> - See [Platform Support and Deployment](docs/PLATFORMS.md) for OS baselines, compiler requirements, and platform limits.

<a id="section-2"></a>
<a id="section-3"></a>

## 2. 5-Minute Setup: Prepare a Minimal Project

**Prerequisites**: CMake 3.16+ and a C/C++ compiler (MSVC Developer Command Prompt on Windows, or GCC/Clang on Linux).

If you already have a prebuilt `include/lib/bin` SDK package, jump directly to the [Installed SDK Integration Guide](docs/INSTALL.md). Below is the standard **source integration (`add_subdirectory`)** workflow:

### 2.1 Clone the Repository

Create a project directory `hx_demo`, place `hx_c_platform` inside `deps/`, and initialize its submodules:

```sh
mkdir hx_demo && cd hx_demo
mkdir deps && cd deps
git clone --branch dev https://github.com/HuangX666/hx_c_platform.git
git -C hx_c_platform submodule update --init --recursive
cd ..
```

### 2.2 Create `CMakeLists.txt`

Create `CMakeLists.txt` in `hx_demo`. To get started immediately without building external native SDKs (OpenSSL, libcurl, etc.) first, this starter configuration disables the external-SDK modules and keeps all built-in core modules enabled (when you need HTTP, crypto, or archives later, follow the [Dependency Build Guide](docs/DEPENDENCIES.md) and turn those options back on):

```cmake
cmake_minimum_required(VERSION 3.16)
project(hx_demo LANGUAGES C)

# Disable modules that require external prebuilt native SDKs
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

> [!NOTE]
> **Building the C++11 examples (`main.cpp`) in Section 3**:
> Change `project(hx_demo LANGUAGES C)` to `LANGUAGES C CXX`, replace `main.c` with `main.cpp`, and link against `hx_c_platform::cpp` instead of `hx_c_platform::hx_c_platform`. See the [complete C++ CMake setup](docs/USAGE.md#cpp-project) for compiler flags.

### 2.3 Build the Project

Save any complete program from Section 3 as `hx_demo/main.c` (or `main.cpp`) and build from `hx_demo`:

```sh
cmake -S . -B build
cmake --build build --config Release
```

<a id="section-4"></a>

## 3. Common Recipes: Run First, Then Substitute Your Data

Each subsection below is a standalone program. Save one program at a time as `main.c` (or `main.cpp`), rebuild, and run from the `hx_demo` directory.

### 3.1 Read a UTF-8 Text File in One Call (C99)

**Scenario**: Read the entire contents of `hello.txt` into an `hx_string` and print it to stdout. The library opens, reads, and closes the file automatically.

1. Create a UTF-8 file named `hello.txt` in `hx_demo`:

   ```text
   Hello, hx_c_platform!
   ```

2. Save the following program as `main.c`:

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

3. Run the executable from `hx_demo` (so relative path lookup finds `hello.txt`):

   ```powershell
   # Windows (MSVC Release)
   .\build\Release\hx_demo.exe
   ```

   ```sh
   # Linux / WSL
   ./build/hx_demo
   ```

   **Expected output**: `Hello, hx_c_platform!`

**How the calls work**:

| Expression | Purpose & Ownership |
| --- | --- |
| `hx_string text = HX_STRING_INIT` | Initializes an empty string struct on the stack (no heap allocation yet) |
| `hx_os_file_read_text("hello.txt", &text, NULL)` | Reads the file into `text`; passing `NULL` for the 3rd `hx_os_error *` argument means we only inspect the returned `hx_os_status` |
| `hx_string_c_str(&text)` | Returns a NUL-terminated `const char *` borrowed from `text` |
| `hx_string_destroy(&text)` | Frees the string buffer (the file handle was already closed before `hx_os_file_read_text` returned) |

See [File Operations and C++ Wrappers](docs/USAGE.md#files) for writing files, line-by-line reading, and C++ `hx::Files::readText`.

---

### 3.2 Trim and Normalize User Input (C99)

**Scenario**: Clean up raw user input `"  DEVICE_A  "` by stripping leading/trailing ASCII whitespace and converting ASCII uppercase letters to lowercase (`device_a`) in place.

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

**Expected output**: `device_a`

- `hx_string_assign_cstr()` copies the input C string into `name`.
- `hx_string_trim_ascii_in_place()` and `hx_string_lower_ascii_in_place()` mutate the same string buffer in place while leaving non-ASCII UTF-8 bytes untouched.
- For full Unicode normalization (NFC/NFKC) or locale-independent Unicode case folding, see [Unicode Tools](docs/UNICODE.md).

---

### 3.3 Read a Port from JSON Configuration (C++11)

**Scenario**: Load `config.json` and read the integer field `/port` using `hx::Result<T>` for explicit, exception-free error handling.

1. Create `config.json` in `hx_demo`:

   ```json
   {"port": 8080}
   ```

2. Using the [C++ CMake setup](docs/USAGE.md#cpp-project), save the following program as `main.cpp`:

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

**Expected output**: `Port: 8080`

- `hx::ConfigDocument::load()` returns `hx::Result<hx::ConfigDocument>`. Check `if (!config)` first; on success, access the document via `config.value()`. The document automatically releases its memory when it leaves scope.
- `integer("/port")` queries the `port` key on the root JSON object. Type checking is strict: a missing key returns `HX_OS_NOT_FOUND`, and a quoted string `"8080"` returns `HX_OS_TYPE_MISMATCH`.
- See the [Configuration Guide](docs/USAGE.md#configuration) for the C API, nested paths, and YAML/TOML/INI/XML support.

---

### 3.4 Sort a Native C Array in Place (C99)

**Scenario**: Sort an ordinary C array `int32_t values[] = {5, 1, 3}` in ascending order without copying elements into a separate container.

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

**Expected output**: `1 3 5`

- `hx_os_sort_i32(values, 3, NULL)` sorts the 3 elements directly inside `values`. Because `values` is a stack array, no cleanup call is needed.
- See [Generic Algorithms](docs/ALGORITHMS.md) for custom struct sorting, binary search, filtering, and C++ iterator ranges, or [Containers](docs/USAGE.md#containers) when you need dynamically resizing collections.

---

### 3.5 Run a Calculation on a Background Thread Pool (C++11)

**Scenario**: Create a thread pool with 2 worker threads, submit `21 * 2` for background execution, wait for the result with a 2-second deadline, and shut down the pool cleanly (from [examples/thread_pool.cpp](examples/thread_pool.cpp)).

```cpp
#include <hx/ThreadPool.hpp>
#include <cstdio>

int main()
{
	hx_os_thread_pool *owner = NULL;

	if (!(hx_os_capabilities() & HX_OS_CAP_THREAD_POOL)) {
		std::puts("Thread pools are disabled.");
		return 0;
	}

	// 1. Create a pool with 2 worker threads; native handle is stored in owner
	auto created = hx::ThreadPool::withThreads(owner, 2);
	if (!created) {
		std::fprintf(
			stderr, "%s\n", hx_os_status_name(created.status()));
		auto closed = hx::ThreadPool(owner).shutdownUntil(
			hx_os_deadline_after_ms(2000));
		if (!closed)
			std::fputs("Cleanup pending; keep owner to retry.\n",
				stderr);
		return 1;
	}

	auto &pool = created.value();

	// 2. Submit background work and wait up to 2 seconds for the result
	auto submitted =
		pool.submit([](int value) noexcept { return value * 2; }, 21);

	hx_os_status workStatus = submitted.status();
	if (submitted) {
		auto answer = submitted.value().getUntil(
			hx_os_deadline_after_ms(2000));
		workStatus = answer.status();
		if (answer)
			std::printf("Task result: %d\n", answer.value());
	}

	// 3. Explicitly shut down the pool (cancel queued work, wait up to 2s)
	auto closed = pool.shutdownUntil(
		hx_os_deadline_after_ms(2000), HX_OS_POOL_CANCEL_PENDING);
	if (!closed) {
		std::fprintf(stderr, "Close pending: %s\n",
			hx_os_status_name(closed.status()));
		return 1;
	}

	return workStatus == HX_OS_OK ? 0 : 1;
}
```

**Expected output**: `Task result: 42`

> [!IMPORTANT]
> **Why pass `owner` and call `shutdownUntil()` explicitly?**
> `hx_c_platform` never forcefully terminates threads or frees active resources on a timeout. If a worker is still running when the deadline expires, `shutdownUntil()` returns `HX_OS_TIMEOUT` and keeps `owner` valid so the worker never touches freed memory. Long-running services should retain `owner` (and any data borrowed by active tasks) and retry `shutdownUntil()` later. See the [Thread Pool Guide](docs/USAGE.md#thread-pool) for details.

---

### 3.6 Print Formatted Text and Numbers with `{}` (C99)

**Scenario**: Format strings and floating-point numbers using modern `{}` and `{:.2f}` placeholders in standard C99 with explicit typed argument wrappers.

```c
#include <hx_print.h>

int main(void)
{
	hx_os_status status = HX_PRINTLN("{}: {:.2f}",
		hx_fmt_str("Voltage"), hx_fmt_f64(3.75));

	return status == HX_OS_OK ? 0 : 1;
}
```

**Expected output**: `Voltage: 3.75`

- `{}` consumes the next argument in order, and `{:.2f}` formats a `double` to two decimal places.
- In C99, `hx_fmt_str()` and `hx_fmt_f64()` tag argument types inline—just include `<hx_print.h>` and link the library, with no code generation step required.
- In C++11, argument types are deduced automatically. See the [Formatting Guide](docs/FORMAT.md) for custom structs, automatic C argument macros, and format specifiers.

<a id="section-5"></a>

## 4. Four Core Rules to Keep in Mind

1. **Always check `hx_os_status` in C**: `HX_OS_OK` (`0`) indicates success; positive statuses (such as `HX_OS_EOF`) and negative error codes carry specific meanings. Use `hx_os_status_name(status)` to get a human-readable status string.
2. **Check `hx::Result<T>` before calling `.value()` in C++**: Test `if (!result)` first, and only call `result.value()` after confirming success. All C++ wrappers use explicit `Result` returns rather than exceptions.
3. **Pair C allocations with their matching release function**: Release strings with `hx_string_destroy()`, configuration documents with `hx_os_config_release()`, and other C handles with the cleanup function documented in their header.
4. **Complete shutdown on objects with workers or async I/O**: After a timeout (`HX_OS_TIMEOUT`), background work may still be running. Retain the owner handle and any borrowed task data until close/shutdown succeeds.

See [First-Use Questions and Troubleshooting](docs/USAGE.md#questions) for concrete examples.

<a id="section-6"></a>

## 5. Feature Map & Documentation Index

Use the tables below to jump directly to the guide and runnable example for your task:

### 5.1 Files, Streams & Text Processing

| What you want to do | Where to start |
| --- | --- |
| Step-by-step tutorials for files, strings, config, containers, and pools | [Getting-Started Tutorials](docs/USAGE.md) |
| Brace `{}` formatting, automatic C arguments, and custom struct output | [Formatting Guide](docs/FORMAT.md) |
| Bounded UTF-8 line-by-line file and stream reading | [Line Reader](docs/LINE_READER.md) |
| Zero-copy string splitting, strict integer parsing, and natural sort order | [Text Utilities](docs/TEXT_UTILITIES.md) |
| Open `FILE*` with UTF-8 paths and convert Windows `wchar_t` strings | [Text Interoperability](docs/TEXT_INTEROP.md) |
| Unicode normalization (NFC/NFD/NFKC/NFKD) and case folding | [Unicode Tools](docs/UNICODE.md) |
| UTF-8 regular expressions, capture groups, and text replacement | [Regular Expressions](docs/REGEX.md) |
| Base64 / Base64URL and hexadecimal encoding/decoding | [Encoding Helpers](docs/ENCODING.md) |
| Recursive directory file search and background scanning | [Recursive File Search](docs/FILE_SEARCH.md) |
| Binary endian-aware streams and bounded background file writing | [Streams and File Writer](docs/STREAMS.md) |
| Pack/unpack ZIP and TAR archives, encrypted ZIP, and optional 7z | [Archive Guide](docs/ARCHIVE.md) |
| Legacy encodings, translation lookup tables, directory visits, and lexing | [Common Utilities](docs/COMMON_UTILITIES.md) |

### 5.2 Configuration, Serialization & Dynamic Types

| What you want to do | Where to start |
| --- | --- |
| Parse JSON / YAML / XML / INI / TOML / ENV / Properties | [Configuration Formats](docs/CONFIGURATION.md) |
| Edit INI files, manage configuration backups, and apply settings overlays | [Configuration Editing](docs/CONFIG_EDIT.md) |
| Serialize C/C++ structs and nested trees across 7 configuration formats | [Serialization Tutorial](docs/SERIALIZATION.md) |
| Hold heterogeneous values or custom structs in a single variable | [Variant Values](docs/VARIANT.md) |
| Pass growable named parameters and binary payloads | [Parameter Packs](docs/PARAMETERS.md) |
| Runtime token checks, quoted strings, and nested block skipping | [Parsing Helpers](docs/PARSING.md) |
| Expand runtime constants, conditionals, and bounded file includes | [Configuration Preprocessing](docs/PREPROCESSING.md) |

### 5.3 Containers, Algorithms & Data Structures

| What you want to do | Where to start |
| --- | --- |
| Generic C vectors, doubly linked lists, queues, and red-black tree maps | [C Containers](docs/CONTAINERS.md) |
| Sort and search C arrays, custom structs, and C++ iterator ranges | [Generic Algorithms](docs/ALGORITHMS.md) |
| Bounded byte queues with bulk read, write, and peek | [Byte Queues](docs/BYTE_QUEUE.md) |
| Caller-owned fixed byte rings with two-span zero-copy borrowing | [Fixed Byte Rings](docs/RING.md) |
| Bounded LRU cache with per-entry expiration (TTL) | [LRU Cache](docs/CACHE.md) |
| Dynamic bitsets, bit scanning, and set operations | [Bitsets](docs/BITSET.md) |
| Compact Varint/ZigZag integers, UUID parsing, and UUID v7 generation | [Integer and UUID Tools](docs/INTEGER_TOOLS.md) |
| Random shuffle/sampling, radix sort, multi-pattern search, and Bloom filters | [Fast Binary & Search Tools](docs/FAST_TOOLS.md) |

### 5.4 Concurrency, Thread Pools, Queues & Coroutines

| What you want to do | Where to start |
| --- | --- |
| Submit C++ thread-pool tasks, cancel work, retry close, and use `Result<T>` | [Thread Pool](docs/CPP_POOL.md) · [Result Guide](docs/CPP_RESULTS.md) |
| Single and batched inter-thread messaging (SPSC / MPSC / MPMC queues) | [Concurrent Queues](docs/QUEUES.md) |
| SQLite-backed persistent message queues with consumer ACKs and leases | [Persistent Queue](docs/PERSISTENT_QUEUE.md) |
| Cooperative cancellation tokens, `WaitGroup`, and reader/writer locks | [Thread Coordination](docs/COORDINATION.md) |
| Thread-safe one-time initialization (`hx_os_once`) | [One-Time Initialization](docs/ONCE.md) |
| Stopwatches, bounded exponential backoff, and token-bucket rate limiting | [Timing and Flow Control](docs/CONTROL.md) |
| Observe signals, mutate shared call arguments, and collect procedure results | [Signals](docs/SIGNALS.md) |

### 5.5 Networking, IPC, Security & Cryptography

| What you want to do | Where to start |
| --- | --- |
| End-to-end TCP business example (order submission and receipts) | [Independent TCP Example](docs/TCP_BUSINESS.md) |
| End-to-end UDP business example (sensor samples and feedback) | [Independent UDP Example](docs/UDP_BUSINESS.md) |
| Reliable UDP block transfer over KCP | [Independent KCP Example](docs/KCP_BUSINESS.md) |
| Local IPC device control and cross-language wire protocol | [Independent IPC Example](docs/IPC_BUSINESS.md) · [IPC Guide & Protocol](docs/IPC.md) |
| Socket keepalive, IPv6 dual stack, broadcast, multicast, and NIC queries | [Socket Options and Discovery](docs/NETWORK_OPTIONS.md) |
| UDP packet metadata, batch I/O, scatter/gather I/O, and trusted-LAN KCP | [Practical Network I/O](docs/NETWORK_IO.md) |
| Length-prefixed message frames over streams, sockets, and TLS | [Message Frames](docs/FRAMES.md) |
| Certificate-verified TLS connections and multi-threaded JSON messaging | [TLS Guide and Examples](docs/TLS.md) |
| Streaming HTTP/HTTPS file transfers and coroutine HTTP requests | [HTTP Transfers](docs/HTTP_FILES.md) |
| One-call text/file encryption, SHA-256, HMAC, and digital signatures | [Cryptography Guide](docs/CRYPTO.md) |
| Incremental chunked SHA-256 / SHA-512 and HMAC computation | [Incremental Digest](docs/INCREMENTAL_HASH.md) |

### 5.6 System Integration, Profiling & Platform Utilities

| What you want to do | Where to start |
| --- | --- |
| HTTP, memory compression, URLs, CLI parsing, temp files, serial ports, and shm | [Everyday I/O Tools](docs/UTILITY_EXTENSIONS.md) |
| Child processes, pipes, directory watches, memory mappings, and system info | [Platform Utilities](docs/PLATFORM_UTILITIES.md) |
| Borrow native handles (`HANDLE`, `fd`, `SOCKET`, thread, and process handles) | [Native Interoperability](docs/NATIVE_INTEROP.md) |
| Read Windows DLL version resources, window titles, and monitor/GPU info | [Windows Utilities](docs/WINDOWS_UTILITIES.md) |
| Configure Windows Error Reporting (WER) LocalDumps for a customer EXE | [WER LocalDumps](docs/LOCAL_DUMPS.md) |
| Request scoped idle-sleep inhibition through Linux desktop services | [Linux Desktop Power](docs/LINUX_DESKTOP.md) |
| Hierarchical interval profiling trees, snapshots, and P50/P95/P99 percentiles | [Profiler Guide](docs/PROFILER.md) |
| Call the bundled SQLite, OpenSSL, libcurl, or compression SDKs directly | [Native Dependencies](docs/NATIVE_DEPENDENCIES.md) |
| Manage third-party submodules and build optional native dependency SDKs | [Dependency Build Guide](docs/DEPENDENCIES.md) |
| Cross-compile for Android NDK and embedded Linux targets | [Android Guide](docs/ANDROID.md) · [Cross-Compilation](docs/REFERENCE.md#section-28) |
| Look up all CMake switches, limits, and complete module contracts | [Detailed Reference](docs/REFERENCE.md) |

<a id="section-7"></a>

## 6. Build Options, Platforms & Licensing

- **Building Public Examples**: Pass `-DHX_OS_BUILD_EXAMPLES=ON` to CMake to build the standalone programs in `examples/`. See [Build and Install Options](docs/REFERENCE.md#section-27) for all CMake switches and [Platform Support](docs/PLATFORMS.md) for OS/architecture baselines.
- **Licensing**: The distribution license for first-party code in `hx_c_platform` is not yet finalized. Third-party components under `deps/` retain their respective upstream licenses; review [Third-Party Notices](THIRD_PARTY_NOTICES.md) before use or redistribution.

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
