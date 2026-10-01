# hx_c_platform

[![CI 配置][ci-badge]](.github/workflows/build.yml)
[![C99 核心][c-badge]](CMakeLists.txt)
[![C++11 封装][cpp-badge]](docs/CPP_POOL.zh-CN.md)
[![仅 C++ 封装为 header-only][header-badge]](docs/CPP_POOL.zh-CN.md)
[![CMake 3.16+][cmake-badge]](CMakeLists.txt)
[![自有代码许可证待定][license-badge]](THIRD_PARTY_NOTICES.zh-CN.md)

[![Linux GCC 与 Clang][linux-badge]](docs/VALIDATION.zh-CN.md)
[![macOS 待验证][macos-badge]](docs/VALIDATION.zh-CN.md)
[![Windows MSVC][windows-badge]](docs/VALIDATION.zh-CN.md)
[![本地 Windows 82/82、Linux 84/84 项测试通过][tests-badge]](docs/VALIDATION.zh-CN.md)
[![嵌入式 Linux 待目标验证][embedded-badge]](docs/VALIDATION.zh-CN.md)
[![Android API 21 ARM 交叉编译通过][android-badge]](docs/ANDROID.zh-CN.md)

系统信息可通过 C 接口或 `hx::SystemInfo` 直接查询；
进程 CPU 采样使用独立的 `hx::CpuUsage` 对象。
参阅[平台便利功能](docs/PLATFORM_UTILITIES.zh-CN.md)与
[24 批次进度](docs/OBS_COMPLETION.zh-CN.md)。

[English](README.md)

徽章依据截至 **2026-10-01** 的记录：CI 已配置，本项目尚未声明远程 CI 通过。
**只有 C++ 封装是 header-only**，C 核心需要编译为静态库／共享库。
C++11 已验证，尚未验证 C++14/17/20 矩阵。
测试数字表示**Windows 82 项／Linux 84 项 CTest 入口**，不是断言数量；
Linux 额外两项验证原生随机源故障。
Linux 证据来自 WSL：GCC 与 Clang 全量库及 C++11 测试；
Windows 证据来自本地 MSVC x86/x64 构建。
ARMv7/AArch64 glibc 已交叉编译，QEMU 部分运行结果与真实板卡分开记录。
macOS、Win7、板卡和 musl 仍待验证，自有代码分发许可证也尚未确定。
具体配置与目标限制见[验证记录](docs/VALIDATION.zh-CN.md)。

独立的 C99 跨平台基础库，提供纯 C ABI。应用和业务库调用同一套接口，
系统差异由底层适配承担。项目名和 `hx_os_` 前缀仍是暂定名称；
拥有内存的 UTF-8 字符串使用 `hx_string`。
0.1.0 是首版实现，目前还没有宣布 ABI 稳定。

基础库不依赖 hx_c_log、Qt、C++ 或 OBS 运行库。借鉴 OBS 的统一接口、
共用实现、平台源码选择和警告配置方式，保持独立构建、测试和安装。
当前没有执行 hx_c_log 接入。

新增[常用工具](docs/COMMON_UTILITIES.zh-CN.md)：字符串分割/替换、
翻译文件加载、宽位换算、命名过程、文件名、精细等待与分词，
以及类型化 POD 容器和一次完成关闭释放的线程池入口。
[Android API 21 基准](docs/ANDROID.zh-CN.md)覆盖两种 ARM ABI，
已在 Android 14 模拟器完成 6 组配置、每组 8 项基准测试：
x86_64 原生执行及 ARM64 翻译执行。ARMv7、API 21 系统和真机运行仍待验证。

## 从要完成的事情开始

常用操作提供直接的 C 入口，库内部处理原生路径、编码、缓冲增长和资源组合。
只有需要调整上限或行为时才传 options。仅开启模块不会启动后台线程。
24 批实现均已落地；各批记录见[进度表](docs/OBS_COMPLETION.zh-CN.md)，
平台实测范围见[验证记录](docs/VALIDATION.zh-CN.md)。

~~~c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status =
		hx_os_file_read_text("配置.json", &text, &error);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
~~~

这一个读取入口负责打开、读取、校验 UTF-8、去除开头的 UTF-8 BOM 和关闭。
默认上限 16 MiB，失败保持输出字符串不变。不需要先初始化全局运行环境，
也不需要修改全局分配器。

| 想做的事情 | 直接入口／可运行示例 |
| --- | --- |
| 读取或保存文本、原子发布 | hx_os_file_read_text/save_atomic；[text_file.c](examples/text_file.c) |
| 读取环境变量或用户目录 | hx_os_env_get_string / hx_os_path_config_home_string；[special_paths.c](examples/special_paths.c) |
| 解析只读配置、修改并保存设置 | hx_os_config_parse / hx_settings_load_json/save_json；[settings.c](examples/settings.c) |
| 启动程序并同时收集两路输出 | hx_os_process_capture_run；[capture_process.c](examples/capture_process.c) |
| 监听目录 | hx_os_watch_create；[watch_directory.c](examples/watch_directory.c) |
| 周期执行 | hx_os_timer_create；[periodic_timer.c](examples/periodic_timer.c) |
| 提交普通／延迟任务 | C 线程池与 hx::ThreadPool；[thread_pool.cpp](examples/thread_pool.cpp)、[delayed_task.c](examples/delayed_task.c) |
| 通过管道传输数据 | [pipe_transfer.c](examples/pipe_transfer.c)、[duplex_message.c](examples/duplex_message.c) |
| 查询内存／CPU、加载插件 | [system_info.c](examples/system_info.c)、[load_plugin.c](examples/load_plugin.c) |
| 映射文件、生成随机 UUID | [mapped_file.c](examples/mapped_file.c)、[random_uuid.c](examples/random_uuid.c) |
| 发送通知、统计区间耗时 | [signal_notifications.c](examples/signal_notifications.c)、[profile_intervals.c](examples/profile_intervals.c) |
| 在线程间发布共享数据 | [atomic_publication.c](examples/atomic_publication.c)、[原子契约](docs/ATOMICS.zh-CN.md) |

进程、线程、监听和未完成 I/O 保留显式等待／关闭，超时保留句柄及缓冲供重试。
同步资源使用 C++ RAII；析构不会偷偷无限 join 或强杀。每个消费者可使用自己的
不可变分配器 context，不会改动其它消费者的设置。

## 已实现模块

| 头文件 | 职责 |
|---|---|
| hx_os_base.h | 统一状态、原生错误、不可变分配器上下文、时钟 |
| hx_os_atomic.h | 对齐的整数／指针原子操作、CPU 等待提示 |
| hx_os_thread.h | 线程、互斥锁、条件变量、保留通知的事件、线程 TLS |
| hx_os_file.h | 普通文件、UTF-8 路径、目录操作 |
| hx_os_text.h | 严格转换／显式替换策略的字符编码转换 |
| hx_string.h | 持有内存的 UTF-8 字符串、借用视图 |
| hx_os_memory.h | 变长内存池、定长对象池、arena |
| hx_os_pool.h | 有界工作线程／任务、取消、可重试关闭 |
| hx_os_net.h | IPv4/IPv6、TCP/UDP、阻塞 DNS、截止时间等待 |
| hx_os_loop.h | 基于 IOCP、epoll、kqueue 的完成回调 |
| hx_os_process.h | 子进程与有界的双路输出捕获 |
| hx_os_local.h | 路径本地套接字与独立双向文件端点 |
| hx_os_system.h | UTF-8 参数、主机／进程标识与可用 CPU 能力 |
| hx_os_bits.h / hx_os_posix.h | 整数微工具和显式时钟转换 |
| hx_config.h | 只读配置文档与可选第三方解析后端 |
| hx_settings.h | 可变用户值／默认值及不可变快照 |
| hx_os_signal.h / hx_os_profiler.h | 同步通知与区间计时树 |
| hx_os_calendar.h / hx_os_text_lookup.h / hx_os_bitstream.h | 日期、翻译表、有界位流 |
| hx_os_registry.h / hx_os_power.h | 显式可选平台请求 |
| hx_container.h | 有界 C 序列、有序／哈希 map 和 set |
| hx/ThreadPool.hpp | 可选 C++11 任务、返回值、异常和取消令牌 |

核心 Unicode 转换不需要第三方库。传统编码模块显式开启，使用 GNU
libiconv 或检测到的系统 iconv。CMake 不自动下载，也不修改第三方源码。

配置模块支持 JSON、YAML、XML、INI、TOML、ENV、properties。
内置语法范围明确，cJSON/libyaml/Mini-XML/inih/tomlc17 后端分别裁剪。
默认输入编码为 UTF-8，详见
[格式及生命周期契约](docs/CONFIGURATION.zh-CN.md) 与
[共用 C 示例](examples/configuration.c)。

C 容器包含 vector、deque、queue、stack、list、大顶堆，
以及有序／哈希 map/set。哈希实现私有使用未改动的 uthash 2.4.0，
关闭 CONTAINERS 即排除。复制／接管／释放、字节上限、对齐和
hx_string 深拷贝预设见[容器契约](docs/CONTAINERS.zh-CN.md)。

[C++11 线程池封装](docs/CPP_POOL.zh-CN.md) 支持 lambda、
只能移动的参数和结果、异常与协作取消，复用 C 线程池的有界队列及可重试关闭。
任务句柄自动持有／释放引用，线程池本体由宿主显式关闭、销毁。
协程留待后续开发。

日志格式、过滤、日志器绑定、轮转保留、日志异步队列、崩溃策略、
LocalDumps 配置和系统日志目标继续属于消费者。
线程局部存储 TLS 与网络加密 TLS 是不同功能；本库没有引入网络 TLS 库。
动态库加载、按页分配与系统信息查询已实现；原生调用栈取证不在本轮范围。

## 构建

需要 CMake 3.16+、具备 C99 能力的 MSVC/GCC/Clang 和平台 SDK。
测试阶段若存在 C++ 编译器，会额外按 C++11 独立编译公共头。

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cd build
ctest -C Release --output-on-failure
~~~

Windows 使用较新 CMake 时可指定
`-G "Visual Studio 17 2022" -A x64`。
CMake 3.16 可在配置完整的 MSVC 开发者环境中使用 NMake。
测试不需要图形界面或外网服务。

~~~sh
python tools/fetch_iconv.py
cmake -S . -B build-text -DHX_OS_ENABLE_ICONV=ON \
  -DHX_OS_ICONV_SOURCE_DIR=/absolute/path/to/.deps/libiconv-1.18
cmake --build build-text --config Release --parallel 4
~~~

下载脚本需要 Python 3.12+，固定获取 GNU libiconv 1.18 并校验压缩包
SHA256；已存在的源码目录会保留。所有配置生成文件位于构建目录。
开启 ICONV 但不指定源码路径时，使用系统实现；具体编码能力以该实现为准。

编码标识包括 UTF-8、UTF-16LE/BE、UTF-32LE/BE、ASCII、ISO-8859-1、
GB2312、GBK、GB18030:2005／:2022、Big5、Shift-JIS、EUC-JP、
EUC-KR 和 Windows 1252/932/936/949/950。
依赖某种编码前可调用 `hx_os_encoding_supported()` 查询。
转换不修改进程 locale，也不猜测 ANSI 代码页。

| 选项 | 默认 | 作用 |
|---|---|---|
| BUILD_SHARED_LIBS | OFF | 构建共享库 |
| HX_OS_ENABLE_NETWORK | ON | 原生套接字和网络生命周期 |
| HX_OS_ENABLE_LOOP | ON | 异步 I/O、定时器、投递及可选 DNS 接入 |
| HX_OS_ENABLE_THREAD_POOL | ON | 有界线程池；异步 DNS 需要它 |
| HX_OS_ENABLE_MEMORY_POOLS | ON | 内存池、对象池、arena |
| HX_OS_ENABLE_ICONV | OFF | 传统编码后端 |
| HX_OS_ENABLE_CONFIG | ON | 配置文档解析 |
| HX_OS_ENABLE_CONTAINERS | ON | 通用 C 容器，含私有 uthash |
| HX_OS_ENABLE_SYSINFO | ON | 系统信息与进程 CPU 采样 |
| HX_OS_ENABLE_LIBRARY | ON | 动态库加载与拥有所有权的诊断文本 |
| HX_OS_ENABLE_FILE_UTILS | ON | 文件、路径、原子发布与遍历便利操作 |
| HX_OS_ENABLE_ENV | ON | UTF-8 环境变量读写 |
| HX_OS_ENABLE_PATHS | ON | UTF-8 用户、共享、临时与模块路径 |
| HX_OS_ENABLE_THREAD_EX | ON | 栈选项、线程命名与有界信号量 |
| HX_OS_ENABLE_PIPE | ON | 阻塞管道；LOOP 开启时提供异步端点 |
| HX_OS_ENABLE_NET_OPTIONS | ON | 常用/原生套接字选项，依赖 NETWORK |
| HX_OS_ENABLE_POSITIONED_IO | ON | 不改变文件位置的定位读写 |
| HX_OS_ENABLE_FILE_INFO | ON | 扩展时间戳、属性与目录项类型 |
| HX_OS_ENABLE_BOM | ON | Unicode BOM 检测与生成纯函数 |
| HX_OS_ENABLE_TEXT_NUMBER | ON | locale 无关的 double 解析与格式化 |
| HX_OS_ENABLE_CONFIG_SERIALIZE | ON | JSON/INI/properties 写入，依赖 CONFIG |
| HX_OS_ENABLE_TIMER | ON | 单调周期定时器，依赖 LOOP |
| HX_OS_ENABLE_RANDOM | ON | 系统密码学随机数和 UUID v4 |
| HX_OS_ENABLE_VIRTUAL_MEMORY | ON | 匿名页内存和文件映射 |
| HX_OS_ENABLE_FILE_EXTRA | ON | 区间锁、时间戳与权限摘要更新 |
| HX_OS_ENABLE_WATCH | ON | 有界目录通知与显式关闭 |
| HX_OS_ENABLE_LOOP_FD | ON | POSIX 描述符就绪通知，依赖 LOOP |
| HX_OS_ENABLE_SYSTEM_UTILS | ON | 原生 UTF-8 参数和 CPU／系统身份查询 |
| HX_OS_ENABLE_PROCESS | ON | 子进程控制；输出捕获还需 PIPE + LOOP |
| HX_OS_ENABLE_DELAYED_TASKS | ON | 在线程池共享容量内按时安排任务 |
| HX_OS_ENABLE_LOCAL_SOCKETS | ON | 原生路径流，依赖 NETWORK |
| HX_OS_ENABLE_DUPLEX_PIPE | ON | 双向文件端点，独立于 NETWORK |
| HX_OS_ENABLE_REGISTRY | ON | UTF-8 Windows 注册表操作 |
| HX_OS_ENABLE_SLEEP_INHIBIT / PERFORMANCE_REQUEST | ON | 对象作用域原生请求；Linux 明确不支持 |
| HX_OS_ENABLE_OBFUSCATION | ON | 可逆字符串编码，不是密码学保护 |
| HX_OS_ENABLE_CALENDAR / TEXT_LOOKUP / BITSTREAM | ON | 独立日期、翻译和位流工具 |
| HX_OS_ENABLE_SIGNALS / SETTINGS / PROFILER | ON | 分别裁剪的有界通用设施 |
| HX_OS_ENABLE_CJSON/LIBYAML/MINIXML/INIH/TOMLC | OFF | 第三方解析适配 |
| HX_OS_BUILD_TESTS | ON | 契约测试和公共头独立编译 |
| HX_OS_BUILD_EXAMPLES | ON | C 示例；已启用 CXX 时包含 C++ 任务示例 |
| HX_OS_BUILD_BENCHMARKS | OFF | 开发微基准 |
| HX_OS_SANITIZER | 空 | GCC/Clang 的 address,undefined 或 thread |

关闭模块后，CMake 排除其实现源码并保留 ABI 入口；资源创建返回
NOT_SUPPORTED，能力位同步关闭。关闭线程池后不能使用异步 DNS，
其他事件循环能力仍可用。关闭 NETWORK 时可以创建本地 loop 使用投递、定时器
和管道，无需进行网络初始化。

## 字符串与编码

~~~c
#include <hx_os.h>

hx_string text = HX_STRING_INIT;
hx_os_error error;
hx_os_status status;
hx_os_text_result result;
unsigned char utf16[128];

status = hx_string_assign_cstr(&text, "Hello, 中文", &error);
if (status == HX_OS_OK)
	status = hx_string_append_cstr(&text, "!", &error);
if (status == HX_OS_OK)
	status = hx_string_encode(&text, HX_OS_UTF16LE, utf16, sizeof(utf16),
		&result, &error);
/* Inspect status before using the converted bytes. */
hx_string_destroy(&text);
~~~

需要自定义分配器上下文时，使用 `hx_string_init(&text, context)`。
现有操作包括赋值、追加、插入、替换、删除、子串、查找、比较、
复制／移动和 printf 格式化。修改维持合法 UTF-8；
严格解码失败时保留原字符串。编码输出遇到错误时可能已经写入合法前缀，
调用者必须检查状态和实际字节数。

长度和偏移量以字节计。字符数量表示 Unicode 码点数量，不表示用户看到的
字形簇数量。视图借用存储，修改后失效；结构体赋值不能代替拥有者复制。
允许内嵌 NUL，但 C 字符串消费者会在第一个 NUL 处停止。
不承诺 Unicode 归一化或本地语言排序。

MSVC 使用 /utf-8 编译源码。库不更改控制台代码页、标准流或进程 locale。
消费者输出 UTF-8 时应使用合适的终端或自己的 Unicode 控制台适配。
Windows 文件接口会在内部把 UTF-8 路径转换为原生 UTF-16。

## 安装和使用

~~~sh
cmake --install build --config Release --prefix /absolute/install/prefix
~~~

~~~cmake
find_package(hx_c_platform 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE hx_c_platform::hx_c_platform)
~~~

使用 C++11 任务封装时，包含 `hx/ThreadPool.hpp`，
并改为链接 `hx_c_platform::cpp`。
该接口目标仅向选用它的消费者声明 C++11 要求。
关闭测试和示例、只构建 C 库时不会启用 C++ 编译器。

消费者配置时设置 CMAKE_PREFIX_PATH。导入目标携带共享库宏及静态链接
所需依赖。安装内容包括头文件、带版本的库、CMake 导出和 pkg-config
元数据。安装目录使用相对位置时 pkg-config 前缀可随安装包移动；
显式绝对目录保留配置位置。Windows DLL 应放在应用旁边或运行时搜索路径中。

~~~sh
python tools/verify_install.py build --config Release
~~~

脚本安装到 build/installed，运行八个 C99/C++11 消费者，再用纯 C CMake
工程运行六个消费者，覆盖配置、容器、C++ 任务、设置、profiler 和原子。
共享库还可运行 `python tools/verify_abi.py build --config Release`，使用
基线旧头文件及测试链接安装后的新库。这些是原生主机验证，不自动模拟交叉目标。

## 平台和结构

~~~mermaid
flowchart TD
    A[应用与业务库] --> B[公共 C ABI]
    B --> C[共享字符串 Unicode 内存池和生命周期契约]
    C --> D[CMake 选择系统适配]
    D --> E[Win32 与 IOCP]
    D --> F[共享 POSIX 实现]
    F --> G[Linux epoll]
    F --> H[macOS kqueue 与 Mach]
~~~

依赖方向单向向下。没有可被某个消费者修改的全局分配器、日志器或队列。
系统、编译器、CPU 和可选依赖分别处理，默认直接调用构建期选定的实现。

Windows 7 SP1/8.1 x86/x64 仍是 API 兼容设计基线，同时需要匹配 CRT/SDK。
当前 Windows x86/x64 与 WSL Linux 已有本机运行证据。
macOS 已有适配源码，尚未在 Apple 主机编译和运行。
嵌入式 Linux ARMv7-A/AArch64 的 glibc、musl、uClibc-ng 属于目标范围；
实际板卡、SDK 及最低版本仍待验证和锁定。

~~~sh
cmake -C cmake/minimal.cmake -S . -B build-board \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/embedded-linux.cmake \
  -DHX_OS_TARGET_PROCESSOR=aarch64 \
  -DHX_OS_CROSS_PREFIX=/sdk/bin/aarch64-linux-gnu- \
  -DHX_OS_SYSROOT=/sdk/sysroot \
  -DHX_OS_BUILD_TESTS=OFF -DHX_OS_BUILD_EXAMPLES=OFF
~~~

必须替换为实际 SDK，不能由示例推断 libc 或浮点 ABI。
minimal 配置关闭可选模块，保留错误/context、时钟、文件、UTF-8 字符串与转换、
线程/同步/TLS 和原子。把需要的 `-DHX_OS_ENABLE_...=ON` 写在 `-C` 后面。
关闭测试/示例后只需 C 编译器。普通桌面默认开启全部新增模块，原有第三方适配
仍按需启用。未指定版本时，macOS 默认 Intel 10.13、ARM64 11.0，通用二进制
使用 11.0；仅可选 Apple 性能请求使用 Objective-C/Foundation，原生验证待补。
编译检查要求可链接的 64 位原子操作和单调条件变量，必要时链接 libatomic。
若平台缺少有限时间的原生线程 join，会明确报告能力缺失，
不会通过释放运行中的线程来伪装完成。

Android、iOS、BSD 尚需明确适配和目标验证；未知 CMake 系统配置失败。
可复用 POSIX 文件、pthread 和套接字抽象，但 API 下限、沙箱、DNS、
FLS/TLS 清理、计时和后端选择需要逐项核实。FreeRTOS、裸机不在当前范围。

## 工程规范与证据

自有源码采用 UTF-8 无 BOM、LF、8 列 Tab、80 列，C 函数使用小写下划线。
C++ 封装按项目的 C++ 类名和方法名规范命名。
警告仅作用于自有目标，默认视为错误。第三方目标不套用自有警告配置，
也不改动或格式化第三方文件。库本身不要求 C++ 编译器。

- [行为与生命周期契约](docs/CONTRACTS.zh-CN.md)
- [C 容器及所有权](docs/CONTAINERS.zh-CN.md)
- [C++11 线程池与关闭](docs/CPP_POOL.zh-CN.md)
- [验证矩阵和待目标环境检查](docs/VALIDATION.zh-CN.md)
- [后续 hx_c_log 迁移方案](docs/MIGRATION.zh-CN.md)
- [第三方声明](THIRD_PARTY_NOTICES.zh-CN.md)

本项目新代码的发布许可证尚未选定；保留的第三方许可证另行记录。
汇编和 intrinsic 限制在 CPU 原语中，性能改进必须有实测依据。
目前没有声称新库或未来迁移已通过 hx_c_log 的锁定性能门槛。


[ci-badge]: https://img.shields.io/badge/CI-configured-lightgrey?logo=githubactions
[c-badge]: https://img.shields.io/badge/C-99-blue?logo=c
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-11-blue?logo=cplusplus
[header-badge]: https://img.shields.io/badge/header--only-C%2B%2B%20facade-yellowgreen
[cmake-badge]: https://img.shields.io/badge/CMake-3.16%2B-blue?logo=cmake
[license-badge]: https://img.shields.io/badge/license-pending-lightgrey
[linux-badge]: https://img.shields.io/badge/Linux-GCC%20%7C%20Clang-yellow?logo=linux
[macos-badge]: https://img.shields.io/badge/macOS-pending-lightgrey?logo=apple
[windows-badge]: https://img.shields.io/badge/Windows-MSVC-blue
[tests-badge]: https://img.shields.io/badge/tests-Win%2082%2F82%20Linux%2084%2F84-brightgreen
[embedded-badge]: https://img.shields.io/badge/embedded%20Linux-target%20pending-lightgrey
[android-badge]: https://img.shields.io/badge/Android-API%2021%20ARM%20builds-blue?logo=android
