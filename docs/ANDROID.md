# Android build and deployment

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

The agreed baseline is **Android 5.0 / API 21**, **arm64-v8a and armeabi-v7a**. API 28 also covers newer native facilities. Android is an explicit target; desktop Linux, WSL and glibc/QEMU results are not Android runtime evidence.

## 1. Build the library and examples

Run these commands in a Linux/WSL shell with an installed NDK. SDK sources are unchanged. Start with the minimal features and `hello` example; for TLS/archive, prepare an ABI/API-matched SDK using the [dependency guide](DEPENDENCIES.md#6-cross-build-linux-arm-and-android), then enable the required modules.

```sh
hx_android_toolchain="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

cmake -C cmake/minimal.cmake -S . -B out/android/arm64-v8a -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$hx_android_toolchain" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DHX_OS_BUILD_EXAMPLES=ON
cmake --build out/android/arm64-v8a --target hx_os_hello --parallel 4
adb devices -l
adb -s DEVICE_SERIAL shell mkdir -p /data/local/tmp/hx-platform-demo
adb -s DEVICE_SERIAL push out/android/arm64-v8a/bin/hx_os_hello \
  /data/local/tmp/hx-platform-demo/
adb -s DEVICE_SERIAL shell chmod 755 \
  /data/local/tmp/hx-platform-demo/hx_os_hello
adb -s DEVICE_SERIAL shell /data/local/tmp/hx-platform-demo/hx_os_hello
```

Set `ANDROID_NDK_ROOT` to the installed NDK and replace `DEVICE_SERIAL` with the selected device. Use `armeabi-v7a` for ARMv7 or `x86_64` for a matching emulator; reuse one build directory per ABI. The build minimum API is distinct from the device OS version. The library and examples need no private regression files.

For shared builds, deploy `libhx_c_platform.so` and required dependency runtimes beside the executable, then set a process-local `LD_LIBRARY_PATH`. Dynamic OpenSSL files are listed in `hx_openssl_runtime_Release.txt` in the build directory; deploy the runtime SONAME files and their targets, never host Windows/Linux libraries. The project Android SDK uses `libssl-hx.so` / `libcrypto-hx.so` to avoid system BoringSSL name collisions. Match ABI, API and dependency versions. The host application owns APK packaging and app directory permissions.

The dated evidence and platform limits below are historical. Automated runners, logs and regression cases are kept only on the maintainer's machine, not required by these public build instructions.

## 2. Shared runtimes and application integration

The introductory example explicitly links the minimal library statically for direct adb execution. For full modules, prepare dependencies for the same ABI/API and enable the required features. Deploy shared executables with the required `.so` files from that build's installation; command-line execution must include their directory in `LD_LIBRARY_PATH`. Do not mix ABIs or system-provided crypto libraries with the same names.

The host application packages native libraries and declares its permissions. This library does not generate APKs, edit manifests or infer application directories. Pass writable paths obtained through Context/NativeActivity explicitly to file, queue and shared-memory entry points.

## 3. Platform boundaries

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

Module bits describe compiled families, not every platform extension. API 21 binaries remain process-disabled on newer devices; build with the higher minimum API for that feature. No JNI/Java dependency, APK, manifest permissions, asset API or UI automation is supplied. Consumers own those. C++ consumers retain their NDK STL policy; deploy the matching libc++_shared.so when using c++_shared.

## 4. Before deployment

Check the device ABI, application UID, SELinux rules, and directory/device permissions. API 21 is a build minimum, not evidence of execution on Android 5.0. Emulator ARM translation is separate from physical ARM execution. Android 5.0, ARMv7 execution and APK sandbox behavior each need target acceptance.

Public examples run independently. Regression sources, detailed logs and historical reports remain local and are not distributed with the repository or SDK. See [platform support](PLATFORMS.md) for other target boundaries.

New network helpers use the same socket/epoll implementation. c-ares can be enabled for explicit DNS, TCP dial and TLS startup; pass DNS servers if the app environment cannot expose its system configuration. The library does not bind an Android Network handle or bypass VPN/sandbox policy. Interface-watch subscription may fail with a native permission error. The HTTP coroutine service uses its own bounded pool; a blocked system resolver can still make close time out. See [network I/O](NETWORK_IO.md) and [HTTP transfers](HTTP_FILES.md).
