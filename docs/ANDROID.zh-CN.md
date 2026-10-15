# Android 构建与部署

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

确认基线为 **Android 5.0 / API 21**，覆盖 **arm64-v8a、armeabi-v7a**，补充 API 28 构建。Android 是独立目标，不能把桌面 Linux、WSL 或 glibc/QEMU 结果视为 Android 运行证据。

## 1. 编译库与示例

以下命令在 Linux／WSL shell 执行，使用已有 NDK，不修改 SDK 源码。先构建最小功能和 `hello` 示例；需要 TLS／压缩时，按[依赖教程](DEPENDENCIES.zh-CN.md#6-linux-arm-与-android-交叉构建)准备匹配 ABI/API 的 SDK，再开启相关模块。

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

`ANDROID_NDK_ROOT` 是 NDK 目录，`DEVICE_SERIAL` 换成列表中的目标设备。ARMv7 将 ABI 改为 `armeabi-v7a`，x86_64 模拟器使用 `x86_64`；每种 ABI 复用各自的构建目录。构建最低 API 与设备实际 Android 版本不是一回事。库和示例不依赖维护者私有回归文件。

动态构建还需把 `libhx_c_platform.so` 和所用依赖运行库放到设备同一目录，运行时设置该进程的 `LD_LIBRARY_PATH`。动态 OpenSSL 的实际文件见构建目录中的 `hx_openssl_runtime_Release.txt`，部署运行时 SONAME 文件及其目标，不能把宿主 Windows/Linux 库传到 Android。项目 Android SDK 使用 `libssl-hx.so`／`libcrypto-hx.so`，避免与系统 BoringSSL 名称冲突。ABI、API 和依赖版本都要匹配；APK 的 native library 打包与目录权限由宿主应用管理。

## 2. 共享运行库与应用接入

上面的入门示例明确静态链接精简库，便于直接用 adb 运行。需要完整模块时，为同一个 ABI/API 准备依赖 SDK，再启用相应模块。共享构建的可执行文件和所需 `.so` 必须一起部署，库文件来自该构建的安装包；命令行运行时把对应目录加入 `LD_LIBRARY_PATH`。不要混用不同 ABI 或系统自带的同名加密库。

APK 应用由宿主构建系统打包原生库并设置权限。本库不生成 APK、不替业务修改 Manifest，也不隐式获取应用目录。把通过 Context／NativeActivity 取得的可写路径显式传给文件、队列和共享内存等入口。

## 3. 能力边界

| 能力 | 当前行为 |
| --- | --- |
| 文件、UTF-8、线程、同步、TLS、原子 | 共用 C/POSIX，实现受 Bionic 和文件权限约束 |
| epoll、inotify、socket、页/映射 | 共用 Linux 适配，受应用沙箱约束 |
| 子进程 | API 21 构建返回 NOT_SUPPORTED、清除 PROCESS 位；API 28 使用 posix_spawn |
| 有限期限线程 join | 使用 Bionic 的公开 pthread_gettid_np 观察内核清零的 TID，再执行最终 join；包含 TLS 清理，支持超时重试 |
| 池/监听器/IPC 等最终回收 | 使用上述有限 join；不把线程函数返回冒充完整 TLS 清理 |
| 本机 IPC | TCP、普通 UDP、KCP；Unix 路径受 UID 和 SELinux 限制，拒绝时返回原生权限错误 |
| 严格亲和性 | 当前适配返回 NOT_SUPPORTED |
| HOME/配置/数据/共享数据/临时目录 | 返回 NOT_SUPPORTED；宿主通过 Context/NativeActivity 取得应用路径后传入 |
| 可执行文件/模块位置 | /proc/self/exe、dladdr，原生限制明确报告 |
| 系统版本 | Android kernel 与 uname 内核版本，不是 Android API |
| 未实现的注册表/电源扩展 | NOT_SUPPORTED，能力位遵循已有平台规则 |
| /proc、CPU 拓扑、进程/网络访问 | 明确报告权限/原生失败，不伪造指标或成功 |

模块位表示编译了功能族，不保证所有平台扩展可用。API 21 构建在新设备上也不会自动获得进程能力，需要更高最低 API 的构建。没有 JNI/Java 依赖、 APK、Manifest 权限、资产系统或 UI 自动化，这些由消费者处理。C++ 消费者沿用自己 NDK 工程的 STL 选择；使用 c++_shared 时，部署匹配的 libc++_shared.so。

## 4. 部署前检查

确认设备支持目标 ABI，检查应用 UID、SELinux、目录及设备访问权限。API 21 是编译最低版本，不等于已在 Android 5.0 系统运行；模拟器的 ARM 转译也不等同于 ARM 实机。Android 5.0、ARMv7 运行及 APK 沙箱需要各自的目标验收。

公开示例可以独立运行。回归源码、详细日志和历史报告由维护者本地保留，不随仓库及 SDK 发布。其他平台边界见[平台支持](PLATFORMS.zh-CN.md)。

新增网络入口使用相同 socket／epoll 实现。显式 DNS、TCP 拨号和 TLS 启动可选择 c-ares；应用环境不能提供系统解析配置时，需传入 DNS 服务器。本库不绑定 Android Network 句柄，也不绕过 VPN／沙盒规则。网卡监听订阅可能返回原生权限错误。HTTP 协程服务使用自己的有界线程池，系统解析器阻塞仍可能造成关闭超时。用法见[网络 I/O](NETWORK_IO.zh-CN.md)和[HTTP 传输](HTTP_FILES.zh-CN.md)。
