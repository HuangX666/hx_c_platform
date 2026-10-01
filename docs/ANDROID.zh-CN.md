# Android 测试基准

[English](ANDROID.md) · [返回首页](../README.zh-CN.md)

确认基线为 **Android 5.0 / API 21**，覆盖 **arm64-v8a、armeabi-v7a**，
补充 API 28 构建。Android 是独立目标，不能把桌面 Linux、WSL
或 glibc/QEMU 结果视为 Android 运行证据。

## 构建与运行

使用现有 NDK，不下载或修改 SDK/第三方源码：

~~~text
python tools/verify_android.py --ndk /absolute/path/to/ndk/28.2.13676358
~~~

默认构建两个 ABI × 两个 API，包含库、示例、基准和全部测试可执行文件，
以及公共头 C99/C++11 独立编译。需要显式指定 Ninja 时追加 --ninja。
Windows 本地可用 NDK 路径 E:/Android/Sdk/ndk/28.2.13676358，
Ninja 路径 E:/Android/Sdk/cmake/3.22.1/bin/ninja.exe。

~~~text
python tools/verify_android.py --ndk /path/to/ndk --api 21 --minimal
python tools/verify_android.py --ndk /path/to/ndk --api 21 --static
~~~

--minimal 使用现有 cmake/minimal.cmake。默认共享库，--static 选择静态库。
--build-root 隔离记录。脚本默认只交叉编译，不执行 ARM 文件或自动选择设备。

~~~text
adb devices -l
python tools/verify_android.py --ndk /path/to/ndk --api 21 --abi arm64-v8a --serial DEVICE_SERIAL
~~~

可用 --adb 指定 adb 绝对路径。脚本核对目标 ABI/API，在新建的
/data/local/tmp/hx_c_platform-随机值 目录上传本次构建的文件，
仅为本次进程设置 LD_LIBRARY_PATH，运行以下 8 组：

| 测试 | 重点 |
| --- | --- |
| android_baseline | 平台/API、UTF-8 文件与 I/O、线程/事件、TLS 析构、有限 join 不支持 |
| string_tools | Unicode、NUL、别名、失败保持、类型化容器 |
| lexer | token 位置、代理对、错误保持、输入/数量上限 |
| wide_wait | 独立大整数参考值、舍入、溢出、单调等待、周期回调 |
| catalog_filename | 翻译覆盖、分配失败、BOM/编码、文件名 |
| procedure | 在途回调、关闭重试、自等待、分配器回入、热路径无分配 |
| atomic | 32/64 位原子、发布关系、并发增量 |
| cpp_utilities | C++11 字符串、分词、翻译、过程及池封装 |

另运行 hx_os_portable_baseline，记录原子、ASCII、宽位換算、命名过程、
精细等待观察值，没有固定时间阈值。ADB 只负责部署和控制。
记录 NDK、源码版本及是否有未提交修改、ABI/API、设备型号/指纹/API、
模拟器标记和每项输出。清理只移除确切上传文件，不递归删除目录；
失败夹保留排查。ADB 超时可能留下目标进程，保留目录并报告，不强杀。
性能采样需记录负载、温度、调频状态并重复多轮。

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

模块位表示编译了功能族，不保证所有平台扩展可用。API 21 构建在新设备上
也不会自动获得进程能力，需要更高最低 API 的构建。没有 JNI/Java 依赖、
APK、Manifest 权限、资产系统或 UI 自动化，这些由消费者处理。
独立验证程序使用 c++_static，消费者沿用自己 NDK 工程的 STL 选择。

## 当前证据

2026-10-01，Windows 宿主，NDK r28c / Clang 19.0.1：

| 配置 | 交叉编译 | 目标运行 |
| --- | --- | --- |
| API 21 arm64-v8a，共享、默认模块 | 通过 | 待设备 |
| API 21 armeabi-v7a，共享、默认模块 | 通过 | 待设备 |
| API 28 arm64-v8a，共享、默认模块 | 通过 | 待设备 |
| API 28 armeabi-v7a，共享、默认模块 | 通过 | 待设备 |

最小/静态配置及宿主验证见[验证记录](VALIDATION.zh-CN.md)。
本机 adb 无连接设备，没有 Android 运行通过率、性能或模拟器/真机通过声明。
CI 已加入固定 NDK 矩阵和 API 21 最小配置，尚无远程执行证据。
后续补 API 21 与较新系统的 32/64 位、应用沙箱、权限和基准运行记录。
