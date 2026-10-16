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

可用 --adb 指定 adb 绝对路径。脚本核对目标 ABI/API，在新建的 /data/local/tmp/hx_c_platform-随机值 目录上传本次构建的文件，仅为本次进程设置 LD_LIBRARY_PATH，运行以下 31 组：

| 测试 | 重点 |
| --- | --- |
| android_baseline | 平台/API、UTF-8 文件与 I/O、线程/事件、TLS 析构、有限 join |
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
| containers / stl | 红黑树/AVL/重复键不变量、遍历、排序、筛选、splice 和集合运算 |
| coroutine / coroutine_io | 有栈/无栈调度、浮点状态、channel/select、准备失败回滚、关闭重试、TCP/DNS/管道和取消 |
| cpp_coroutine / cpp_coroutine20 | 无异常 C++11 go、C++20 帧/局部变量、等待和取消后的析构 |
| coroutine_sync / cpp_go11 / cpp_go20 | 自动持有任务、错误汇总、拥有值 channel、协程锁、条件变量取消和局部值清理 |
| format / cpp_format | UTF-8 类型化格式化、自定义值、失败时保持输出、分配释放 |
| kcp / ipc | 丢包重传、完整消息、双向请求、多线程、重启、背压和关闭；本地路径权限拒绝明确记录 |
| lifecycle | 分配失败、TLS 析构阻塞期间 join 超时，释放后重试成功 |
| config_tree / codec / cpp_codec | 深层混合文档、七种格式写出、结构体映射、资源清理和无异常 C++11 |

另运行 hx_os_portable_baseline，记录原子、ASCII、宽位換算、命名过程、精细等待观察值，没有固定时间阈值。ADB 只负责部署和控制。记录 NDK、源码版本及是否有未提交修改、ABI/API、设备型号/指纹/API、模拟器标记和每项输出。清理只移除确切上传文件，不递归删除目录；失败夹保留排查。ADB 超时可能留下目标进程，保留目录并报告，不强杀。性能采样需记录负载、温度、调频状态并重复多轮。

报告区分编译最低 API、实际系统 API、原生机器架构和翻译执行，逐程序记录结果。失败不会继续写成“未运行”。验证用共享库程序带 $ORIGIN RUNPATH，支持新版 Android 从程序所在目录加载依赖；旧系统仍使用 LD_LIBRARY_PATH。这些链接选项仅作用于测试/示例/基准程序，不传给消费者或修改系统库路径。

只复核通信时，可在运行命令后追加 `--suite ipc --suite kcp --suite lifecycle`。不指定 `--suite` 时运行全部 31 组；可移植基准仍会运行，报告保存本次选择的套件和明确跳过的子项。

只验证序列化时，追加 `--suite config_tree --suite codec --suite cpp_codec`。用法与示例见[序列化教程](SERIALIZATION.zh-CN.md)。

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

## 运行限制

最低 API 21 是构建目标，不能证明程序已经在 Android 5.0 上运行。API 21 系统运行、ARMv7 运行、实体 ARM 硬件和 APK 沙箱行为仍需要对应目标环境验证。模拟器转译结果不能作为原生 ARM 性能结论。

Unix 域套接字需要符合 UID 和 SELinux 权限要求。即使其它通信方式可用，shell 下的运行仍可能返回 `EACCES`；应在实际应用环境中验证 Unix 套接字。

线程函数返回不代表 TLS 析构已完成。有时限 join 包含线程退出清理，超时后保留所持有的线程供重试；库不会把有限等待暗中改成无限等待。

共享库示例／测试程序在支持时使用相对可执行文件的 RUNPATH。RUNPATH 需要 API 24 或更高版本，较旧的原生系统使用 `LD_LIBRARY_PATH`，详见 [Android 链接器说明](https://android.googlesource.com/platform/bionic/+/refs/heads/android15-tests-release/android-changes-for-ndk-developers.md)。

可选的 C 自动语法生成器在构建宿主上运行。核心库交叉编译成功，不代表生成的应用源码已经适配所有 Android sysroot；这些源码仍需使用应用的目标工具链编译。
