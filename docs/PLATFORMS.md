# Platform Support and Deployment Guide

[简体中文](PLATFORMS.zh-CN.md) · [Back to README](../README.md)

## 1. Target Platforms and Toolchain Baselines

The `hx_c_platform` core is written in standard C99 and exports a stable C ABI. The optional C++ convenience layer (`include/hx/`) is a header-only C++11 facade built without C++ exceptions, and C++20 stackless coroutines are opt-in. Building requires **CMake 3.16+**, and any prebuilt dependency SDK must match your application's architecture, C runtime (CRT), and Debug/Release configuration.

| Target Platform | Compatibility Baseline | Deployment & Integration Requirements |
| --- | --- | --- |
| **Windows** | Windows 7 SP1 and newer; MSVC x86 / x64 | Deploy matching DLLs from `bin/`; when targeting legacy Windows releases, ensure selected third-party SDKs are also built for that OS baseline |
| **Linux Desktop / Server** | GCC or Clang; native POSIX / Linux APIs | Match target libc and toolchain; desktop power services (D-Bus) are optional at runtime |
| **Embedded Linux** | ARMv7-A and AArch64 | Match board sysroot, floating-point ABI (e.g., hard-float), and device permissions; start from `cmake/minimal.cmake` for small footprints |
| **Android** | API 21+; `arm64-v8a` and `armeabi-v7a` | Match NDK ABI/API level and app sandbox permissions; see the [Android Build Guide](ANDROID.md) |
| **macOS** | Intel 10.13, Apple Silicon (ARM64) 11.0 | Apple adapters (kqueue / Mach) are implemented; native Apple hardware build and runtime verification are pending |

> [!NOTE]
> **Target Hardware Acceptance**:
> The table above defines the OS API baselines targeted by the codebase. When deploying to Windows 7/8.1, Android 5.0, physical ARM boards, or custom libc/toolchain combinations, always run acceptance checks on the actual target environment—user-mode QEMU, emulators, and native-bridge translation do not substitute for physical hardware verification. BSD, iOS, FreeRTOS, and bare-metal targets are outside the current scope.

## 2. Querying Capabilities and Handling Platform Limits

Use `hx_os_capabilities()` at runtime to check which optional modules are compiled into the library, and always inspect the returned `hx_os_status`. Even when a module is enabled at build time, OS-specific operations that are unavailable on the current host return `HX_OS_NOT_SUPPORTED`, while permission and I/O failures preserve native OS diagnostic codes in `hx_os_error`.

Common cross-platform boundaries include:

- **Linux Idle-Sleep Inhibition**: Requires an active desktop D-Bus service (`systemd-logind` or `elogind`). Headless server and embedded Linux builds can disable this module at configure time; see [Linux Desktop Power Management](LINUX_DESKTOP.md).
- **Android Sandbox and API Level Limits**: Application directories must be supplied by the host Android app. Child process creation requires building against Android API 28 or newer, and local Unix domain sockets and network interface notifications remain subject to Android SELinux policies and UID permissions.
- **Stackful Coroutines**: Require a CPU/context-switch backend for the target architecture. On Linux systems with active hardware shadow stacks (CET), unsupported operations fail explicitly with `HX_OS_NOT_SUPPORTED`; see [Coroutines](COROUTINES.md).
- **Clocks, Deadlines, and Synchronous File I/O**: Clock resolution, timer wakeup latency, and disk flushing (`fsync` / `FlushFileBuffers`) follow the underlying OS kernel and storage hardware. Passing a deadline bounds waiting operations, but cannot forcefully interrupt an arbitrary blocking kernel file I/O syscall.
- **UDP Fragmentation and Virtualized Networks**: Standard UDP datagrams can be lost, reordered, or duplicated, and fragmented IPv4 UDP packets may be dropped under virtualized networking such as WSL mirrored mode. Keep UDP payloads within path MTU or use TCP/KCP when reliability is required; see [IPC and Transport Choices](IPC.md).

## 3. Building, Installing, and Deploying

By default, CMake builds only the core library. Pass `-DHX_OS_BUILD_EXAMPLES=ON` to compile the standalone public examples under `examples/`. Maintain a separate build directory for each toolchain, architecture, and Debug/Release configuration.

- **Preparing Third-Party Native SDKs**: See the [Dependency Build Guide](DEPENDENCIES.md).
- **CMake Switches and Installation**: See [Build Options and Installation](REFERENCE.md#section-27).
- **Calling Bundled Native SDKs Directly**: See [Native Dependencies](NATIVE_DEPENDENCIES.md).

> [!IMPORTANT]
> **Static vs. Shared Runtime Dependencies**:
> Linking `hx_c_platform` statically does not automatically make all external SDK dependencies static—by default, OpenSSL, libcurl, and utf8proc build as shared libraries (DLLs / `.so`). Always bundle the required runtime libraries from your SDK prefix and test a small example on the target machine to verify permissions, file paths, and shutdown behavior.

## 4. Platform Boundaries for Advanced Networking

UDP packet metadata, scatter/gather vector I/O, bounded TCP dialing, c-ares cancellable DNS, TLS security profiles, HTTP file streaming, and coroutine I/O bridges are supported on **Windows, Linux, and Android** (with Linux ARMv7/AArch64 sharing the POSIX/Linux implementation):

- **Network Interface Notifications**: Uses native OS change notifications, which may be blocked by Android sandbox/SELinux policies. Treat notifications as a signal to re-query the interface snapshot rather than a lossless event log.
- **OS-Specific Socket Options**: TCP user timeout is specific to Linux and Android; UDP ICMP connection-reset suppression (`SIO_UDP_CONNRESET`) is specific to Windows; named IPv6 zone resolution requires `HX_OS_ENABLE_NET_OPTIONS`.
- **IDNA and Cancellable DNS**: Internationalized domain name (IDNA) conversion depends on the linked `libcurl` backend and returns `HX_OS_NOT_SUPPORTED` when unavailable. The optional `c-ares` resolver can be disabled at build time and performs direct DNS queries rather than replicating every host OS or enterprise VPN name-resolution policy. None of the networking adapters modify firewall rules, WSL networking modes, or global OS network settings.
