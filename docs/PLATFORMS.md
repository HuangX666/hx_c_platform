# Platform support and deployment

[简体中文](PLATFORMS.zh-CN.md) · [README](../README.md)

## 1. Select a target

The core is C99 with a C ABI; the optional facade uses C++11 without exceptions. C++20 stackless syntax is opt-in. Use CMake 3.16 or newer and an SDK built for the same architecture, runtime and configuration as the consumer.

| Target | Compatibility baseline | Deployment requirements |
| --- | --- | --- |
| Windows | Windows 7 SP1; x86/x64 MSVC | Deploy matching DLLs from `bin/`; older-Windows compatibility must also hold for selected third-party SDKs |
| Linux desktop/server | GCC or Clang, POSIX/Linux adapters | Match libc and toolchain; desktop services are optional |
| Embedded Linux | ARMv7 and AArch64 adapters | Match sysroot, floating-point ABI and device permissions; start with `cmake/minimal.cmake` |
| Android | API 21; arm64-v8a and armeabi-v7a | Match NDK ABI/API and application permissions; see [Android](ANDROID.md) |
| macOS | Intel 10.13; ARM64 11.0 design baselines | Apple adapters exist; target build and runtime acceptance remain pending |

These are implementation and compatibility boundaries, not certification of every OS release or device. Windows 7/8.1, Android 5.0, Android ARMv7 execution, physical ARM boards and other libc/toolchain combinations need target-specific acceptance. Emulation, native-bridge execution and QEMU cannot establish physical-device behavior or performance. BSD, iOS, FreeRTOS and bare metal are not advertised as validated ports.

## 2. Query capabilities and handle limits

Use module availability queries and returned statuses. An enabled module does not imply that every platform-specific operation exists. Unsupported operations return `HX_OS_NOT_SUPPORTED`; native permission and I/O failures retain diagnostic information.

Common boundaries include:

- Linux sleep inhibition requires an available desktop D-Bus service. Embedded/headless deployments can disable it; see [desktop power requests](LINUX_DESKTOP.md).
- Android application directories come from the host application. Process support depends on the build minimum API, and local sockets remain subject to SELinux and UID permissions.
- Stackful coroutines require an implemented CPU/context backend. Active Linux shadow-stack restrictions are reported explicitly; see [coroutines](COROUTINES.md).
- Clocks, timeouts, file synchronization and device access retain their documented native limits. A deadline is not a guarantee that arbitrary native I/O can be interrupted.
- Ordinary UDP can lose, reorder or duplicate datagrams. Fragmented IPv4 traffic can also be affected by WSL mirrored networking; use path-sized datagrams or TCP/KCP as appropriate. See [IPC transport choices](IPC.md).

## 3. Build, install and run examples

New configurations build the library only. Enable `HX_OS_BUILD_EXAMPLES=ON` to compile public examples; they require no private maintainer files. Reuse one build directory per toolchain/configuration.

See [dependency preparation](DEPENDENCIES.md), [build options and installation](REFERENCE.md#section-27), and [native SDK access](NATIVE_DEPENDENCIES.md). Shared-library deployments include their selected runtime dependencies. Static core linkage does not automatically make every dependency static.

Start with a small example for the facility used by your application, then check its actual permissions, paths, input sizes and close behavior on the deployment target. Detailed regression logs and historical audit records are maintained locally and are excluded from the repository and installed SDK. Remote CI is disabled.

## 4. Additional network boundaries

The new UDP packet metadata, vector I/O, bounded TCP dialer, c-ares resolver, TLS profiles, HTTP streaming and coroutine bridges target Windows, Linux and Android. Linux ARMv7/AArch64 use the shared POSIX path; cross-compilation and QEMU/emulator execution do not establish hardware acceptance. BSD, iOS, QNX, RTOS, Harmony and WASM are not new validated targets in this work.

Interface changes use native notifications and can be rejected by Android sandbox/SELinux permissions. Requery the snapshot after a notification; it is not a guaranteed event history. TCP user timeout is Linux/Android-specific, UDP ICMP reset control is Windows-specific. Named IPv6 zones require NET_OPTIONS. IDNA depends on the selected curl backend; unsupported conversions report `NOT_SUPPORTED`. c-ares is optional and is not a replacement for every system/VPN DNS policy. No adapter changes firewall rules, WSL mode or global networking settings.
