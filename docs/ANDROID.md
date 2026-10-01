# Android validation baseline

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

The agreed baseline is **Android 5.0 / API 21**, **arm64-v8a and armeabi-v7a**.
API 28 also covers newer native facilities. Android is an explicit target;
desktop Linux, WSL and glibc/QEMU results are not Android runtime evidence.

## Build and run

Use an existing NDK; the script does not download or modify SDK/vendor files:

~~~text
python tools/verify_android.py --ndk /absolute/path/to/ndk/28.2.13676358
~~~

Default: both ABIs at API 21/28, library, examples, benchmark, all test
executables and standalone C99/C++11 public headers. Use --ninja to select
Ninja explicitly. Local Windows paths are E:/Android/Sdk/ndk/28.2.13676358
and E:/Android/Sdk/cmake/3.22.1/bin/ninja.exe.

~~~text
python tools/verify_android.py --ndk /path/to/ndk --api 21 --minimal
python tools/verify_android.py --ndk /path/to/ndk --api 21 --static
~~~

Minimal uses cmake/minimal.cmake. Shared is default; --static selects static.
Use --build-root for separate records. Without --serial, the script only
cross-compiles and never selects a device or executes ARM binaries.

~~~text
adb devices -l
python tools/verify_android.py --ndk /path/to/ndk --api 21 --abi arm64-v8a --serial DEVICE_SERIAL
~~~

Pass --adb for an absolute adb path. The runner checks ABI/API, uploads only
its artifacts into a fresh /data/local/tmp/hx_c_platform-UUID directory,
sets that process's LD_LIBRARY_PATH, and executes eight suites:

| Suite | Coverage |
| --- | --- |
| android_baseline | Platform/API, Unicode files/I/O, thread/event/TLS cleanup, unsupported finite join |
| string_tools | Unicode, NUL, aliases, failure retention, typed containers |
| lexer | Positions, surrogate pairs, sticky errors, resource limits |
| wide_wait | Big-integer reference values, rounding/overflow, waits/repeat |
| catalog_filename | Overlays, allocation failure, BOM/encoding, filenames |
| procedure | In-flight retirement, retry/self-wait, allocator reentry, allocation-free warm calls |
| atomic | 32/64-bit atomics, publication, concurrent counters |
| cpp_utilities | C++11 strings, lexing, translations, procedures and pools |

hx_os_portable_baseline also records atomic, ASCII, wide conversion,
named-call and precise-wait observations without fixed timing thresholds.
Logs include NDK, source revision/dirty status, ABI/API, device model,
fingerprint, runtime API, emulator marker and command output. Cleanup removes
only explicitly uploaded files, never recursively deleting test directories.
Failed fixtures remain. An adb timeout may leave the target process running;
preserve/report its directory without killing it. Record load/temperature/
frequency conditions and collect repeated performance samples.

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

Module bits describe compiled families, not every platform extension.
API 21 binaries remain process-disabled on newer devices; build with the
higher minimum API for that feature. No JNI/Java dependency, APK, manifest
permissions, asset API or UI automation is supplied. Consumers own those.
Standalone validation uses c++_static; consumers retain their NDK STL policy.

## Current evidence

2026-10-01, Windows build host, NDK r28c / Clang 19.0.1:

| Configuration | Cross-build | Target execution |
| --- | --- | --- |
| API 21 arm64-v8a, shared/default modules | Passed | Pending device |
| API 21 armeabi-v7a, shared/default modules | Passed | Pending device |
| API 28 arm64-v8a, shared/default modules | Passed | Pending device |
| API 28 armeabi-v7a, shared/default modules | Passed | Pending device |

See [validation](VALIDATION.md) for minimal/static and host results.
adb found no attached device. No Android runtime pass count, performance or
emulator/physical-device success is claimed. CI contains the pinned-NDK
matrix and API 21 minimal builds; no remote run is claimed. Target acceptance
still needs API 21/newer systems, both bit widths, app sandbox, permissions,
device provenance and repeated measurements.
