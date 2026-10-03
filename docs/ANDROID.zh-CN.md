# Android 测试基准

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

确认基线为 **Android 5.0 / API 21**，覆盖 **arm64-v8a、armeabi-v7a**，补充 API 28 构建。Android 是独立目标，不能把桌面 Linux、WSL 或 glibc/QEMU 结果视为 Android 运行证据。

## 构建与运行

使用现有 NDK，不下载或修改 SDK/第三方源码：

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

可用 --adb 指定 adb 绝对路径。脚本核对目标 ABI/API，在新建的 /data/local/tmp/hx_c_platform-随机值 目录上传本次构建的文件，仅为本次进程设置 LD_LIBRARY_PATH，运行以下 14 组：

| 测试 | 重点 |
| --- | --- |
| android_baseline | 平台/API、UTF-8 文件与 I/O、线程/事件、TLS 析构、有限 join 不支持 |
| string_tools | Unicode、NUL、别名、失败保持、类型化容器 |
| lexer | token 位置、代理对、错误保持、输入/数量上限 |
| wide_wait | 独立大整数参考值、舍入、溢出、单调等待、周期回调 |
| catalog_filename | 翻译覆盖、分配失败、BOM/编码、文件名 |
| procedure | 在途回调、关闭重试、自等待、分配器回入、热路径无分配 |
| atomic | 32/64 位原子、发布关系、并发增量 |
| cpp_utilities | 关闭异常的 C++11 Result、字符串、词法、过程与分配回滚 |
| cpp_pool | 类型化结果、取消、容量、准备/关闭竞争与清理 |
| cpp_extensions | 设置快照、信号结果、捕获退役、位流与平台扩展 |
| convenience | 原地字符串、并发更新期间持有设置字符串、连续设置失败、翻译借用视图和有界目录遍历 |
| variant | 精确类型、UTF-8/NUL、自定义复制/清理、对齐、所有权/context 回收和容器失败路径 |
| cpp_variant | 无异常 C++11 set/get/emplace/clone/visit、类型列表、只构造对象和分配失败 |
| file_search | 递归流式/首次命中查询、Unicode、限额、链接、分配回收和模块关闭 |

另运行 hx_os_portable_baseline，记录原子、ASCII、宽位換算、命名过程、精细等待观察值，没有固定时间阈值。ADB 只负责部署和控制。记录 NDK、源码版本及是否有未提交修改、ABI/API、设备型号/指纹/API、模拟器标记和每项输出。清理只移除确切上传文件，不递归删除目录；失败夹保留排查。ADB 超时可能留下目标进程，保留目录并报告，不强杀。性能采样需记录负载、温度、调频状态并重复多轮。

报告区分编译最低 API、实际系统 API、原生机器架构和翻译执行，逐程序记录结果。失败不会继续写成“未运行”。验证用共享库程序带 $ORIGIN RUNPATH，支持新版 Android 从程序所在目录加载依赖；旧系统仍使用 LD_LIBRARY_PATH。这些链接选项仅作用于测试/示例/基准程序，不传给消费者或修改系统库路径。

## 能力边界

| 能力 | 当前行为 |
| --- | --- |
| 文件、UTF-8、线程、同步、TLS、原子 | 共用 C/POSIX，实现受 Bionic 和文件权限约束 |
| epoll、inotify、socket、页/映射 | 共用 Linux 适配，受应用沙箱约束 |
| 子进程 | API 21 构建返回 NOT_SUPPORTED、清除 PROCESS 位；API 28 使用 posix_spawn |
| 有限期限线程 join | Bionic 缺少所需 tryjoin，返回 NOT_SUPPORTED、保留线程；无限 join 包含 TLS 清理 |
| 池/监听器等最终回收 | 同受有限 join 限制；不把线程退出前的标记冒充完整 TLS 清理 |
| 严格亲和性 | 当前适配返回 NOT_SUPPORTED |
| HOME/配置/数据/共享数据/临时目录 | 返回 NOT_SUPPORTED；宿主通过 Context/NativeActivity 取得应用路径后传入 |
| 可执行文件/模块位置 | /proc/self/exe、dladdr，原生限制明确报告 |
| 系统版本 | Android kernel 与 uname 内核版本，不是 Android API |
| 未实现的注册表/电源扩展 | NOT_SUPPORTED，能力位遵循已有平台规则 |
| /proc、CPU 拓扑、进程/网络访问 | 明确报告权限/原生失败，不伪造指标或成功 |

模块位表示编译了功能族，不保证所有平台扩展可用。API 21 构建在新设备上也不会自动获得进程能力，需要更高最低 API 的构建。没有 JNI/Java 依赖、 APK、Manifest 权限、资产系统或 UI 自动化，这些由消费者处理。独立验证程序使用 c++_static，消费者沿用自己 NDK 工程的 STL 选择。

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
