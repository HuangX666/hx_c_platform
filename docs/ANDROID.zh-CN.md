# Android 测试基准

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

确认基线为 **Android 5.0 / API 21**，覆盖 **arm64-v8a、armeabi-v7a**，补充 API 28 构建。Android 是独立目标，不能把桌面 Linux、WSL 或 glibc/QEMU 结果视为 Android 运行证据。

新运行默认复用 `out/android/`，也可通过 `--build-root` 复用已有目录。下文历史路径表示当时的运行位置；本地构建清理后，保留的 JSON 报告归入 `out/records/<原构建目录名>/`，旧二进制和日志不长期保留。

## 构建与运行

使用现有 NDK，脚本不下载或修改 SDK。可选依赖准备遵循所选来源，不改写第三方源码：

~~~text
python tools/verify_android.py --ndk /absolute/path/to/ndk/28.2.13676358
~~~

默认构建两个 ABI × 两个 API，包含库、示例、基准和全部测试可执行文件，以及公共头 C99/C++11 独立编译。需要显式指定 Ninja 时追加 --ninja。 Windows 本地可用 NDK 路径 E:/Android/Sdk/ndk/28.2.13676358， Ninja 路径 E:/Android/Sdk/cmake/3.22.1/bin/ninja.exe。

~~~text
python tools/verify_android.py --ndk /path/to/ndk --api 21 --minimal
python tools/verify_android.py --ndk /path/to/ndk --api 21 --static
~~~

--minimal 使用现有 cmake/minimal.cmake。默认共享库，--static 选择静态库。 --build-root 隔离记录。脚本默认只交叉编译，不执行 ARM 文件或自动选择设备。

adb 列表为空时，先检查未启动的 AVD。本机已有 Qt_Android_34：

~~~text
emulator -list-avds
emulator -avd Qt_Android_34 -port 5580 -read-only -no-snapshot
~~~

等待启动完成后使用 emulator-5580。--read-only 保留原 AVD 用户数据， --no-snapshot 不加载或保存快照。自动验证可加 -no-window -no-audio。本机 AVD 为 x86_64；新增 --abi x86_64 作为模拟器补充配置，ARM 默认值不变。

~~~text
adb devices -l
python tools/verify_android.py --ndk /path/to/ndk --api 21 --abi arm64-v8a --serial DEVICE_SERIAL
~~~

可用 `--adb` 指定 adb 绝对路径。脚本检查明确选中的 ABI/API，创建隔离的 `/data/local/tmp/hx_c_platform-随机值` 目录，并通过 `ctest --show-only=json-v1` 自动取得**全部已配置原生测试和示例**，不再手工维护套件列表。参数、测试文件、工作目录、环境变量、输出要求、跳过码和超时均来自 CTest。宿主 Python／工具命令另列为 `host_tests`，不计入设备运行。

因此，新增加密／TLS／压缩、流和后台文件写出、字节队列、INI／备份／设置、信号／参数、解析／预处理、profiler 测试，都会与已有平台、容器、协程和序列化测试一起运行。关闭模块的配置验证明确的不支持行为。指定 7-Zip 运行库后增加 `archive_7zip`，平台专有能力的缺失单独说明。

启用完整可选依赖时，先按[依赖构建](DEPENDENCIES.zh-CN.md#6-linux-arm-与-android-交叉构建)准备对应 ABI/API 的 SDK，再执行：

```sh
python tools/verify_android.py --ndk /path/to/ndk --abi arm64-v8a --api 21 \
  --dependency-prefix /path/to/build-deps-android-arm64/prefix \
  --parsers --iconv --sevenzip-library /path/to/target/runtime/7z.so \
  --serial DEVICE_SERIAL --build-root out/android
```

不需要独立原生 7-Zip 后端时可省略 `--sevenzip-library`。省略 `--dependency-prefix` 表示明确关闭加密／压缩。一个 prefix 必须匹配显式指定的一种 ABI/API，不能套用到默认两种 ARM ABI。`--parsers` 开启五种可选解析器，`--iconv` 开启固定版本 GNU 转换器，`--provider FETCHCONTENT` 选择替代源码来源。

脚本读取生成的 `hx_openssl_runtime_Release.txt`，部署实际选中的 **libssl 和 libcrypto** 及所需版本名称，与测试程序和核心库放在一起。运行库缺失时部署失败。程序和核心同时使用 `$ORIGIN` RUNPATH 及进程局部库路径，ARM 转译的间接依赖也需要显式路径。只部署独立 SONAME 的运行库，普通 `libssl.so`／`libcrypto.so` 链接别名不能覆盖 x86_64 转译启动器使用的系统 BoringSSL。部署效果需要设备运行验证，不能只以链接成功判断。

准备好的 Android SDK 通过上游支持的独立配置使用 `libssl-hx.so`／`libcrypto-hx.so` SONAME，避免转译客体误绑定已经加载的同名系统 BoringSSL。上传的文件由运行库清单及其符号链接真实目标确定；与系统 BoringSSL 同名的链接别名留在 SDK 内。

另运行 hx_os_portable_baseline，记录原子、ASCII、宽位換算、命名过程、精细等待观察值，没有固定时间阈值。ADB 只负责部署和控制。记录 NDK、源码版本及是否有未提交修改、ABI/API、设备型号/指纹/API、模拟器标记和每项输出。清理只移除确切上传文件，不递归删除目录；失败夹保留排查。ADB 超时可能留下目标进程，保留目录并报告，不强杀。性能采样需记录负载、温度、调频状态并重复多轮。

报告区分编译最低 API、实际系统 API、原生机器架构和翻译执行，逐程序记录结果。失败不会继续写成“未运行”。验证用共享库程序带 $ORIGIN RUNPATH，支持新版 Android 从程序所在目录加载依赖；旧系统仍使用 LD_LIBRARY_PATH。验证程序及使用动态 OpenSSL 的 Android 核心带相对构建 RUNPATH；安装后共享库保持既有相对安装 RUNPATH，不修改系统库路径。

只复核通信时，可在运行命令后追加 `--suite ipc --suite kcp --suite lifecycle`。不指定 `--suite` 时运行全部已配置原生测试和示例。名称使用 CTest 测试名，不是可执行文件名；`--timeout-scale` 只缩放设备测试的超时上限，不改变库的截止时间。可移植基准仍会运行，报告保存本次选择的套件和明确跳过的子项。

只验证序列化时，追加 `--suite config_tree --suite codec --suite cpp_codec`。用法与示例见[序列化教程](SERIALIZATION.zh-CN.md)。

## 当前验收（2026-10-07）

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
