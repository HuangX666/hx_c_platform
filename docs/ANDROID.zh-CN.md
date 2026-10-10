# Android 测试基准

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

确认基线为 **Android 5.0 / API 21**，覆盖 **arm64-v8a、armeabi-v7a**，补充 API 28 构建。Android 是独立目标，不能把桌面 Linux、WSL 或 glibc/QEMU 结果视为 Android 运行证据。

## 编译库与示例

以下命令在 Linux／WSL shell 执行，使用已有 NDK，不修改 SDK 源码。先构建最小功能和 `hello` 示例；需要 TLS／压缩时，按[依赖教程](DEPENDENCIES.zh-CN.md#6-交叉编译-linux-arm-和-android)准备匹配 ABI/API 的 SDK，再开启相关模块。

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

`ANDROID_NDK_ROOT` 是 NDK 目录，`DEVICE_SERIAL` 换成列表中的目标设备。ARMv7 将 ABI 改为 `armeabi-v7a`，x86_64 模拟器使用 `x86_64`；每种 ABI 复用各自的构建目录。构建最低 API 与设备实际 Android 版本不是一回事。库和示例不依赖维护者私有回归文件。

动态构建还需把 `libhx_c_platform.so` 和所用依赖运行库放到设备同一目录，运行时设置该进程的 `LD_LIBRARY_PATH`。动态 OpenSSL 的实际文件见构建目录中的 `hx_openssl_runtime_Release.txt`，部署运行时 SONAME 文件及其目标，不能把宿主 Windows/Linux 库传到 Android。项目 Android SDK 使用 `libssl-hx.so`／`libcrypto-hx.so`，避免与系统 BoringSSL 名称冲突。ABI、API 和依赖版本都要匹配；APK 的 native library 打包与目录权限由宿主应用管理。

下方保留带日期的历史验收结果与平台限制。历史自动回归工具、日志和用例仅保存在维护者本机，不属于公开源码的运行步骤。

## 历史验收（2026-10-07）

以下均在同一台 API 34 x86_64 `Qt_Android_34` 模拟器运行。完整配置开启 Crypto、TLS、Archive、可选 7-Zip、五种原生解析后端及 GNU iconv；目标 SDK 包含对应架构的 OpenSSL、zlib、XZ、Zstd。表中数量不含另外执行的可移植基准，每个已运行配置的基准均通过。

| 构建配置 | 执行方式 | 原生 CTest 测试／示例 |
| --- | --- | --- |
| x86_64 API 21 完整共享 | 原生架构 | 159 项通过，3 项明确跳过 |
| arm64-v8a API 21 完整共享 | native bridge 转译 | 159 项通过，3 项明确跳过 |
| x86_64 API 28 完整共享 | 原生架构 | 160 项通过，2 项明确跳过 |
| x86_64 API 21 最小共享 | 原生架构 | 108 项通过 |
| armeabi-v7a API 21 完整共享 | 模拟器不支持该 ABI | 编译通过，运行明确跳过 |

API 21 的跳过项是 SELinux 拒绝的路径 Unix socket、缺少应用上下文时的配置目录，以及进程捕获示例。API 28 已启用并通过进程创建／捕获，保留前两项跳过。IPC 和 C++ 平台套件单独记录 Unix socket 子项后继续验证其他功能，没有修改安全设置。报告分别记录构建下限、实际系统 API 和运行架构。

报告位于 `build-platform-android-x64/report.json`、`build-platform-android-arm64/report.json`、`build-platform-android-api28/report.json`、`build-platform-android-armv7/report.json`、`build-platform-android-minimal/report-minimal.json`。各 ABI 目录保留完整 `configure.log`、`build.log`、`run-*.log` 和运行库上传日志。最终功能标志从生成配置头读取，包含 CRYPTO 与 OPENSSL 的组合条件。宿主部署回归命令为 `python -B tests/test_android_runner.py`，覆盖缺失文件、位于源码目录内的 SDK、原生测试发现和 SONAME 别名。

Android 5.0 系统运行、Android ARMv7 运行、APK 沙箱及实体 ARM 硬件仍单独标记未验证。本轮按用户确认使用现有模拟器／QEMU 验收；Linux ARM QEMU 结果见[验证记录](VALIDATION.zh-CN.md)，不能当作 Android 结果。远端 CI 继续仅保留手动入口，本次未启动。

## 扩大运行测试后确认的行为

启用 `_GNU_SOURCE` 时，API 23+ Bionic 使用 GNU 版 `strerror_r` 返回类型，错误适配已与 API 21 分开处理。API 28 使用原生 `posix_spawn`：子进程创建后，部分 exec 前失败可能以退出码 127 报告。测试明确检查这个 POSIX 结果，不把它伪装成立即返回的原生 errno。POSIX 示例直接使用 main 的 argv，避免 procfs 中的 ARM 转译启动器参数替代业务参数。

Linux／Android 压缩输出目录的祖先检查改用只要求目录搜索权限的描述符，继续保留 `O_NOFOLLOW`，不再多余要求列出 `/data` 等祖先目录；包含仅有执行／搜索权限的父目录回归。测试临时文件放在脚本持有的可写目录，不写死桌面 `/tmp`。应用目录查询不可用、API 28 以下的进程限制，以及 SELinux 拒绝路径 Unix socket，均明确报告。

## 序列化补测（2026-10-04）

提交 `8de8da1`，API 21 编译的 x86_64 与 arm64-v8a 完整共享配置在 Qt_Android_34 上各通过 `config_tree`、`codec`、`cpp_codec` 三个套件，并各完成一次可移植基准。实际系统为 Android 14 / API 34，原生机器 x86_64，ARM64 通过 libndk_translation 执行。模拟器以只读、不保存快照、无窗口方式临时启动，完成后关闭，未清空或保存用户数据。

两份运行报告为 `build-android-emulator/report.json` 和 `build-android/report.json`；各 ABI 构建目录保留 `run-hx_os_test_config_tree.log`、`run-hx_os_test_codec.log`、`run-hx_os_test_cpp_codec.log`。ARMv7 交叉编译通过，但该模拟器 ABI 列表不含 ARMv7，仍待目标运行。API 21 系统、ARM 原生硬件、APK 沙箱运行与性能验收也仍待验证；本次没有重跑其余 28 个套件。

## 能力边界

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

模块位表示编译了功能族，不保证所有平台扩展可用。API 21 构建在新设备上也不会自动获得进程能力，需要更高最低 API 的构建。没有 JNI/Java 依赖、 APK、Manifest 权限、资产系统或 UI 自动化，这些由消费者处理。独立验证程序使用 c++_static，消费者沿用自己 NDK 工程的 STL 选择。

## IPC 与有时限回收验证（2026-10-03）

API 21 ARM64 共享配置在现有 API 34 x86_64 模拟器通过 ARM 转译执行 28 组测试与可移植基准。IPC 的 TCP、普通 UDP、KCP 覆盖完整消息、双向请求、并发回复、重启、限额及关闭。所选模拟器的 SELinux 拒绝 shell 用户创建路径型 Unix socket（`sock_file create`，EACCES）；此子项明确跳过，报告 `test_notes` 和运行日志保留原因，没有降低系统安全设置，也没有写成 Unix socket 已运行通过。需要在有相应权限的 APK/目标环境验证本地路径传输。

Android 有时限 join 适配依据 Bionic 的公开函数和线程退出实现，等待内核清零 TID 后才回收未分离的线程；[Bionic join 实现](https://android.googlesource.com/platform/bionic/+/master/libc/bionic/pthread_join.cpp) 也在最终回收前等待该清零。`lifecycle` 特别覆盖用户函数已返回但 TLS 清理尚未结束的情形。最低 API 21 的编译不等于 Android 5.0 真机运行，ARMv7 仍需目标运行。下方关于有限 join 不支持的文字是历史版本记录，由本节取代当前行为。

## 容器与协程验证（2026-10-03，历史记录）

NDK r28c 为 arm64-v8a、armeabi-v7a、x86_64 构建最低 API 21 的完整共享库，包含自研有栈适配、C99 状态机、C++11 go 和可选 C++20 层。API 34 的 Qt_Android_34 模拟器上，原生 x86_64 与转译 ARM64 各通过二十个套件和可移植基准。模拟器 ABI 列表不含 ARMv7，因此明确跳过其运行。报告为 build-android/report.json 与 build-android-emulator/report.json。

新增检查包括 RB/AVL 不变量、重复键、算法、channel/select、取消、prepare/关闭清理、浮点状态、profiler 隔离、TCP/DNS/管道和 C++20 帧回收。运行时保留有限原生 join 的限制：受控测试安排好协作退出后，显式选择无限 final join；库不会把有限关闭暗中变为无限等待。API 21 系统实际运行、ARMv7 运行和实体 ARM 设备仍待验证。较早的最小/静态/API 28 记录属于历史结果，并非此次新增功能的重测，详见[验证及本地测量记录](VALIDATION.zh-CN.md)。

## 递归搜索验证（2026-10-03）

API 21 ARM64/ARMv7 完整和最小共享库四项配置均构建通过，包含 C99/C++11 公共头。ARM64 两种配置各在 API 34 x86_64 模拟器经 native-bridge 翻译通过十四组测试及 portable 基准。ARMv7 因 ABI 不兼容明确跳过运行。file_search 增加深度/宽度、Unicode、首次命中、回调、链接、分配和模块关闭检查，cpp_utilities 也验证现有线程池中的查询。结果不等于 Android 5.0 运行、ARM 真机性能或应用沙箱验证，详见[验证记录](VALIDATION.zh-CN.md)。

## variant 验证（2026-10-03）

variant 阶段的集合为 13 组，增加 variant/cpp_variant；portable 基准增加整数和自定义结构体 set/get 时间观察。API 21 ARM64/ARMv7 完整及最小共享库构建包含该模块、模块关闭测试和公共头 C99/C++11 独立编译。实际运行与待验证目标分别记录在 [VALIDATION.zh-CN.md](VALIDATION.zh-CN.md)。下面的此前记录描述原版本和当时的测试集合。

## 常用接口验证（2026-10-01）

NDK r28c／Clang 19 编译 API 21 的 arm64-v8a、armeabi-v7a，各覆盖完整／最小共享配置，包含 C99／C++11 独立头和直接调用示例。ARM64 两种配置在 Qt_Android_34／emulator-5580（API 34，原生 x86_64，ARM64 原生桥翻译）各通过全部 11 项选定测试，并运行一次性能基准。模拟器只声明 x86_64、arm64-v8a，因此明确跳过 ARMv7 运行。convenience 测试验证并发更新／关闭后的字符串持有、分配故障清理、连续设置语义、Unicode 目录遍历和模块关闭行为。不能由此推导 Android 5.0 运行或 ARM 真机时序通过，下方 API 28／静态／x86_64 配置保留为历史证据。

## Result 迁移验证（2026-10-01）

API 21 的 arm64-v8a、armeabi-v7a 完整/最小共享配置均编译通过。ARM64 两种配置各 10/10 在现有 API 34 x86_64 模拟器经原生桥翻译执行；ARMv7 不在模拟器 ABI 列表中，明确跳过运行。全部 C++ 测试关闭异常，有限 join 按能力报告不支持，受控测试由调用者显式选择最终无限 join。下表保留此前八测试基线，不能将其中其它 API/链接配置视为此次 Result 迁移已重新运行。

## 此前基线记录

2026-10-01，Windows 宿主，NDK r28c / Clang 19.0.1：

| 配置 | 交叉编译 | 目标运行 |
| --- | --- | --- |
| API 21 arm64-v8a，共享、默认模块 | 通过 | Android 14 翻译执行 8/8 |
| API 21 armeabi-v7a，共享、默认模块 | 通过 | 待设备 |
| API 28 arm64-v8a，共享、默认模块 | 通过 | Android 14 翻译执行 8/8 |
| API 28 armeabi-v7a，共享、默认模块 | 通过 | 待设备 |
| API 21 x86_64，共享、默认模块 | 通过 | Android 14 原生 ISA 执行 8/8 |
| API 28 x86_64，共享、默认模块 | 通过 | Android 14 原生 ISA 执行 8/8 |
| API 21 arm64-v8a，静态、默认模块 | 通过 | Android 14 翻译执行 8/8 |
| API 21 arm64-v8a，共享、最小模块 | 通过 | Android 14 翻译执行 8/8 |

最小/静态配置及宿主验证见[验证记录](VALIDATION.zh-CN.md)。已启动现有 Qt_Android_34，序列号 emulator-5580，实际系统为 API 34，原生机器为 x86_64，提供 libndk_translation。上表 6 个已运行配置分别通过 8 项安卓基准测试，另各执行一次性能基准程序。最小配置验证功能关闭语义。这 8 项属于此前安卓基准子集，不是当前集合，也不是整套桌面 CTest。

ARM64 共享程序最初无法通过 LD_LIBRARY_PATH 找到本库；加入程序自身目录的 RUNPATH 后，两组共享配置及最小配置通过。该路径机制在 API 24 起可用，参阅 [Android 动态链接器说明](https://android.googlesource.com/platform/bionic/+/refs/heads/android15-tests-release/android-changes-for-ndk-developers.md)。 ARM64 是翻译执行，性能数据含翻译和虚拟化开销，不代表 ARM 硬件性能。模拟器未提供 ARMv7 ABI，该配置在部署前明确跳过，未伪装运行成功。编译最低 API 21 并不代表在 Android 5.0 系统运行过；旧系统、ARMv7、真实 ARM 设备和应用沙箱权限仍需对应环境验证。

原始报告分别位于 build-android-emulator/report.json、build-android/report.json、 build-android/report-minimal.json、build-android-static/report.json，每个配置目录内 run-*.log 保存程序实际输出。 CI 已加入固定 NDK 矩阵和 API 21 最小配置，尚无远程执行证据。

## 2026-10-03 新增验证范围

C 自动语法生成器只在宿主运行，已单独在 Windows／MSVC 和 WSL／GCC 中验证，包括安装后消费。安卓测试运行不依赖生成器的 C99 格式化核心和无异常 C++ 包装。核心交叉编译成功不等于任意应用源文件已经通过特定 Android sysroot 的生成分析。此次协程／格式化增量的真实 ABI 和系统运行证据见[验证记录](VALIDATION.zh-CN.md)。
