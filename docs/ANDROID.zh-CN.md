# Android 构建与部署

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

`hx_c_platform` 支持交叉编译到 Android 平台，默认支持基线为 **Android 5.0（API 21）**，覆盖 **`arm64-v8a`** 与 **`armeabi-v7a`**（以及模拟器常用的 `x86_64`），同时支持在 **API 28+** 下启用 `posix_spawn` 子进程管理等新内核/Bionic 特性。

## 1. 编译库与示例

以下命令在 Linux 或 WSL 终端中执行，直接使用本地已安装的 Android NDK 工具链，无需修改任何库源码。你可以先使用 `cmake/minimal.cmake` 预设编译无第三方依赖的核心静态库与 `hx_os_hello` 示例，并通过 `adb` 推送到设备上运行；如果业务需要启用 TLS、归档压缩或 HTTP 模块，请先按[依赖教程](DEPENDENCIES.zh-CN.md#6-linux-arm-与-android-交叉构建)交叉编译匹配相同 ABI 与 API 级别的依赖 SDK，再开启对应模块：

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

- 将 `ANDROID_NDK_ROOT` 指向你本机的 NDK 根目录，并将 `DEVICE_SERIAL` 替换为 `adb devices -l` 列出的目标设备序列号。
- 若编译 32 位 ARMv7，将 `-DANDROID_ABI` 改为 `armeabi-v7a`；若编译 x86_64 模拟器版本，则改为 `x86_64`（建议为每种 ABI 使用独立的构建目录，如 `out/android/armeabi-v7a`）。

## 2. 共享运行库与应用接入

上一节的快速验证示例使用了静态链接（`-DBUILD_SHARED_LIBS=OFF`），因此单个可执行文件即可直接在 `adb shell` 中运行。当在工程中使用共享库（`-DBUILD_SHARED_LIBS=ON`）或接入完整第三方依赖时，请注意以下部署规则：

- **成套部署 `.so` 与避免 BoringSSL 符号冲突**：
  - 共享构建生成的可执行文件必须与同一次构建产出的 `libhx_c_platform.so` 及所需的第三方依赖 `.so` 放在同一目录；若通过 `adb shell` 命令行直接运行，需将该目录加入进程局部的 `LD_LIBRARY_PATH`。
  - 动态 OpenSSL 运行库的实际文件清单记录在构建目录下的 `hx_openssl_runtime_Release.txt` 中。本项目的 Android 依赖构建脚本专门将 OpenSSL 动态库命名为 **`libssl-hx.so` 与 `libcrypto-hx.so`**，以防止 Android 系统进程预加载的内部 BoringSSL（`libssl.so` / `libcrypto.so`）发生同名符号劫持。切勿将宿主机 Linux 的 `.so` 或不同 ABI 的库混入 Android 设备。
- **APK 集成与沙箱路径传递**：
  - 本库是纯 C99/C++11 原生系统层库，不包含 Java/JNI 包装、不生成 APK、不修改 `AndroidManifest.xml` 权限，也不会在底层隐式猜测应用的私有目录。
  - 在 Android 应用中，请先在 Java/Kotlin 层（`Context.getFilesDir()` / `getCacheDir()`）或 `NativeActivity` 中获取应用拥有读写权限的沙箱目录路径，再将其作为 UTF-8 字符串显式传给本库的文件读写、临时文件（`hx_os_temp_create_file`）、持久化队列或文件映射共享内存（`hx_os_shared_memory_create_at`）等入口。
  - C++ 接入方继续沿用宿主 NDK 工程自身的 STL 配置；若选用 `c++_shared`，请在 APK 中一并打包匹配的 `libc++_shared.so`。

## 3. 能力边界

Android 采用 Bionic C 库与严格的每应用 UID / SELinux 沙箱隔离，各功能模块在 Android 上的实际行为如下表所示：

| 功能领域 | Android 平台行为与边界 |
| --- | --- |
| 文件、UTF-8、线程、同步原语、TLS、原子操作 | 直接复用标准 C/POSIX 实现，受 Bionic 行为与应用文件权限约束 |
| `epoll`、`inotify`、套接字、匿名页与文件映射 | 复用 Linux 内核适配，受 Android 应用沙箱与 SELinux 策略约束 |
| 子进程管理（`hx_os_process`） | **API 21 构建**：返回 `HX_OS_NOT_SUPPORTED` 并清除 `HX_OS_CAP_PROCESS` 能力位；**API 28+ 构建**：使用 Bionic `posix_spawn` |
| 带截止时间的线程汇合（`hx_os_thread_join_until`） | 通过 Bionic 公开的 `pthread_gettid_np` 轮询观察内核在线程退出并完成 TLS 清理后清零的 `tid`，就绪后再执行最终 `pthread_join`，支持超时安全重试 |
| 线程池、目录监听器与后台写出器的最终回收 | 统一复用上述带截止时间的原生线程汇合机制，确保线程局部存储（TLS）析构真正完成后才视作线程退役 |
| 本机进程间通信（IPC） | 支持回环 TCP、UDP 与 KCP；`AF_UNIX` 路径套接字受应用 UID 与 SELinux 限制，无权限时原样返回系统权限错误；共享内存需使用基于授权目录的 `create_at` / `open_at` |
| 严格 CPU 核心亲和性绑定 | 当前适配器固定返回 `HX_OS_NOT_SUPPORTED` |
| 标准路径查询（`home`/`config_home`/`data_home`/`program_data`/`temp`） | 固定返回 `HX_OS_NOT_SUPPORTED`（Android 无统一桌面环境变量目录，必须由宿主通过 `Context` 获取应用沙箱路径后显式传入） |
| 可执行文件与当前模块路径（`executable` / `module`） | 通过 `/proc/self/exe` 与 `dladdr` 查询，受限情况原样返回系统错误 |
| 系统版本查询（`hx_os_sys_version_string`） | 返回底层 Linux 内核与 `uname` 版本信息（非 Android Java SDK API 级别） |
| 桌面专属扩展（Windows 注册表、桌面电源抑制等） | 返回 `HX_OS_NOT_SUPPORTED`，对应能力位保持清零 |
| `/proc`、CPU 拓扑与网络接口状态查询 | 受 Android 版本与 SELinux 限制时明确返回权限错误或 `HX_OS_NOT_SUPPORTED`，绝不伪造指标或谎报成功 |

> [!IMPORTANT]
> **编译最低 API 级别（`ANDROID_PLATFORM`）决定子进程能力**
> 如果使用 `-DANDROID_PLATFORM=android-21` 编译二进制文件，由于 API 21 的 Bionic 尚未提供完整的 `posix_spawn` 支持，子进程功能在编译期即被关闭（即使将该二进制放到 Android 10+ 设备上运行也依然返回 `HX_OS_NOT_SUPPORTED`）。若业务需要使用子进程功能，请将最低编译目标设为 `android-28` 或更高版本。

## 4. 部署前检查与网络 DNS 说明

- **实机与沙箱环境验证**：在部署上线前，请确认目标设备的 CPU ABI、应用沙箱目录权限、网络权限（`android.permission.INTERNET`）以及 SELinux 策略。请注意，桌面 Linux、WSL 或模拟器上的 ARM 指令转译运行不能替代真实 ARM 物理设备与真实 APK 沙箱权限下的集成测试。其他跨平台基线要求见[平台支持](PLATFORMS.zh-CN.md)。
- **Android 下的 DNS 解析与可取消 HTTP 请求**：
  - 网络模块在 Android 上复用相同的非阻塞套接字与 `epoll` 实现，但本库**不会**在底层自动绑定 Android Java 层的 `Network` 句柄，也不会绕过系统 VPN 或沙箱防火墙规则（网卡变更监听订阅在受限的 Android 版本上可能返回原生权限错误）。
  - 当使用 `c-ares` 执行显式异步 DNS 解析、TCP 拨号或 TLS 握手时，由于 Android 应用沙箱内通常没有传统的 `/etc/resolv.conf`，如果运行环境无法直接读取系统解析器配置，请通过配置接口显式传入上游 DNS 服务器地址。
  - 对于 HTTP 协程服务，默认 SDK 使用的系统阻塞式解析器在遇到网络黑洞时可能导致关闭等待超时。如果需要支持连 DNS 无响应时也能及时取消，请选用开启了 `HX_DEPS_CURL_CARES=ON` 的依赖 SDK，并在提交请求前调用 `hx_os_coro_http_use_cancellable_dns`（同时确保应用提供了可用的 DNS 配置）。详细用法请参阅[网络 I/O](NETWORK_IO.zh-CN.md) 与 [HTTP 传输](HTTP_FILES.zh-CN.md)。
