# hx_c_platform

[![CI configuration][ci-badge]](.github/workflows/build.yml)
[![C99 core][c-badge]](CMakeLists.txt)
[![C++11 facade][cpp-badge]](docs/CPP_POOL.md)
[![Header-only C++ facade][header-badge]](docs/CPP_POOL.md)
[![CMake 3.16+][cmake-badge]](CMakeLists.txt)
[![First-party license pending][license-badge]](THIRD_PARTY_NOTICES.md)

[![Linux GCC and Clang][linux-badge]](docs/VALIDATION.md)
[![macOS validation pending][macos-badge]](docs/VALIDATION.md)
[![Windows MSVC][windows-badge]](docs/VALIDATION.md)
[![28 of 28 local test suites passed][tests-badge]](docs/VALIDATION.md)
[![Embedded Linux target pending][embedded-badge]](docs/VALIDATION.md)

Query system information directly through C or `hx::SystemInfo`;
`hx::CpuUsage` owns an independent process CPU sampler. See
[platform utilities](docs/PLATFORM_UTILITIES.md) and the
[24-batch implementation status](docs/OBS_COMPLETION.md).

[简体中文](README.zh-CN.md)

Badge evidence is recorded through **2026-09-29**. CI is configured; no remote
CI pass is claimed for this project. The **C++ facade only** is header-only;
the C core is a compiled static/shared library. C++11 has been checked;
a C++14/17/20 matrix has not been verified. The test count is **10 CTest
suites in the full local configuration**, not a count of individual assertions.
Linux evidence is from WSL: GCC library tests and a Clang C++11 facade test.
Windows evidence covers local MSVC x86/x64 builds. macOS and embedded SDK/board
runs remain pending, as does the first-party distribution license. See the
[validation record](docs/VALIDATION.md) for configuration and target limits.

An independent C99 foundation library with a C ABI. Applications and business
libraries use the same interfaces; native implementation choices stay below
them. The provisional OS prefix is `hx_os_`; the owning UTF-8 string is
`hx_string`. Version 0.1.0 is an initial implementation, not a stable ABI
release.

There is no dependency on hx_c_log, Qt, C++, or the OBS runtime. OBS informs the
interface/shared-implementation/platform-selection pattern and warning profile.
The project builds, tests and installs independently. No hx_c_log integration
has been performed.

## Included modules

| Header | Responsibility |
|---|---|
| hx_os_base.h | Status/native errors, immutable allocator contexts, clocks |
| hx_os_atomic.h | Aligned integer/pointer atomics and CPU wait hints |
| hx_os_thread.h | Threads, mutexes, conditions, retained events, thread TLS |
| hx_os_file.h | Regular files, UTF-8 paths, directory operations |
| hx_os_text.h | Strict/explicit-replacement encoding conversion |
| hx_string.h | Owning UTF-8 strings and borrowed views |
| hx_os_memory.h | Variable-size pool, object pool, arena |
| hx_os_pool.h | Bounded workers/tasks, cancellation and retryable close |
| hx_os_net.h | IPv4/IPv6, TCP/UDP, blocking DNS and deadline waits |
| hx_os_loop.h | Completion callbacks over IOCP, epoll or kqueue |
| hx_config.h | Immutable configuration documents and optional parser backends |
| hx_container.h | Bounded C sequences, ordered/hash maps and sets |
| hx/ThreadPool.hpp | Optional C++11 tasks, return values, exceptions and stop tokens |

Core Unicode conversion needs no third-party library. Legacy encodings are an
opt-in module using GNU libiconv or a detected system iconv. No third-party
source is modified or downloaded by CMake.

Configuration parsing covers JSON, YAML, XML, INI, TOML, ENV and properties.
Builtin syntax profiles are explicit; optional cJSON/libyaml/Mini-XML/inih/
tomlc17 backends are independently selected. See the
[format and lifetime contracts](docs/CONFIGURATION.md) and the common
[C example](examples/configuration.c). Default input encoding is UTF-8.

C containers include vector, deque, queue, stack, list, a max-heap and
ordered/hash map/set. Hash storage uses a private, unchanged uthash 2.4.0
header; disabling CONTAINERS excludes it. Copy/transfer/drop rules, byte
budgets, alignment and the hx_string deep-copy preset are documented in
[CONTAINERS.md](docs/CONTAINERS.md).

The [C++11 pool facade](docs/CPP_POOL.md) accepts lambdas, move-only arguments
and results, exceptions and cooperative cancellation. It reuses the C pool's
bounded queue and retryable close. Task handles own references; the host owns
the pool and explicitly closes/destroys it. Coroutines remain deferred.

Formatting/filtering, logger binding, rotation retention, asynchronous logging
queues, crash policy, LocalDumps configuration and system-log destinations
belong to consumers. Network encryption TLS is not thread-local storage TLS;
no TLS encryption library is bundled. Dynamic loading, page allocation,
additional system information and native stack capture remain future work.

## Build

Requires CMake 3.16+, a C99-capable MSVC/GCC/Clang toolchain and platform SDK.
Tests optionally compile public headers as C++11 if a C++ compiler is present.

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cd build
ctest -C Release --output-on-failure
~~~

On Windows select `-G "Visual Studio 17 2022" -A x64` when using a
current CMake. CMake 3.16 works with NMake in a properly configured MSVC
developer environment. Tests use no display server or external network.

~~~sh
python tools/fetch_iconv.py
cmake -S . -B build-text -DHX_OS_ENABLE_ICONV=ON \
  -DHX_OS_ICONV_SOURCE_DIR=/absolute/path/to/.deps/libiconv-1.18
cmake --build build-text --config Release --parallel 4
~~~

The helper needs Python 3.12+ and fetches pinned GNU libiconv 1.18, verifying
the archive SHA256. Existing source trees are retained. CMake only generates
configuration files inside the build directory. Omitting the source path with
ICONV enabled selects the system implementation; exact encoding availability
then depends on that implementation.

Supported encoding identifiers include UTF-8, UTF-16LE/BE, UTF-32LE/BE, ASCII,
ISO-8859-1, GB2312, GBK, GB18030:2005 and :2022, Big5, Shift-JIS, EUC-JP,
EUC-KR and Windows 1252/932/936/949/950. Query
`hx_os_encoding_supported()` before relying on a backend-specific encoding.
Conversion never changes the process locale or assumes an ANSI codepage.

| Option | Default | Effect |
|---|---|---|
| BUILD_SHARED_LIBS | OFF | Shared instead of static library |
| HX_OS_ENABLE_NETWORK | ON | Native sockets and network lifecycle |
| HX_OS_ENABLE_LOOP | ON | Async I/O, timers, posting, optional DNS integration |
| HX_OS_ENABLE_THREAD_POOL | ON | Bounded task pool; needed for async DNS |
| HX_OS_ENABLE_MEMORY_POOLS | ON | Pools and arena |
| HX_OS_ENABLE_ICONV | OFF | Legacy encoding backend |
| HX_OS_ENABLE_CONFIG | ON | Configuration document parsers |
| HX_OS_ENABLE_CONTAINERS | ON | Generic C containers, including private uthash |
| HX_OS_ENABLE_SYSINFO | ON | System information and process CPU sampling |
| HX_OS_ENABLE_LIBRARY | ON | Dynamic loading with owned diagnostic text |
| HX_OS_ENABLE_FILE_UTILS | ON | File/path conveniences, atomic publication and walks |
| HX_OS_ENABLE_ENV | ON | UTF-8 environment access |
| HX_OS_ENABLE_PATHS | ON | UTF-8 user, shared, temporary and module paths |
| HX_OS_ENABLE_THREAD_EX | ON | Stack options, names and bounded semaphores |
| HX_OS_ENABLE_PIPE | ON | Blocking pipes; async endpoints when LOOP is on |
| HX_OS_ENABLE_NET_OPTIONS | ON | Common/native socket options; needs NETWORK |
| HX_OS_ENABLE_POSITIONED_IO | ON | File reads/writes without changing position |
| HX_OS_ENABLE_FILE_INFO | ON | Extended timestamps, attributes and entry kinds |
| HX_OS_ENABLE_BOM | ON | Pure Unicode BOM detection and generation |
| HX_OS_ENABLE_CJSON/LIBYAML/MINIXML/INIH/TOMLC | OFF | Native parser adapters |
| HX_OS_BUILD_TESTS | ON | Contract tests and isolated public headers |
| HX_OS_BUILD_EXAMPLES | ON | C examples; C++ task example when CXX is enabled |
| HX_OS_BUILD_BENCHMARKS | OFF | Development microbenchmarks |
| HX_OS_SANITIZER | empty | GCC/Clang address,undefined or thread |

Disabling a module excludes its implementation sources while retaining ABI
entry points. Resource creation returns NOT_SUPPORTED and capability bits
are cleared. Async DNS is unavailable without the thread pool; other loop
operations remain usable. With NETWORK=OFF, create a local loop for posts,
timers and pipes. No network initialization is required.

## Strings and encoding

~~~c
#include <hx_os.h>

hx_string text = HX_STRING_INIT;
hx_os_error error;
hx_os_status status;
hx_os_text_result result;
unsigned char utf16[128];

status = hx_string_assign_cstr(&text, "Hello, 中文", &error);
if (status == HX_OS_OK)
	status = hx_string_append_cstr(&text, "!", &error);
if (status == HX_OS_OK)
	status = hx_string_encode(&text, HX_OS_UTF16LE, utf16, sizeof(utf16),
		&result, &error);
/* Inspect status before using the converted bytes. */
hx_string_destroy(&text);
~~~

Use `hx_string_init(&text, context)` for a custom context. Assignment,
append, insertion, replacement, erasure, substring, search, comparison,
copy/move and printf formatting are provided. Mutations preserve valid UTF-8;
strict decoding failure preserves the original value. Encoding output may
contain a valid prefix on error: always inspect the status and byte counts.

Lengths and offsets are bytes. Character counts count Unicode codepoints,
not grapheme clusters. Views borrow storage and expire on mutation; structure
assignment is not an owning copy. Embedded NUL is allowed, but a C-string
consumer stops there. No Unicode normalization or locale collation is implied.

UTF-8 source compilation uses MSVC /utf-8. The library does not change console
codepages, standard streams or locale. Consumers that print UTF-8 must use a
suitable terminal or their own Unicode console adapter. The same UTF-8 path
API converts to native UTF-16 on Windows.

## Install and consume

~~~sh
cmake --install build --config Release --prefix /absolute/install/prefix
~~~

~~~cmake
find_package(hx_c_platform 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE hx_c_platform::hx_c_platform)
~~~

For the C++11 task facade, include `hx/ThreadPool.hpp` and instead link
`hx_c_platform::cpp`. This interface target adds C++11 requirements only for
consumers that select it. Building the C library with tests/examples disabled
does not enable a C++ compiler.

Supply CMAKE_PREFIX_PATH to the consuming configure. The imported target
carries the shared-library definition and required static link dependencies.
Headers, versioned library, CMake exports and pkg-config metadata are installed.
The pkg-config prefix is relocatable with relative installation directories;
explicit absolute directories retain their configured paths. Put a Windows DLL beside
the application or on its runtime search path.

~~~sh
python tools/verify_install.py build --config Release
~~~

This installs into build/installed, builds five separate consumers covering
C99, C++11, configuration, containers and C++ tasks, and runs them. The script
is a native-host check, not a cross-target emulator.

## Platforms and architecture

~~~mermaid
flowchart TD
    A[Applications and business libraries] --> B[Public C ABI]
    B --> C[Shared strings, Unicode, pools and lifetime rules]
    C --> D[OS adapters selected by CMake]
    D --> E[Win32 and IOCP]
    D --> F[POSIX shared code]
    F --> G[Linux epoll]
    F --> H[macOS kqueue and Mach]
~~~

The dependency direction is one-way. There are no configurable process-wide
allocators, loggers or queues. OS, compiler, CPU and optional dependency
decisions are separate. Calls use build-selected implementations directly.

Windows 7 SP1/8.1 x86/x64 remains the intended API baseline, including CRT/SDK
compatibility requirements. Current Windows x86/x64 and WSL Linux have local
test evidence. macOS code exists but has not been compiled/run on an Apple host.
Embedded Linux ARMv7-A/AArch64 with glibc, musl or uClibc-ng is a target;
board/SDK validation and exact minimum versions are still pending.

~~~sh
cmake -S . -B build-board \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/embedded-linux.cmake \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=/sdk/bin/aarch64-linux-gnu- \
  -DHX_OS_SYSROOT=/sdk/sysroot \
  -DHX_OS_BUILD_TESTS=OFF -DHX_OS_BUILD_EXAMPLES=OFF
~~~

Use the actual SDK; do not infer libc or float ABI from this example.
Compiler checks require linkable 64-bit atomics and monotonic conditions.
libatomic is linked when necessary. A missing finite native thread join is
reported as a missing capability, never emulated by freeing a running thread.

Android, iOS and BSD need explicit adaptation and target verification; unknown
CMake systems fail configuration. POSIX files/pthreads/socket abstractions
are reusable, but API floors, sandboxes, DNS, FLS/TLS cleanup, timers and
backend choices still need review. FreeRTOS and bare metal are out of scope.

## Engineering and evidence

First-party source uses UTF-8 without BOM, LF, Tab width 8, an 80-column
limit and lowercase underscore C function names. The C++ facade follows the
C++ class/method naming rules. Warning policy is target-local and defaults to
errors. Vendor targets are not reformatted or given the
first-party warning policy. C++ is not required to build the library itself.

- [Behavior and lifetime contracts](docs/CONTRACTS.md)
- [C containers and ownership](docs/CONTAINERS.md)
- [C++11 task pool and shutdown](docs/CPP_POOL.md)
- [Verification matrix and remaining target checks](docs/VALIDATION.md)
- [Future hx_c_log migration](docs/MIGRATION.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

The first-party project's distribution license has not yet been selected.
Retained third-party license terms are recorded separately. Assembly/intrinsics
are confined to CPU primitives; performance improvements require measurements.
No claim is made that hx_c_log's locked performance thresholds have been met
by this new library or a migration.


[ci-badge]: https://img.shields.io/badge/CI-configured-lightgrey?logo=githubactions
[c-badge]: https://img.shields.io/badge/C-99-blue?logo=c
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-11-blue?logo=cplusplus
[header-badge]: https://img.shields.io/badge/header--only-C%2B%2B%20facade-yellowgreen
[cmake-badge]: https://img.shields.io/badge/CMake-3.16%2B-blue?logo=cmake
[license-badge]: https://img.shields.io/badge/license-pending-lightgrey
[linux-badge]: https://img.shields.io/badge/Linux-GCC%20%7C%20Clang-yellow?logo=linux
[macos-badge]: https://img.shields.io/badge/macOS-pending-lightgrey?logo=apple
[windows-badge]: https://img.shields.io/badge/Windows-MSVC-blue
[tests-badge]: https://img.shields.io/badge/tests-28%2F28%20suites%20local-brightgreen
[embedded-badge]: https://img.shields.io/badge/embedded%20Linux-target%20pending-lightgrey
