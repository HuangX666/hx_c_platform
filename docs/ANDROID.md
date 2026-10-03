# Android validation baseline

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

The agreed baseline is **Android 5.0 / API 21**, **arm64-v8a and armeabi-v7a**. API 28 also covers newer native facilities. Android is an explicit target; desktop Linux, WSL and glibc/QEMU results are not Android runtime evidence.

## Build and run

Use an existing NDK; the script does not download or modify SDK/vendor files:

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

Pass --adb for an absolute adb path. The runner checks ABI/API, uploads only its artifacts into a fresh /data/local/tmp/hx_c_platform-UUID directory, sets that process's LD_LIBRARY_PATH, and executes twenty-five suites:

| Suite | Coverage |
| --- | --- |
| android_baseline | Platform/API, Unicode files/I/O, thread/event/TLS cleanup, unsupported finite join |
| string_tools | Unicode, NUL, aliases, failure retention, typed containers |
| lexer | Positions, surrogate pairs, sticky errors, resource limits |
| wide_wait | Big-integer reference values, rounding/overflow, waits/repeat |
| catalog_filename | Overlays, allocation failure, BOM/encoding, filenames |
| procedure | In-flight retirement, retry/self-wait, allocator reentry, allocation-free warm calls |
| atomic | 32/64-bit atomics, publication, concurrent counters |
| cpp_utilities | No-exception C++11 results, strings, lexing, procedures and allocation rollback |
| cpp_pool | Typed results, cancellation, admission, preparation/close races and cleanup |
| cpp_extensions | Settings snapshots, signal results, capture retirement, bitstreams and extensions |
| convenience | In-place strings, retained setting strings under concurrent updates, immediate cursor failures, borrowed catalog values and bounded directory visits |
| variant | Exact types, UTF-8/NUL, custom copy/drop, alignment, ownership/context cleanup and container failure paths |
| cpp_variant | Exception-free C++11 set/get/emplace/clone/visit, typed alternatives, move-only objects and allocator failures |
| file_search | Streaming/first-match recursive search, Unicode, limits, links, allocation cleanup and module-off behavior |
| containers / stl | RB/AVL/duplicate invariants, traversal, sorting, filtering, splice and set operations |
| coroutine / coroutine_io | Stackful/stackless scheduling, floating-point state, channel/select, prepare rollback, close retry, TCP/DNS/pipes and cancellation |
| cpp_coroutine / cpp_coroutine20 | Exception-free C++11 go, C++20 frames/locals, waits and cancellation cleanup |
| coroutine_sync / cpp_go11 / cpp_go20 | Runtime-owned tasks, errors, owned channel values, cooperative locks, condition cancellation and local cleanup |
| format / cpp_format | Typed UTF-8 formatting, custom values, failed-output retention and allocator cleanup |

hx_os_portable_baseline also records atomic, ASCII, wide conversion, named-call and precise-wait observations without fixed timing thresholds. Logs include NDK, source revision/dirty status, ABI/API, device model, fingerprint, runtime API, emulator marker and command output. Cleanup removes only explicitly uploaded files, never recursively deleting test directories. Failed fixtures remain. An adb timeout may leave the target process running; preserve/report its directory without killing it. Record load/temperature/ frequency conditions and collect repeated performance samples.

Reports distinguish build minimum API, runtime API, native machine and translation, with a result per executable. Failures do not remain labeled as unrun. Shared validation executables have $ORIGIN RUNPATH for newer Android; older native systems still use LD_LIBRARY_PATH. Only test/example/benchmark targets receive this option; consumer and system library paths are unchanged.

## Platform boundaries

| Facility | Current behavior |
| --- | --- |
| Files, UTF-8, threads, synchronization, TLS, atomics | Shared C/POSIX, subject to Bionic and permissions |
| epoll/inotify/sockets/pages/mappings | Shared Linux adapters, subject to app sandbox |
| Processes | API 21 build: NOT_SUPPORTED, PROCESS bit cleared; API 28: posix_spawn |
| Finite thread join | No required Bionic tryjoin; NOT_SUPPORTED retains thread; infinite join includes native TLS cleanup |
| Final worker/watch joins | Same finite-join limit; a pre-exit flag is not TLS completion |
| Strict affinity | NOT_SUPPORTED in this adapter |
| HOME/config/data/shared-data/temp | NOT_SUPPORTED; host obtains app roots via Context/NativeActivity and passes paths |
| Executable/module location | /proc/self/exe and dladdr with explicit native restrictions |
| System version | Android kernel/uname release, not Android API |
| Unimplemented registry/power extensions | NOT_SUPPORTED with existing capability rules |
| /proc, topology, process/network access | Preserve native/permission failures, never fake metrics or success |

Module bits describe compiled families, not every platform extension. API 21 binaries remain process-disabled on newer devices; build with the higher minimum API for that feature. No JNI/Java dependency, APK, manifest permissions, asset API or UI automation is supplied. Consumers own those. Standalone validation uses c++_static; consumers retain their NDK STL policy.

## Containers and coroutine verification (2026-10-03)

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
