# Android validation baseline

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

The agreed baseline is **Android 5.0 / API 21**, **arm64-v8a and armeabi-v7a**. API 28 also covers newer native facilities. Android is an explicit target; desktop Linux, WSL and glibc/QEMU results are not Android runtime evidence.

New runs reuse `out/android/`; `--build-root` can reuse an existing directory. Historical paths below identify the original runs. After local build cleanup, retained JSON reports are under `out/records/<original-build-name>/`; old binaries and logs are not retained indefinitely.

## Build and run

Use an existing NDK; the script does not download or modify the SDK. Optional dependency acquisition follows the selected provider:

~~~text
python tools/verify_android.py --ndk /absolute/path/to/ndk/28.2.13676358
~~~

Default: both ABIs at API 21/28, library, examples, benchmark, all test executables and standalone C99/C++11 public headers. Use --ninja to select Ninja explicitly. Local Windows paths are E:/Android/Sdk/ndk/28.2.13676358 and E:/Android/Sdk/cmake/3.22.1/bin/ninja.exe.

~~~text
python tools/verify_android.py --ndk /path/to/ndk --api 21 --minimal
python tools/verify_android.py --ndk /path/to/ndk --api 21 --static
~~~

Minimal uses cmake/minimal.cmake. Shared is default; --static selects static. Use --build-root for separate records. Without --serial, the script only cross-compiles and never selects a device or executes ARM binaries.

An empty adb list can mean an installed AVD is stopped. This host has Qt_Android_34:

~~~text
emulator -list-avds
emulator -avd Qt_Android_34 -port 5580 -read-only -no-snapshot
~~~

Wait for boot and select emulator-5580. Read-only mode preserves AVD user data; no-snapshot disables snapshot load/save. Automated runs may add -no-window -no-audio. The AVD is x86_64; --abi x86_64 is now available as an additional emulator configuration. The two default ARM ABIs are unchanged.

~~~text
adb devices -l
python tools/verify_android.py --ndk /path/to/ndk --api 21 --abi arm64-v8a --serial DEVICE_SERIAL
~~~

Pass `--adb` for an absolute adb path. The runner checks the explicitly selected ABI/API, creates an isolated `/data/local/tmp/hx_c_platform-UUID` directory and discovers **all configured native CTest tests and examples** using `ctest --show-only=json-v1`. There is no manually maintained suite list. Arguments, fixture files, working directories, test environment, output expectations, skip codes and timeouts come from CTest. Host Python/tool commands are listed separately in `host_tests`; they are not counted as device executions.

The list therefore includes the new crypto/TLS/archive, streams and buffered writer, queue, INI/backup/settings, signal/parameter, parser/preprocessor and profiler tests alongside the existing platform, container, coroutine and serialization suites. Module-off builds exercise their explicit unsupported behavior. A selected 7-Zip runtime adds `archive_7zip`; absent platform-only facilities remain separately identified.

For the full optional dependency stack, first prepare one ABI/API SDK as described in [dependency builds](DEPENDENCIES.md#6-cross-build-linux-arm-and-android), then run:

```sh
python tools/verify_android.py --ndk /path/to/ndk --abi arm64-v8a --api 21 \
  --dependency-prefix /path/to/build-deps-android-arm64/prefix \
  --parsers --iconv --sevenzip-library /path/to/target/runtime/7z.so \
  --serial DEVICE_SERIAL --build-root out/android
```

Omit `--sevenzip-library` if the separate optional native backend is not needed. Omit `--dependency-prefix` only for a deliberate crypto/archive-disabled build. A prefix must match one explicit ABI/API; it is never reused across both default ARM ABIs. `--parsers` enables the five optional adapters, `--iconv` enables the pinned GNU converter, and `--provider FETCHCONTENT` selects the alternative source provider.

The runner reads the generated `hx_openssl_runtime_Release.txt` to deploy the exact selected **libssl and libcrypto**, including required versioned names, beside the test executables and core library. Missing runtimes fail deployment. Executables and the core use `$ORIGIN` RUNPATH plus a process-local library path. ARM translation also needs that explicit path for transitive dependencies. Only the distinct runtime SONAME files are deployed: ordinary `libssl.so` / `libcrypto.so` linker aliases must not shadow the x86_64 native bridge's system BoringSSL. This packaging behavior is covered by actual device runs, not just link success.

The prepared Android SDK uses distinct `libssl-hx.so` / `libcrypto-hx.so` SONAMEs through an upstream-supported configuration overlay. This also prevents the translated guest from binding to an already loaded system BoringSSL with the ordinary `libcrypto.so` name. The manifest and its resolved symlink targets determine which files are uploaded; unversioned BoringSSL linker aliases stay in the SDK.

hx_os_portable_baseline also records atomic, ASCII, wide conversion, named-call and precise-wait observations without fixed timing thresholds. Logs include NDK, source revision/dirty status, ABI/API, device model, fingerprint, runtime API, emulator marker and command output. Cleanup removes only explicitly uploaded files, never recursively deleting test directories. Failed fixtures remain. An adb timeout may leave the target process running; preserve/report its directory without killing it. Record load/temperature/ frequency conditions and collect repeated performance samples.

Reports distinguish build minimum API, runtime API, native machine and translation, with a result per executable. Failures do not remain labeled as unrun. Shared validation executables have $ORIGIN RUNPATH for newer Android; older native systems still use LD_LIBRARY_PATH. Validation executables and the Android core with dynamic OpenSSL use relative build RUNPATH. Installed shared libraries keep their existing relative install RUNPATH; no system library paths are changed.

For a focused communication run, append `--suite ipc --suite kcp --suite lifecycle`. Without `--suite`, every configured native CTest test and example runs. Names are the actual CTest names, not binary filenames. `--timeout-scale` explicitly scales device test limits, without changing library deadlines. The portable benchmark still runs; the report records selected suites and explicit subcase skips.

For serialization only, append `--suite config_tree --suite codec --suite cpp_codec`. See the [serialization tutorial](SERIALIZATION.md).

## Current acceptance (2026-10-07)

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
