# Android validation baseline

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

The agreed baseline is **Android 5.0 / API 21**, **arm64-v8a and armeabi-v7a**. API 28 also covers newer native facilities. Android is an explicit target; desktop Linux, WSL and glibc/QEMU results are not Android runtime evidence.

## Build the library and examples

Run these commands in a Linux/WSL shell with an installed NDK. SDK sources are unchanged. Start with the minimal features and `hello` example; for TLS/archive, prepare an ABI/API-matched SDK using the [dependency guide](DEPENDENCIES.md#6-cross-build-linux-arm-and-android), then enable the required modules.

```sh
cmake -C cmake/minimal.cmake -S . -B out/android/arm64-v8a -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release -DHX_OS_BUILD_EXAMPLES=ON
cmake --build out/android/arm64-v8a --target hx_os_hello --parallel 4
adb devices -l
adb -s DEVICE_SERIAL shell mkdir -p /data/local/tmp/hx-platform-demo
adb -s DEVICE_SERIAL push out/android/arm64-v8a/bin/hx_os_hello /data/local/tmp/hx-platform-demo/
adb -s DEVICE_SERIAL shell chmod 755 /data/local/tmp/hx-platform-demo/hx_os_hello
adb -s DEVICE_SERIAL shell /data/local/tmp/hx-platform-demo/hx_os_hello
```

Set `ANDROID_NDK_ROOT` to the installed NDK and replace `DEVICE_SERIAL` with the selected device. Use `armeabi-v7a` for ARMv7 or `x86_64` for a matching emulator; reuse one build directory per ABI. The build minimum API is distinct from the device OS version. The library and examples need no private regression files.

For shared builds, deploy `libhx_c_platform.so` and required dependency runtimes beside the executable, then set a process-local `LD_LIBRARY_PATH`. Dynamic OpenSSL files are listed in `hx_openssl_runtime_Release.txt` in the build directory; deploy the runtime SONAME files and their targets, never host Windows/Linux libraries. The project Android SDK uses `libssl-hx.so` / `libcrypto-hx.so` to avoid system BoringSSL name collisions. Match ABI, API and dependency versions. The host application owns APK packaging and app directory permissions.

The dated evidence and platform limits below are historical. Automated runners, logs and regression cases are kept only on the maintainer's machine, not required by these public build instructions.

## Recorded acceptance (2026-10-07)

All executions below use the same API 34 x86_64 `Qt_Android_34` emulator. The full profile enables Crypto, TLS, Archive, optional 7-Zip, all five native parser backends and GNU iconv. Its dependency SDK includes OpenSSL, zlib, XZ and Zstd for the selected target. Counts exclude the additional portable benchmark, which passed in every executed configuration.

| Build configuration | Execution | Native CTest tests/examples |
| --- | --- | --- |
| x86_64 API 21 full shared | Native ISA | 159 passed, 3 explicit skips |
| arm64-v8a API 21 full shared | Native-bridge translation | 159 passed, 3 explicit skips |
| x86_64 API 28 full shared | Native ISA | 160 passed, 2 explicit skips |
| x86_64 API 21 minimal shared | Native ISA | 108 passed |
| armeabi-v7a API 21 full shared | Incompatible emulator ABI | Compilation passed; runtime explicitly skipped |

API 21 skips pathname Unix sockets denied by SELinux, application configuration-directory lookup without an app context, and the process-capture example. API 28 enables and passes process creation/capture; it retains the first two skips. IPC and C++ platform suites record their Unix-socket subcase separately and continue. No security setting was changed. The actual build minimum, runtime API and ISA are separate report fields.

Reports are `build-platform-android-x64/report.json`, `build-platform-android-arm64/report.json`, `build-platform-android-api28/report.json`, `build-platform-android-armv7/report.json` and `build-platform-android-minimal/report-minimal.json`. Their ABI directories contain complete `configure.log`, `build.log`, `run-*.log` and runtime upload logs. Final feature flags come from the generated configuration header, including the CRYPTO + OPENSSL combination. Host deployment regressions run with `python -B tests/test_android_runner.py` and cover missing files, source-relative SDKs, native-test discovery and SONAME aliases.

Android 5.0 OS execution, Android ARMv7 execution, APK sandbox behavior and physical ARM hardware remain separately unverified. The user-approved acceptance for this round uses available emulator/QEMU environments; Linux ARM QEMU results are in [validation](VALIDATION.md) and are not Android results. Remote CI remains manual-only and was not started.

## Runtime details exposed by the expanded tests

API 23+ Bionic uses the GNU `strerror_r` return type when `_GNU_SOURCE` is enabled; the error adapter handles this separately from API 21. API 28 process creation uses native `posix_spawn`: a created child may report pre-exec failure by exit code 127. This is tested as an explicit POSIX outcome, not relabeled as an immediate native errno. POSIX examples use main's argv, so an ARM native-bridge launcher in procfs does not replace their actual arguments.

Archive output-ancestor checks use search-only directory descriptors on Linux/Android and retain `O_NOFOLLOW`. They no longer require listing permission for ancestors such as `/data`; a dedicated execute-only-parent regression is included. Test scratch paths use the runner's owned storage instead of desktop `/tmp`. Unavailable app-directory queries, process support below API 28 and SELinux-denied pathname Unix sockets are reported explicitly.

## Serialization verification (2026-10-04)

Commit `8de8da1`: minimum-API-21 full shared builds for x86_64 and arm64-v8a each passed config_tree, codec and cpp_codec on Qt_Android_34, plus one portable benchmark execution. The actual OS was Android 14 / API 34 on x86_64; ARM64 used libndk_translation. A temporary read-only, no-snapshot, headless emulator was closed afterward without wiping or saving user data.

Reports are build-android-emulator/report.json and build-android/report.json. Each ABI directory retains run-hx_os_test_config_tree.log, run-hx_os_test_codec.log and run-hx_os_test_cpp_codec.log. ARMv7 cross-compilation passed but runtime remains pending because this emulator lacks that ABI. API 21 OS execution, physical ARM hardware, APK sandbox execution and performance acceptance remain unverified. The other 28 suites were not rerun in this follow-up.

## Platform boundaries

| Facility | Current behavior |
| --- | --- |
| Files, UTF-8, threads, synchronization, TLS, atomics | Shared C/POSIX, subject to Bionic and permissions |
| epoll/inotify/sockets/pages/mappings | Shared Linux adapters, subject to app sandbox |
| Processes | API 21 build: NOT_SUPPORTED, PROCESS bit cleared; API 28: posix_spawn |
| Finite thread join | Poll Bionic's public pthread_gettid_np for the kernel-cleared TID before final join; includes TLS cleanup and allows retry |
| Final worker/watch/IPC joins | Use the finite native join; function return is not TLS completion |
| Local IPC | TCP, ordinary UDP and KCP; Unix paths remain subject to UID and SELinux policy, with explicit native permission errors |
| Strict affinity | NOT_SUPPORTED in this adapter |
| HOME/config/data/shared-data/temp | NOT_SUPPORTED; host obtains app roots via Context/NativeActivity and passes paths |
| Executable/module location | /proc/self/exe and dladdr with explicit native restrictions |
| System version | Android kernel/uname release, not Android API |
| Unimplemented registry/power extensions | NOT_SUPPORTED with existing capability rules |
| /proc, topology, process/network access | Preserve native/permission failures, never fake metrics or success |

Module bits describe compiled families, not every platform extension. API 21 binaries remain process-disabled on newer devices; build with the higher minimum API for that feature. No JNI/Java dependency, APK, manifest permissions, asset API or UI automation is supplied. Consumers own those. Standalone validation uses c++_static; consumers retain their NDK STL policy.

## IPC and bounded native join verification (2026-10-03)

The API 21 ARM64 shared configuration runs twenty-eight suites and the portable benchmark through ARM translation on the existing API 34 x86_64 emulator. TCP, ordinary UDP and KCP cover complete messages, duplex/concurrent requests, restart, bounds and closure. This emulator's SELinux policy denies shell creation of pathname Unix sockets (`sock_file create`, EACCES); that subcase is explicitly skipped, with reasons retained in report `test_notes` and logs. System security settings were not relaxed, and Unix socket runtime support was not claimed. Validate that transport inside an appropriately permitted APK/target environment.

Bounded Android join uses Bionic's public API to observe the kernel-cleared TID before reclaiming an owned, joinable thread. [Bionic's join implementation](https://android.googlesource.com/platform/bionic/+/master/libc/bionic/pthread_join.cpp) also waits for that clearing before final reclamation. The `lifecycle` suite specifically covers a returned thread function whose TLS destructor is still blocked. Compiling for API 21 does not prove execution on Android 5.0 hardware; ARMv7 still requires a target run. The finite-join restrictions below are historical and are superseded by this section for current behavior.

## Containers and coroutine verification (2026-10-03, historical)

NDK r28c builds the full shared library at minimum API 21 for arm64-v8a, armeabi-v7a and x86_64, including self-developed stackful adapters, the C99 state machine, C++11 go and optional C++20 layer. The API 34 Qt_Android_34 emulator passes twenty suites plus the portable benchmark for native x86_64 and translated ARM64. ARMv7 runtime is explicitly skipped because it is absent from the emulator ABI list. Reports are build-android/report.json and build-android-emulator/report.json.

New checks include RB/AVL invariants, duplicate keys, algorithms, channel/select, cancellation, prepare/close cleanup, floating-point state, profiler isolation, TCP/DNS/pipes and C++20 frame cleanup. The runtime retains the finite-native-join restriction: controlled tests explicitly choose infinite final join after arranging cooperative exit. The library never silently turns finite close into infinite close. API 21 runtime, ARMv7 execution and physical ARM boards remain unverified. Earlier minimal/static/API 28 records are historical, not reruns of this addition. See [validation and local measurements](VALIDATION.md).

## Recursive search verification (2026-10-03)

API 21 ARM64/ARMv7 full and minimal shared configurations all build, including C99/C++11 headers. Each ARM64 configuration passes fourteen suites and the portable benchmark on the API 34 x86_64 emulator through native-bridge translation. ARMv7 runtime is explicitly skipped for an incompatible ABI. file_search adds depth/width, Unicode, first-match, callback, link, allocation and disabled-module checks; cpp_utilities also exercises search in the existing pool. These results do not establish Android 5.0 runtime, physical ARM performance or app sandbox behavior. See [validation](VALIDATION.md).

## Variant verification (2026-10-03)

At the variant stage, the selected subset contained thirteen suites, adding variant/cpp_variant; the portable benchmark added integer and custom-structure set/get timing observations. API 21 ARM64/ARMv7 full and minimal shared builds included the module, disabled-module tests and standalone C99/C++11 headers. Runtime and pending targets are recorded separately in [VALIDATION.md](VALIDATION.md). Prior records below describe their original revisions and selected subsets.

## Common-call verification (2026-10-01)

NDK r28c / Clang 19 builds API 21 arm64-v8a and armeabi-v7a, each in full/minimal shared configurations, including standalone C99/C++11 headers and the direct-call example. Both ARM64 configurations pass all 11 selected suites plus the benchmark on Qt_Android_34 / emulator-5580 (API 34, native x86_64, ARM64 native-bridge translation). ARMv7 runtime is explicitly skipped because the emulator advertises only x86_64 and arm64-v8a. The convenience suite checks retained strings during concurrent mutation/after close, allocation-failure cleanup, immediate update semantics, Unicode directory visits and module-off behavior. No Android 5.0 runtime or physical ARM timing claim follows; API 28/static/x86_64 configurations below remain earlier evidence.

## Result migration verification (2026-10-01)

API 21 arm64-v8a and armeabi-v7a full/minimal shared configurations compile. Both ARM64 configurations pass 10/10 suites on the existing API 34 x86_64 emulator through native-bridge translation. ARMv7 is absent from its ABI list and is explicitly skipped at runtime. All C++ tests disable exceptions. Finite join reports the capability limit; controlled tests explicitly choose an infinite final join. The table below preserves the earlier eight-suite baseline; other API/link configurations were not rerun for this migration.

## Earlier baseline evidence

2026-10-01, Windows build host, NDK r28c / Clang 19.0.1:

| Configuration | Cross-build | Target execution |
| --- | --- | --- |
| API 21 arm64-v8a, shared/default modules | Passed | Android 14 translated, 8/8 |
| API 21 armeabi-v7a, shared/default modules | Passed | Pending device |
| API 28 arm64-v8a, shared/default modules | Passed | Android 14 translated, 8/8 |
| API 28 armeabi-v7a, shared/default modules | Passed | Pending device |
| API 21 x86_64, shared/default modules | Passed | Android 14 native ISA, 8/8 |
| API 28 x86_64, shared/default modules | Passed | Android 14 native ISA, 8/8 |
| API 21 arm64-v8a, static/default modules | Passed | Android 14 translated, 8/8 |
| API 21 arm64-v8a, shared/minimal modules | Passed | Android 14 translated, 8/8 |

See [validation](VALIDATION.md) for minimal/static and host results. The existing Qt_Android_34 AVD was booted as emulator-5580: API 34, native x86_64, with libndk_translation. Each of the six executed configurations passes the earlier eight-suite Android subset and runs the benchmark once. Minimal verifies disabled-feature behavior. These eight suites are not the entire desktop CTest collection.

ARM64 shared execution initially could not locate the library through LD_LIBRARY_PATH. Executable-relative RUNPATH fixed both shared configurations and minimal. RUNPATH is supported since API 24; see the [Android linker documentation](https://android.googlesource.com/platform/bionic/+/refs/heads/android15-tests-release/android-changes-for-ndk-developers.md). ARM64 results include translation/virtualization and are not hardware performance evidence. The AVD offers no ARMv7 ABI, so deployment explicitly skips it. Building with minimum API 21 is not an Android 5.0 runtime test. Old OS releases, ARMv7, physical ARM and application sandbox permissions still require matching environments.

Reports: build-android-emulator/report.json, build-android/report.json, build-android/report-minimal.json and build-android-static/report.json; run-*.log files under each configuration contain actual program output. CI contains the pinned-NDK matrix and API 21 minimal builds; no remote run is claimed.

## 2026-10-03 additions

The host-only automatic C syntax generator is tested separately on Windows/MSVC and WSL/GCC, including installed consumers. Android target tests exercise the generated-independent C99 engine and exception-free C++ wrappers. A cross build of the core does not validate arbitrary generated application sources against a particular Android sysroot. The current coroutine/format additions and actual ABI/runtime evidence are recorded in [validation](VALIDATION.md).
