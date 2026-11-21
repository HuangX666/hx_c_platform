# Android build and deployment

[简体中文](ANDROID.zh-CN.md) · [Home](../README.md)

`hx_c_platform` supports cross-compiling for Android with a baseline of **Android 5.0 (API 21)** across **`arm64-v8a`** and **`armeabi-v7a`** (as well as `x86_64` for emulators), while **API 28+** unlocks newer Bionic facilities such as `posix_spawn` process creation.

## 1. Build the library and examples

Run the commands below in a Linux or WSL shell with an installed Android NDK. You can start by building the zero-dependency minimal profile (`cmake/minimal.cmake`) and the `hx_os_hello` example as a static binary for quick `adb` testing. When your application needs TLS, archives, or HTTP, build an ABI- and API-matched dependency SDK first using the [dependency guide](DEPENDENCIES.md#6-cross-build-linux-arm-and-android) and enable the corresponding CMake modules:

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

- Point `ANDROID_NDK_ROOT` at your installed NDK directory and replace `DEVICE_SERIAL` with the target device ID reported by `adb devices -l`.
- Change `-DANDROID_ABI` to `armeabi-v7a` for 32-bit ARMv7 or `x86_64` for an x86_64 emulator, using a separate build directory per ABI.

## 2. Shared runtimes and application integration

The quick-start command above links the minimal library statically (`-DBUILD_SHARED_LIBS=OFF`) so the single `hx_os_hello` binary runs directly over `adb`. When building shared libraries (`-DBUILD_SHARED_LIBS=ON`) or integrating into an Android application, keep these deployment rules in mind:

- **Deploying `.so` files and avoiding BoringSSL symbol collisions**:
  - For command-line shared builds, push `libhx_c_platform.so` and all required third-party dependency `.so` files from that same build alongside the executable and set `LD_LIBRARY_PATH` for the process.
  - Dynamic OpenSSL runtime files are listed in `hx_openssl_runtime_Release.txt` inside the build directory. The project's Android dependency build names its OpenSSL shared libraries **`libssl-hx.so` and `libcrypto-hx.so`** so they never collide with the system BoringSSL (`libssl.so` / `libcrypto.so`) preloaded into Android app processes. Never mix ABIs or copy host Linux `.so` binaries onto an Android device.
- **APK packaging and sandbox paths**:
  - `hx_c_platform` is a native C99/C++11 systems library: it does not bundle Java/JNI bindings, package APKs, edit `AndroidManifest.xml` permissions, or guess private app directories.
  - Obtain writable sandbox directories from your host application via `Context.getFilesDir()` / `getCacheDir()` or `NativeActivity`, and pass those UTF-8 paths explicitly to file I/O, temporary file creation (`hx_os_temp_create_file`), persistent queues, and file-backed shared memory (`hx_os_shared_memory_create_at`).
  - C++ consumers retain their NDK project's STL choice; when using `c++_shared`, package the matching `libc++_shared.so` in your APK.

## 3. Platform boundaries

Because Android uses Bionic libc and enforces per-app UID and SELinux sandboxing, certain OS facilities behave differently from desktop Linux:

| Facility | Android Behavior and Boundaries |
| --- | --- |
| Files, UTF-8, threads, synchronization, TLS, atomics | Shared C/POSIX implementation, subject to Bionic behavior and app file permissions |
| `epoll`, `inotify`, sockets, anonymous pages, file mappings | Shared Linux adapters, subject to the Android app sandbox |
| Child processes (`hx_os_process`) | **API 21 build**: returns `HX_OS_NOT_SUPPORTED` with `HX_OS_CAP_PROCESS` cleared; **API 28+ build**: uses `posix_spawn` |
| Deadline-bounded thread join (`hx_os_thread_join_until`) | Polls Bionic's public `pthread_gettid_np` for the kernel-cleared `tid` (after TLS destructors finish) before calling `pthread_join`, supporting timeout retries |
| Thread pool, directory watch, and file writer shutdown | Uses the bounded native join above so worker retirement always includes full TLS cleanup |
| Local IPC | Loopback TCP, UDP, and KCP work normally; `AF_UNIX` pathname sockets are subject to UID and SELinux policy; shared memory uses file-backed `create_at` / `open_at` in an authorized directory |
| Strict CPU affinity | Returns `HX_OS_NOT_SUPPORTED` |
| Standard path lookups (`home`/`config_home`/`data_home`/`program_data`/`temp`) | Return `HX_OS_NOT_SUPPORTED`; obtain app-specific paths via `Context` / `NativeActivity` and pass them explicitly |
| Executable and module paths (`executable` / `module`) | Resolved via `/proc/self/exe` and `dladdr`, preserving any native OS restrictions |
| OS version string (`hx_os_sys_version_string`) | Reports the Linux kernel / `uname` release string (not the Android Java API level) |
| Desktop-only extensions (Windows registry, desktop power inhibitors) | Return `HX_OS_NOT_SUPPORTED` with capability bits cleared |
| `/proc`, CPU topology, and process/network inspection | Return explicit native permission errors or `HX_OS_NOT_SUPPORTED` when restricted by SELinux, never faking metrics or success |

> [!IMPORTANT]
> **Minimum Build API Level (`ANDROID_PLATFORM`) Controls Process Spawning**
> A binary compiled with `-DANDROID_PLATFORM=android-21` disables child-process spawning at build time (`HX_OS_NOT_SUPPORTED`), even if that binary is later executed on an Android 10+ device. Configure with `-DANDROID_PLATFORM=android-28` or higher when your target requires `hx_os_process`.

## 4. Before deployment

- **Verify on physical target hardware and inside the APK sandbox**: Check device ABI compatibility, app UID permissions, SELinux policies, and required manifest permissions (such as `android.permission.INTERNET`). Desktop Linux runs, WSL runs, and emulator ARM translation do not substitute for testing on physical ARM devices and inside an actual APK sandbox. See [platform support](PLATFORMS.md) for other target details.
- **DNS resolution and cancellable HTTP on Android**:
  - Network helpers use the standard socket/`epoll` backend without binding Android Java `Network` handles or bypassing VPN/sandbox rules (network-interface watch subscriptions may fail with native permission errors on restricted Android releases).
  - When `c-ares` is enabled for explicit DNS lookups, TCP dialing, or TLS startup, pass explicit DNS server addresses if the app sandbox does not expose system resolver configuration files.
  - For the HTTP coroutine service, the default SDK's system resolver can block during shutdown if a DNS server never replies. To enable cancellable HTTP DNS, link against a dependency SDK built with `HX_DEPS_CURL_CARES=ON` and call `hx_os_coro_http_use_cancellable_dns` before submitting requests (while supplying valid DNS configuration). See [network I/O](NETWORK_IO.md) and [HTTP transfers](HTTP_FILES.md) for details.
