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

Pass --adb for an absolute adb path. The runner checks ABI/API, uploads only its artifacts into a fresh /data/local/tmp/hx_c_platform-UUID directory, sets that process's LD_LIBRARY_PATH, and executes thirty-one suites:

| Suite | Coverage |
| --- | --- |
| android_baseline | Platform/API, Unicode files/I/O, thread/event/TLS cleanup, finite join |
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
| kcp / ipc | Loss/retransmission, complete messages, duplex/concurrent requests, restart, backpressure and close; explicit local-path permission skips |
| lifecycle | Allocation failures, join timeout during blocked TLS cleanup and successful retry after release |
| config_tree / codec / cpp_codec | Deep mixed trees, seven writers, schema mapping, ownership and no-exception C++11 |

hx_os_portable_baseline also records atomic, ASCII, wide conversion, named-call and precise-wait observations without fixed timing thresholds. Logs include NDK, source revision/dirty status, ABI/API, device model, fingerprint, runtime API, emulator marker and command output. Cleanup removes only explicitly uploaded files, never recursively deleting test directories. Failed fixtures remain. An adb timeout may leave the target process running; preserve/report its directory without killing it. Record load/temperature/ frequency conditions and collect repeated performance samples.

Reports distinguish build minimum API, runtime API, native machine and translation, with a result per executable. Failures do not remain labeled as unrun. Shared validation executables have $ORIGIN RUNPATH for newer Android; older native systems still use LD_LIBRARY_PATH. Only test/example/benchmark targets receive this option; consumer and system library paths are unchanged.

For a focused communication run, append `--suite ipc --suite kcp --suite lifecycle`. Without `--suite`, all thirty-one suites run. The portable benchmark still runs; the report records selected suites and explicit subcase skips.

For serialization only, append `--suite config_tree --suite codec --suite cpp_codec`. See the [serialization tutorial](SERIALIZATION.md).

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

## Runtime limits

Minimum API 21 is a build target, not proof of execution on Android 5.0. API 21 runtime, ARMv7 execution, physical ARM hardware and APK sandbox behavior still need matching target environments. Emulator translation does not establish native ARM performance.

Unix-domain sockets require suitable UID and SELinux permissions. A shell-based run may receive `EACCES` even when another transport works; validate Unix sockets inside the intended application environment.

A returned thread function does not mean its TLS destructors have finished. Finite join includes thread-exit cleanup and preserves the owned thread on timeout for retry; the library does not silently change a finite wait to an infinite wait.

Shared example/test executables use executable-relative RUNPATH where supported. RUNPATH requires API 24 or newer; older native systems use `LD_LIBRARY_PATH`. See the [Android linker documentation](https://android.googlesource.com/platform/bionic/+/refs/heads/android15-tests-release/android-changes-for-ndk-developers.md).

The optional automatic C syntax generator runs on the build host. Cross-compiling the core does not validate generated application sources against every Android sysroot; compile those sources with the application's target toolchain.
