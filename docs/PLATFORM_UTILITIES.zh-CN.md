# 平台便利功能

[English](PLATFORM_UTILITIES.md)

本页汇总了 `hx_c_platform` 提供的跨平台系统级工具与便利接口，涵盖系统硬件与资源查询、动态库（插件）加载、文件与路径快捷操作、环境变量与标准目录、线程扩展与信号量、匿名/异步管道、定位读写、内存映射、目录监听、子进程管理与输出捕获、本地套接字、注册表、防睡眠电源请求、日历与翻译表、同步信号分发、可变设置（Settings）以及有界区间计时器。

你可以先从下表中挑选要完成的任务，按[示例运行说明](../examples/README.zh-CN.md#run)编译对应示例，并参考[输入准备](../examples/README.zh-CN.md#inputs)准备测试文件或命令行参数：

| 想做什么 | 完整示例程序 | 核心调用流程与预期输出 |
| --- | --- | --- |
| 查询操作系统版本与当前进程 CPU 占用率 | [system_info.c](../examples/system_info.c) | 读取系统版本 → 创建 CPU 采样器 → 睡眠间隔后调用 `query` → 打印系统版本与 CPU 百分比 |
| 一次性读取 UTF-8 文本文件并可选原子另存 | [text_file.c](../examples/text_file.c) | `hx_os_file_read_text` → 可选 `hx_os_file_write_text` → 打印内容 → 销毁字符串 |
| 不移动文件游标，从指定偏移量读取 4 个字节 | [positioned_read.c](../examples/positioned_read.c) | 打开文件 → `hx_os_file_read_at(offset=4)` → 关闭（对 `demo.txt` 输出 `34 35 36 37`） |
| 将磁盘文件直接映射到内存并计算校验值 | [mapped_file.c](../examples/mapped_file.c) | `hx_os_file_map_open` → 读取借用字节视图 → 打印字节数与 CRC32 → 销毁映射 |
| 读取进程环境变量 | [environment.c](../examples/environment.c) | 传入 `PATH` 等变量名 → `hx_os_env_get_string` → 打印变量值（只读，不修改环境） |
| 获取当前用户的配置目录等标准路径 | [special_paths.c](../examples/special_paths.c) | `hx_os_path_config_home_string` → 打印绝对路径（仅查询路径，不自动创建文件夹） |
| 实时监听目录内的文件增删改事件 | [watch_directory.c](../examples/watch_directory.c) | 创建监听器 → 在 5 秒窗口内接收并打印变更回调 → 安全关闭（可在另一终端修改文件观察输出） |
| 启动子进程并捕获其 `stdout` / `stderr` 输出 | [capture_process.c](../examples/capture_process.c) | 传入可执行文件路径与参数 → `hx_os_process_capture_run` → 等待退出 → 读取输出并关闭会话 |
| 动态加载业务插件并绑定导出函数 | [load_plugin.c](../examples/load_plugin.c) | 打开动态库 → 校验 ABI 版本 → 绑定函数指针 → 调用 → 卸载（配套[公开插件](../examples/README.zh-CN.md#plugin)） |
| 在本地异步管道两端传输数据 | [pipe_transfer.c](../examples/pipe_transfer.c) | 创建本地事件循环与管道对 → 提交异步读写 → 轮询完成回调 → 关闭管道与循环 |

以上功能均提供直接可用的 C99 入口，并在 `include/hx/` 下提供共用同一底层实现的 C++11 无异常封装。向接口传入 `NULL` 上下文（`hx_context *`）会自动选用内置的不可变默认分配器。当某项功能通过 CMake 开关裁剪关闭时，公共头文件声明与失败桩函数仍会保留，对应能力位（Capability Bit）清零，调用时明确返回 `HX_OS_NOT_SUPPORTED`。

## 系统信息

`hx_os_sysinfo.h`（由 `HX_OS_ENABLE_SYSINFO` 控制，默认开启）提供物理/可用内存字节数、CPU 逻辑与物理核心拓扑、内存页大小、磁盘文件系统空间、当前进程内存占用以及操作系统版本字符串查询。即使关闭 `HX_OS_ENABLE_SYSINFO`，库版本与 ABI 版本查询依然可用。所有查询均为无副作用的按需读取，不会在后台启动线程或修改进程状态：

```c
hx_string version = HX_STRING_INIT;
hx_os_error error;

hx_os_status status = hx_os_sys_version_string(&version, &error);

// Check status, then use hx_string_c_str(&version).
hx_string_destroy(&version);
```

- **结构初始化与缓冲区约定**：带版本号的磁盘空间与进程内存结构体请使用对应的 `*_INFO_INIT` 宏初始化。基于字符缓冲区的文本查询接口返回的所需容量均包含末尾 `NUL` 终止符；查询失败时，标量与结构体输出均保持原值不变。
- **CPU 拓扑与内存/磁盘口径**：
  - **CPU 核心数**：反映系统当前在线的 CPU 数量（Windows 下跨全部处理器组统计），不等同于当前进程的亲和性掩码或容器 cgroup 配额。Linux 下通过 `sysfs` 对在线 `package/core` 组合去重来统计物理核心数；当宿主环境无法提供真实物理拓扑时，明确返回 `HX_OS_NOT_SUPPORTED`，绝不用逻辑核心数冒充物理核心数。
  - **可用内存与磁盘空间**：可用内存分别读取 Windows 可用物理内存、Linux `MemAvailable` 以及 macOS 的空闲页加非活跃页估算值（注意：系统内存快照不代表随后的大块 `malloc` 必然成功，且 Linux 读取的是宿主内核视图而非容器专属限额）。POSIX 磁盘可用空间读取非特权用户可用的 `f_bavail`（区别于包含根用户保留块的 `f_bfree`）。
  - **当前进程内存**：驻留字节（Resident Bytes）对应 Windows 工作集（Working Set）或 POSIX RSS；虚拟字节（Virtual Bytes）对应 Windows 已保留加已提交地址空间、Linux `/proc/self/statm` 总大小或 macOS Mach 虚拟大小（虚拟地址空间大小不等同于物理内存消耗或系统 Commit 账目）。
- **进程 CPU 占用率采样器（`hx_os_cpu_usage` / `hx::CpuUsage`）**：调用 `hx_os_cpu_usage_create` 会记录当前进程 CPU 时间与单调时钟的初始基线。此后每次调用查询，都会返回自上一次成功采样以来的平均 CPU 占用百分比：`100 * process_cpu_delta / elapsed_delta / initial_logical_cpu_count`（不含子进程）。若两次查询间隔为零，则返回 `HX_OS_WOULD_BLOCK` 并保留原采样基线。每个采样器实例互相独立，需由调用方串行访问。C++11 封装返回 `hx::Result<T>`（详见 [Result 契约](CPP_RESULTS.zh-CN.md) 与 [system_info.c](../examples/system_info.c)）：

```cpp
#include <hx/SystemInfo.hpp>

hx::Result<double> sample_cpu() noexcept
{
	auto cpu = hx::CpuUsage::create();
	if (!cpu)
		return hx::Failure(cpu.error());

	hx_os_error error = {};

	auto status = hx_os_sleep_until(hx_os_deadline_after_ms(10), &error);
	if (status != HX_OS_OK)
		return hx::Failure(status, error);

	return cpu.value().query();
}
```

## 动态库

`HX_OS_ENABLE_LIBRARY` 默认开启。`hx_os_library_open`、`hx_os_library_symbol` 和 `hx_os_library_close` 提供基础动态库加载与符号查找；带 `_ex` 后缀的变体还可额外接收一个已初始化的 `hx_string`，用于捕获系统加载器返回的错误诊断文本。在 C++11 中，`hx::Library` 提供移动语义的句柄管理与类型化符号查找 `symbol<Pointer>(name)`：

```cpp
#include <hx/Library.hpp>

hx::Result<uint32_t> plugin_abi(const char *path) noexcept
{
	auto plugin = hx::Library::open(path);
	if (!plugin)
		return hx::Failure(plugin.error());

	using Abi = uint32_t(HX_OS_CALL *)(void);

	auto symbol = plugin.value().symbol<Abi>("hx_plugin_abi_version");
	if (!symbol)
		return hx::Failure(symbol.error());

	return symbol.value()();
}
```

- **路径解析与加载器诊断**：输入为 UTF-8 文件系统路径。相对文件名会按当前工作目录解析为文件，而不会当作裸 POSIX `soname` 去系统库目录搜索（被加载模块自身依赖的其他动态库仍遵循操作系统加载器规则）。POSIX 下固定使用 `RTLD_NOW` 搭配可选的 `LOCAL` / `GLOBAL` 可见性标志（Windows 忽略可见性标志）。由于 POSIX `dlerror()` 只提供文本而无标准错误码，加载失败时主错误域设为 `HX_OS_ERROR_LOADER`，并在触发任何可能重入加载器的分配器回调前先在栈上拷贝最多 `2,047` 字节的诊断文本（转义非法 UTF-8 字节并标记截断）。
- **直接绑定函数指针（`hx_os_library_bind`）**：C 语言调用方可以使用 `hx_os_library_bind` 或 `hx_os_library_bind_ex`，直接传入函数指针变量的地址和 `sizeof(variable)`，一步完成符号查找与指针写入，无需手写冗长的类型转换；查找失败时目标变量保持原值不变。
- **卸载生命周期安全**：对同一个库句柄的操作与关闭需由调用方串行化。在卸载（`close`）插件之前，宿主必须确保插件中的所有函数调用已返回、插件分配的对象已释放、注册的回调已断开，且插件相关的线程局部存储（TLS）析构函数已执行完毕。`close` 即使遇到底层报错也会消费句柄。你可以按[运行步骤](../examples/README.zh-CN.md#plugin)一同编译并运行公开示例 [load_plugin.c](../examples/load_plugin.c) 与 [demo_plugin.c](../examples/demo_plugin.c)，查看完整的 `Compatible plugin: Example plugin 1.0` 验证输出。

## 文件与路径便利操作

`HX_OS_ENABLE_FILE_UTILS` 默认开启，提供文本/二进制文件一键读写、原子文件发布、安全复制/备份替换、路径解析、通配符 `glob` 以及目录遍历与递归删除：

- **一次性读写文本与二进制文件**：
  - `hx_os_file_read_text(path, &text, &error)` 默认严格校验 UTF-8 编码、自动剥离文件开头的 UTF-8 BOM，并将整个文件大小限制在 `16 MiB` 以内（可通过显式 `load` 扩展接口调整大小上限与 BOM 保留策略）。编码错误、超出大小上限或 I/O 失败均保持原输出字符串不变。
  - `hx_os_file_write_text(path, &text, &error)` 将已有字符串写入文件。完整示例见 [text_file.c](../examples/text_file.c)。
  - 二进制读取接口返回由调用方独占持有的内存块（哪怕读取的是 0 字节空文件，也会返回一块合法的可释放分配），用完后统一调用 `hx_os_memory_free` 释放。
- **原子保存（Atomic Save）与带备份替换**：
  - 原子保存先在目标文件所在目录排他创建临时文件、完整写入数据、请求文件刷盘（`fsync` / `FlushFileBuffers`）、关闭句柄，最后通过原子重命名覆盖目标路径。扩展结果结构体可区分“文件重命名已发布”与“父目录同步结果”（例如 Windows 不支持对目录句柄刷盘，会在扩展结果中报告 `HX_OS_NOT_SUPPORTED`，而不会将已成功的发布改判为失败）。
  - 文件复制拒绝自我覆盖；带备份替换在 Windows 上调用原生替换 API，在 POSIX 上执行两次重命名（已存在同名备份文件时会明确拒绝，避免覆盖旧备份）。
- **路径拆解、当前工作目录与 `glob` 展开**：
  - `basename`、`dirname` 和 `extension` 为零分配的纯词法借用视图（同时识别 `/` 和 `\` 分隔符，即使关闭 `HX_OS_ENABLE_FILE_UTILS` 也依然可用；扩展名包含前导 `.`，但纯以 `.` 开头的隐藏文件名本身不算扩展名）。
  - 获取或切换当前工作目录（`cwd`）会影响整个进程，多线程程序须自行协调相对路径访问。查询绝对路径要求目标文件存在并会解析符号链接。
  - `hx_os_path_glob`（需同时启用 `HX_OS_ENABLE_CONTAINERS` 与 `HX_OS_ENABLE_FILE_UTILS`）返回持有 `hx_string` 元素的序列容器（用 `hx_os_sequence_destroy` 释放）。支持区分大小写的 UTF-8 `*` 和 `?`（单码点匹配），前导 `.` 文件需显式匹配 `.`，不支持 `[...]` 或 `**` 跨层级展开。
- **目录枚举与递归删除**：
  - 目录遍历与递归删除默认限制最大深度 `64`（可调至 `256`）、最多访问 `1,000,000` 个目录项、最多 `65,536` 个匹配，容器与文本预算各 `16 MiB`。递归删除严格**不跟随符号链接**（POSIX 使用目录描述符相对 `openat` 加 `O_NOFOLLOW`，Windows 直接打开重解析点本身并保持目录句柄防止并发改名）。

```cpp
#include <hx/Files.hpp>
#include <cstdio>

hx::Result<void> list_directory(const char *path) noexcept
{
	auto directory = hx::Directory::open(path);
	if (!directory)
		return hx::Failure(directory.error());

	for (;;) {
		auto entry = directory.value().next();
		if (entry.status() == HX_OS_EOF)
			return hx::Result<void>();

		if (!entry)
			return hx::Failure(entry.error());

		std::puts(entry.value().name());
	}
}
```

在 C++11 `<hx/Files.hpp>` 中，`hx::File::open` 与 `hx::Directory::open` 返回只可移动的核心句柄包装（即使关闭 `HX_OS_ENABLE_FILE_UTILS` 也可用）。`File::read` 将到达文件末尾作为成功的 `ReadResult{bytes, eof}` 返回，而 `readSome` / `readAt` 在遇到 `HX_OS_EOF` 或错误时仍可通过 `hasValue()` 检查已传输字节数。单层目录回调遍历可使用 `hx_os_directory_visit` 或 `hx::Directory::forEach`（详见[遍历与文本视图契约](COMMON_UTILITIES.zh-CN.md)）。

## 环境变量

`HX_OS_ENABLE_ENV` 默认开启。`hx_os_env_get_string` 使用输出 `hx_string` 自身的分配器上下文读取环境变量（若变量不存在则返回 `HX_OS_NOT_FOUND`，参见 [environment.c](../examples/environment.c)）；C++11 的 `hx::Environment::get` 与 `getOr` 返回 `hx::Result<hx::String>`（仅在变量完全不存在时回退默认值，已存在的空字符串变量仍返回空字符串）：

```cpp
#include <hx/Environment.hpp>

hx::Result<hx::String> application_mode() noexcept
{
	return hx::Environment::getOr("APP_MODE", "normal");
}
```

- **修改与删除环境变量**：`hx_os_env_set(name, value, replace, &error)` 用于修改当前进程的环境变量（当 `replace == 0` 时保留已有值，包括已有的空值）；删除一个本来就不存在的变量视为成功。变量名不得为空或包含 `'='`。修改后的环境块会被之后新启动的子进程继承，不影响父进程或已经运行的子进程。
- **并发与平台编码边界**：同一份已加载库实例内的环境变量读写通过内部锁自动同步（若其他线程在锁外持续扩增同一变量导致连续 8 次容量重试仍不够，查询会返回 `HX_OS_BUSY` 并保持原输出不变）。在 Windows 上，本库直接操作 Win32 Unicode 环境块——由于 C 运行库（CRT）的 `getenv` / `_environ` 维护的是独立缓存，可能看不到 Win32 层面的修改，请成对使用本库的环境变量读写接口。在 POSIX 上，若原生环境变量包含非法 UTF-8 字节，查询会返回 `HX_OS_INVALID_ENCODING`。

## 常用目录

`HX_OS_ENABLE_PATHS` 默认开启（独立于 `ENV`、`FILE_UTILS` 和 `LIBRARY` 开关）。C 接口如 `hx_os_path_config_home_string(&path, &error)` 与 C++11 接口如 `hx::Paths::configHome()` 提供七类标准系统路径查询（详见 [special_paths.c](../examples/special_paths.c)）。所有查询均返回不带尾部斜杠的绝对 UTF-8 路径（根目录除外），**仅返回路径字符串，不会在磁盘上自动创建目录，也不保证路径当前一定存在或可写**：

| 路径查询项 | Windows 解析来源 | Linux 解析来源 | macOS 解析来源 |
| --- | --- | --- | --- |
| `home` | 绝对路径 `USERPROFILE`，否则回退系统 Profile 已知目录 | 绝对路径 `HOME`，否则查询当前 UID 密码数据库 | 与 Linux 相同的 POSIX 查询 |
| `config_home` | `FOLDERID_RoamingAppData` | 绝对路径 `XDG_CONFIG_HOME`，否则 `HOME/.config` | `HOME/Library/Application Support` |
| `data_home` | `FOLDERID_LocalAppData` | 绝对路径 `XDG_DATA_HOME`，否则 `HOME/.local/share` | `HOME/Library/Application Support` |
| `program_data` | `FOLDERID_ProgramData` | `/var/lib` | `/Library/Application Support` |
| `temp` | `GetTempPathW` | 绝对路径 `TMPDIR`，否则 `/tmp` | 绝对路径 `TMPDIR`，否则 Darwin 用户临时目录，再回退 `/tmp` |
| `executable` | `GetModuleFileNameW` | `/proc/self/exe` | `_NSGetExecutablePath` + `realpath` |
| `module` | 当前动态库（或静态链接时的可执行文件）所在目录 | `dladdr` 返回绝对路径时的所在目录 | 与 Linux 相同 |

> [!NOTE]
> - `program_data` 表示整机共享的应用数据目录，应用使用时应在其下拼接自己的子目录名并按需创建。
> - 遵循 [XDG 基础目录规范](https://specifications.freedesktop.org/basedir/latest/)，Linux 下为空或为相对路径的 `XDG_*` 环境变量会被自动忽略并回退到默认路径（但若包含非法 UTF-8 字节则会明确报错）。
> - Windows 下使用 `SHGetKnownFolderPath`（带 `KF_FLAG_DONT_VERIFY`）加 `CoTaskMemFree`，无需调用方预先执行 `CoInitialize`。POSIX 下若动态加载器只记录了相对模块名，`module` 查询会返回 `HX_OS_NOT_SUPPORTED`，防止工作目录改变后拼出错误路径。

## 线程选项、命名与信号量

`HX_OS_ENABLE_THREAD_EX` 默认开启，提供自定义线程栈大小、UTF-8 线程命名以及有界计数信号量：

- **自定义线程栈大小（`hx_os_thread_create_with_options`）**：使用 `HX_OS_THREAD_OPTIONS_INIT` 初始化选项结构体后，可设置以字节为单位的 `stack_size`（设为 `0` 表示沿用操作系统默认栈大小；示例见 `examples/thread_options.c`）。POSIX 会校验最小栈大小与页对齐要求；Windows 底层继续使用 `_beginthreadex` 配合 `STACK_SIZE_PARAM_IS_A_RESERVATION`（超出 `unsigned` 范围返回 `HX_OS_RANGE`）。在嵌入式环境配置小栈时，请为回调函数、TLS 变量及 Sanitizer 留出余量。
- **线程命名**：名称必须为合法 UTF-8。Linux 与 macOS 分别在 `15` 字节和 `63` 字节上限内按完整 Unicode 码点安全截断（macOS 仅支持为当前调用线程自身命名）。Windows 动态查找 `KernelBase.dll` 中的 `SetThreadDescription`（在不支持该 API 的旧版 Windows 7 上返回 `HX_OS_NOT_SUPPORTED`）。对已 `join` 的线程命名返回 `HX_OS_CLOSED`。
- **有界信号量（`hx_os_semaphore` / `hx::Semaphore`）**：基于互斥锁与条件变量实现有界令牌计数。`post(count > 0)` 在当前无等待线程时也会累计令牌，若加上 `count` 后超过创建时设定的最大值，则整体拒绝并返回 `HX_OS_LIMIT`（不做部分增加）。`wait_until` 支持单调时钟截止时间（传入 `HX_OS_DEADLINE_NOW` 时若有令牌则立即消费，无令牌则立即返回 `HX_OS_TIMEOUT`；也支持 `HX_OS_DEADLINE_INFINITE`）。
- **C++11 线程所有权模型**：`hx::Thread` 采用与线程池一致的显式 `owner` 指针托管模型——调用 `closeUntil` 成功完成 `join` 后才销毁句柄；若等待超时则保留 `owner` 供后续重试，析构函数绝不会隐式阻塞等待或强行终止线程。

## 管道与本地 I/O 循环

当进程内或父子进程间需要流式字节通道时，可以使用同步匿名管道或接入本地事件循环的异步管道（示例见 [pipe_transfer.c](../examples/pipe_transfer.c)）：

- **同步匿名管道（`hx_os_pipe_pair_anonymous` / `hx::Pipes::anonymous`）**：创建一对阻塞式 `hx_os_file` 读写端点，直接复用常规文件读写与关闭接口。所有写端关闭且缓冲区数据读尽后，读端返回 `HX_OS_EOF`；读端关闭后继续写入则返回 `HX_OS_CLOSED`。Linux 下使用带 `O_CLOEXEC` 的 `pipe2` 创建；macOS 通过 `fcntl` 设置 `FD_CLOEXEC`（在 macOS 上并发创建管道与 `fork`/`exec` 时需由宿主同步）。
- **异步管道与父子进程半异步管道**：
  - `hx_os_pipe_pair_async` 创建一对支持截止时间与取消的异步管道端点，配合 `hx_os_pipe_read_async` / `hx_os_pipe_write_async` 使用（写入默认复制缓冲区并写满全部字节，扩展接口支持借用缓冲区）。
  - `hx_os_pipe_pair_child` 创建“一端为父进程异步管道、另一端为子进程同步文件”的混合管道对（初始均为不可继承，待调用 `hx_os_process_spawn` 时按需复制给子进程标准流）。
- **无网络的本地事件循环（`hx_os_loop_create_local`）**：无需初始化网络模块，即可驱动任务投递（Post）、周期定时器与本地异步管道（开启 `HX_OS_ENABLE_PIPE` 与 `HX_OS_ENABLE_LOOP` 即具备 `HX_OS_CAP_ASYNC_PIPE` 能力，完全独立于 `HX_OS_ENABLE_NETWORK`）。
  - 每个异步管道端点同一时刻只接受一个由 `loop` 创建线程提交的请求，并与套接字共享 `loop` 的请求槽位与在途字节预算。
  - `close_until` 会阻止新请求、发起取消并轮询驱动底层原生 I/O 安全退役；若超时则保留指针与借用缓冲区供重试（在回调中等待同一 `loop` 的未完成操作或重入关闭会返回 `HX_OS_DEADLOCK`）。必须在销毁 `loop` 之前先关闭所有附属管道。
  - **POSIX 线程局部 `SIGPIPE` 屏蔽**：在 POSIX 上向已断开的管道写入时，库仅在调用线程上临时阻塞 `SIGPIPE`、吸收本次断管信号并恢复原信号掩码（保留线程原先已挂起的 `SIGPIPE`），绝不安装或覆盖进程级的 `SIGaction` 信号处理器。

## 套接字选项与定位文件 I/O

如果你需要配置 TCP Keepalive 探测参数、IPv6 双栈监听、UDP 广播、组播加组或枚举本机网卡地址，请参阅专门的[网络选项与设备发现](NETWORK_OPTIONS.zh-CN.md)指南。

- **常用套接字开关（`HX_OS_ENABLE_NET_OPTIONS`）**：提供跨平台的 `hx_os_socket_set_nodelay`、`hx_os_socket_set_keepalive` 和 `hx_os_socket_set_reuseaddr`（地址复用保持显式开启，`create` / `bind` 不会隐式打开它）。注意：一旦套接字附加到了事件循环（`loop`），即使当前空闲，修改选项也会返回 `HX_OS_BUSY`。C++11 借用外观 `hx::SocketOptions` 同时提供这些 setter 以及 `peerAddress()` 查询。底层逃生舱接口 `option_set` / `option_get` 允许直接传入目标平台 SDK 的 `level` / `name`（C++ `get<T>` 要求操作系统返回的长度与 `sizeof(T)` 严格相等，否则返回 `HX_OS_RANGE`；例如 Windows 下 `TCP_NODELAY` 可能在 4 字节缓冲区中仅返回 1 字节，C 调用方应检查返回的实际长度）。
- **定位文件读写（`HX_OS_ENABLE_POSITIONED_IO`）**：独立于 `FILE_UTILS`。`hx_os_file_read_at` 与 `hx_os_file_write_at`（C++11 为 `File::readAt` / `writeAt`）接收绝对字节偏移量，在不改变文件常规顺序读写游标的前提下传输数据（完整示例见 [positioned_read.c](../examples/positioned_read.c)）：
  - 单次传输上限为 `INT_MAX` 字节，偏移量加长度必须落在有符号 64 位整数范围内。以追加模式（Append）打开的文件不支持定位写入（返回 `HX_OS_NOT_SUPPORTED`），打开权限不匹配时返回 `HX_OS_ACCESS_DENIED`。
  - **底层实现机制**：POSIX 直接调用 `pread` / `pwrite`（自动重试 `EINTR`）。Windows 在首次执行定位读写时，内部通过 `ReOpenFile` 为同一文件对象惰性开启第二个带 `FILE_FLAG_OVERLAPPED` 的句柄并通过原子 CAS 发布缓存；此后每次定位调用使用独立的事件与 `OVERLAPPED` 结构，从而既不篡改主文件句柄的共享游标，又支持多线程并发定位读写（文件最终关闭时会一并释放这两个句柄）。

## 文件属性、目录项类型与 BOM

- **扩展文件属性与目录项类型（`HX_OS_ENABLE_FILE_INFO`，默认开启）**：
  - `hx_os_path_stat_extended`（需用 `HX_OS_FILE_INFO_EXTENDED_INIT` 初始化输出结构）和 `hx_os_directory_next_info`（C++11 为 `hx::Files::info` 与 `directory.next(true)`，示例见 `examples/file_info.c`）提供纳秒级时间戳、权限摘要与不跟随链接的目录项分类。
  - **时间戳与权限摘要**：时间戳为有符号 Unix 秒加 `[0, 10^9)` 纳秒；元数据变更时间读取 POSIX `ctime` 或 Windows `FILE_BASIC_INFO.ChangeTime`（而非创建时间）。`read_only` 反映 Windows 只读属性或 POSIX 缺少 owner-write 位；POSIX 还会标记 owner-read/write 摘要有效，而 Windows 下将该模式位摘要标为未知（不臆测复杂的 NTFS ACL）。
  - **目录项类型识别**：针对目录项本身分类（不跟随符号链接，Windows 重解析点统一报告为 `LINK`；POSIX 优先读取 `d_type`，未知时基于已打开目录描述符执行相对 `fstatat`）。
- **字节序标记检测与生成（BOM 工具，默认开启）**：
  - `hx_os_text_bom_detect`（C++11 为 `hx::Text::detectBom`）按最长前缀优先识别 `UTF-32LE/BE`、`UTF-8` 和 `UTF-16LE/BE` 的完整 BOM 标记；若无 BOM 则返回 `HX_OS_NOT_FOUND` 且 `prefix_bytes = 0`（注意：它不做流式半截猜测——若只传入 `FF FE` 两个字节，会被识别为完整的 `UTF-16LE` BOM；要区分 `UTF-32LE` 请先读够 4 个字节）。`hx_os_text_bom_bytes` 则生成最多 4 字节的原始 BOM 序列。

## 配置序列化与数字文本

- **多格式配置序列化与原子保存（`HX_OS_ENABLE_CONFIG_SERIALIZE`）**：默认开启，依赖 `HX_OS_ENABLE_CONFIG`。`hx_os_config_serialize` 将不可变配置文档序列化到 `hx_string`，`hx_os_config_save`（还需 `HX_OS_ENABLE_FILE_UTILS`）则一步完成序列化与原子文件保存（支持 JSON、INI、`.properties`、YAML、XML、TOML 和 `.env` 七种格式，详见[序列化教程](SERIALIZATION.zh-CN.md)与 `examples/config_save.c`）。
  - JSON 输出保留原始合法数字拼写（大整数不经过 `double` 舍入而损失精度）；TOML 非十进制整数精确转为十进制。INI 与 `.properties` 遇到无法无损表达的嵌套类型、非字符串或特殊字符时会明确返回 `HX_OS_UNREPRESENTABLE`，绝不静默伪造字符串。
  - 序列化采用“有界测量 + 一次性分配写入”两遍扫描，严格受 `max_work`、`max_nodes`、`max_depth`（最大 128 层）、`max_input_bytes` 与 `max_memory_bytes` 约束。C++11 `hx::ConfigDocument` 除提供 `string()` / `stringOr()` 拷贝读取外，还提供零拷贝的 `stringView(pointer)`。
- **独立数值文本转换（`HX_OS_ENABLE_TEXT_NUMBER`）**：提供不受进程当前 `locale` 干扰的 `parse_double` 与 `format_double`（IEEE 754 `binary64` 输出 17 位有效数字，支持 TOML 下划线/进制语法与规范化的 `nan`/`inf`，上溢或下溢返回 `HX_OS_RANGE`）。

## 周期定时器

`HX_OS_ENABLE_TIMER` 默认开启（依赖 `HX_OS_ENABLE_LOOP`，无需开启 `NETWORK`）。调用 `hx_os_timer_create(loop, interval_ns, callback, user, &timer, &error)`（C++11 为 `hx::PeriodicTimer::every`，示例见 `examples/periodic_timer.c`）可在事件循环上注册按单调纳秒间隔触发的周期定时器：

- **固定时间轴与漏拍合并**：定时器在创建后经过一个 `interval_ns` 首次触发，后续严格沿原定时间轴调度；如果某次回调耗时较长或轮询延迟导致错过了多个周期，错过的周期会合并为当次 `poll` 的**单次触发**，不会突发连跑多次回调。
- **停止、等待与关闭边界**：回调在轮询线程锁外执行（返回 `HX_OS_OK` 继续，返回终止性错误停止）。`hx_os_timer_stop` 支持跨线程异步请求停止；`close_until` 仅限创建线程调用，负责驱动取消并等待最终回调与 `cleanup` 完成（若超时返回 `HX_OS_TIMEOUT`，保留对象供重试；在回调或 `cleanup` 内同步关闭自身会返回 `HX_OS_DEADLOCK`）。注意：销毁 `loop` 前必须先逐一关闭所有关联的定时器对象，否则 `hx_os_loop_destroy` 会返回 `HX_OS_BUSY`。

## 线程池身份查询

当一段通用业务函数既可能在外部线程调用、也可能在线程池的工作线程内部被调用时，可以使用 `hx_os_thread_pool_inside(pool, &inside)`（C++11 为 `pool.inside()`）判断当前线程是否属于指定线程池的工作线程（包括正在执行任务 `cleanup` 回调的阶段）：

```c
int inside;

hx_os_status status = hx_os_thread_pool_inside(pool, &inside);
if (status == HX_OS_OK && !inside)
	status = hx_os_task_wait_until(task, deadline, &work_status, &error);
// Inside a worker, continue asynchronously instead of waiting for new work.
```

该查询直接读取现有的线程局部标识（TLS），零分配、零加锁、零系统调用。由于线程池的工作线程如果阻塞等待同一个池中尚未完成的任务（或试图关闭自身所属的线程池）会立即触发 `HX_OS_DEADLOCK`，提前查询 `inside` 可以安全分流同步等待与异步续接逻辑（注：若存在多个线程池，跨池循环等待仍需由调用方自行避免）。

## 原生错误文本

`hx_os_error_message` 会将 `hx_os_error` 格式化为可读的严格 UTF-8 诊断字符串。除了常规系统错误码外，它还支持追加地址解析错误（`GAI` 错误域，POSIX 使用 `gai_strerror`，Windows 直接使用线程安全的 `FormatMessageW` 宽字符转换，无需 `WSAStartup`，避免了[微软文档指出非线程安全的 `gai_strerrorW`](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrorw)）以及 Apple 平台的 `Mach` 错误域（`mach_error_string`）。

输出的 `required` 容量包含末尾 `NUL`；由于原生错误措辞取决于操作系统版本与语言环境，请仅将其用于日志打印，**不要在代码中通过匹配错误文本来编写分支逻辑**。

## 随机数、UUID、CRC32 和安全清零

- **系统密码学随机数（`HX_OS_ENABLE_RANDOM`，默认开启）**：
  - `hx_os_random_bytes` 直接调用操作系统密码学安全随机源填满缓冲区，**绝不使用时间种子或普通伪随机数发生器（PRNG）回退**。若底层失败，函数会将整个输出缓冲区清零。
  - **各平台随机源实现**：Windows 从系统目录加载 `BCryptGenRandom`（仅在入口不存在时回退 `CryptGenRandom`）；Linux 优先调用 `getrandom` 系统调用（自动重试 `EINTR` 与短读，仅在旧内核返回 `ENOSYS` 时先从 `/dev/random` 读够 32 字节熵再读取 `/dev/urandom`，原因详见 [Linux `random(4)` 手册](https://www.man7.org/linux/man-pages/man4/random.4.html)）；Apple 使用 `arc4random_buf`。
- **UUID v4 生成与格式化**：`hx_os_uuid_generate_string(&text, &error)`（C++11 为 `hx::Random::uuid()`，示例见 `examples/random_uuid.c`）一步生成符合 RFC 规范的随机 UUID v4 并格式化为小写 `8-4-4-4-12` 字符串（独立格式化函数即使在关闭 `RANDOM` 模块时也可用）。
- **CRC32 与安全清零（始终可用）**：
  - `hx_os_crc32` 计算标准反射 `CRC-32/ISO-HDLC`（多项式 `0xedb88320`，初始种子传 `0`，支持分块增量计算）。内部采用 64 字节只读半字节查表（Nibble Table），零动态分配、无锁（仅用于意外数据损坏校验，非密码学哈希）。
  - `hx_os_memory_secure_zero` 使用 `SecureZeroMemory`（Windows）、`explicit_bzero`（Linux）或 `volatile` 逐字节覆写，防止编译器因死存储优化删掉敏感内存擦除操作。

## 按页内存与文件映射

`HX_OS_ENABLE_VIRTUAL_MEMORY` 默认开启（独立于 `SYSINFO` 和 `FILE_UTILS`），提供匿名页分配与文件内存映射（示例见 [mapped_file.c](../examples/mapped_file.c)）：

- **按页匿名内存（`hx_os_pages_alloc` / `hx::Pages`）**：将请求的正数字节数向上取整到系统页大小，分配初始清零的匿名页。支持 `NONE`、`READ`、`READ|WRITE`、`READ|EXECUTE` 和 `READ|WRITE|EXECUTE` 权限组合（不支持缺少 `READ` 的单独写或执行权限；启用 `EXECUTE` 时会自动同步指令缓存，但若操作系统安全策略禁止可执行内存，则会原样返回错误，详见 `hx_os_pages_protect`）。
- **文件内存映射（`hx_os_file_map_open` / `hx_os_file_map_create` / `hx::MappedFile`）**：
  - 支持一次性映射整个普通文件，或按指定字节偏移量与长度映射子区间（内部自动对齐系统分配粒度，并向调用方返回恰好指向所请求偏移处的视图，且不改变原文件的读写游标）。每个映射对象内部持有独立复制的底层文件句柄，因此创建映射后调用方可以随时关闭原文件句柄。
  - **三种映射模式**：`READ`（只读借用视图）、`SHARED_WRITE`（要求读写文件句柄，修改可写回文件，拒绝 `APPEND` 模式句柄）以及 `PRIVATE_COPY`（写时复制私有页，可基于只读文件句柄创建，修改绝不写回磁盘文件）。
  - **显式刷盘与风险边界**：仅 `SHARED_WRITE` 模式支持调用 `flush`（底层调用 `msync(MS_SYNC)` / `FlushViewOfFile`；当 `durable = 1` 时进一步调用 `fsync` / `FlushFileBuffers`，详见 [Windows 冲刷契约](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile) 与 [Linux `mmap(2)` 手册](https://www.man7.org/linux/man-pages/man2/mmap.2.html)）。注意：销毁映射对象不会隐式自动刷盘；此外，内存映射不是文件快照——若文件在映射期间被外部进程并发截断或遇到底层存储故障，直接解引用映射指针可能触发操作系统级 `SIGBUS` 信号或结构化异常。

## 文件区间锁、时间戳与权限摘要

`HX_OS_ENABLE_FILE_EXTRA` 默认开启，提供字节区间文件锁、纳秒级文件时间戳修改以及所有者权限更新（示例见 `examples/file_attributes.c`）：

- **字节区间锁（`hx_os_file_lock_at` / `hx::FileRangeLock`）**：对正数字节区间加共享锁（需读权限）或排他锁（需写权限），不改变文件游标。支持立即尝试（`HX_OS_DEADLINE_NOW`，冲突时返回 `HX_OS_TIMEOUT`）或带单调截止时间的轮询等待。
  - **重要平台差异**：POSIX `fcntl` 记录锁属于**进程级建议锁（Advisory Lock）**——只要关闭当前进程内指向同一 inode 的**任何一个**文件描述符（包括文件映射关闭时释放的内部副本描述符），该进程在该文件上的所有 `fcntl` 锁都会被内核释放！而 Windows `LockFileEx` 则是**按句柄持有的强制锁**（会拦截冲突的常规读写，但不拦截内存映射视图；此外，由于本库在 Windows 上的定位读写 `read_at`/`write_at` 使用独立的第二个句柄，持锁期间请通过加锁的原文件句柄执行顺序游标读写）。详见 [Linux `fcntl` 记录锁](https://www.man7.org/linux/man-pages/man2/fcntl_locking.2.html) 与 [Windows `LockFileEx` 文档](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)。
- **修改文件时间戳与权限摘要（`hx::FileAttributes`）**：
  - 时间戳结构用 `HX_OS_FILE_TIMES_INIT` 初始化，通过 `fields` 掩码按需选择更新访问时间（atime）或修改时间（mtime），支持指定是否跟随符号链接（POSIX 使用 `utimensat` + `UTIME_OMIT`，Windows 使用 `SetFileTime` 截断至 100 纳秒单位）。
  - 权限更新在 POSIX 上替换文件所有者（Owner）的读/写/执行位并保留 Group/Other 位；在 Windows 上必须包含 `READ`（不支持 `EXECUTE`，`WRITE` 标志仅用于控制文件的只读属性位，不编辑复杂的 NTFS ACL）。

## 目录监听与外部描述符就绪通知

- **跨平台目录变更监听（`HX_OS_ENABLE_WATCH`，默认开启）**：
  - 调用 `hx_os_watch_create(context, path, recursive, callback, user, &watch, &error)`（C++11 为 `hx::DirectoryWatch`，完整示例见 [watch_directory.c](../examples/watch_directory.c)）会启动一个专用的可汇合工作线程，并等待底层订阅就绪后返回。
  - **事件类型与溢出重扫**：回调在工作线程锁外运行，接收借用的相对 UTF-8 路径（以 `/` 分隔，空串代表被监听根目录本身）。事件涵盖 `CHANGED`、`ADDED`、`REMOVED`、`RENAMED`（尽力通知，非事务审计日志）、终止性的 `ERROR` 以及 `OVERFLOW`（当收到 `OVERFLOW` 时，说明内核通知缓冲发生过丢包或 Linux 子目录树完成了拓扑重建，业务应当重新扫描一次目录快照）。
  - **底层机制与资源配额**：默认原生缓冲 `64 KiB`、路径上限 `32 KiB`、软件监听记录上限 `1,024` 个目录、最大深度 `64`、可变内存预算 `4 MiB`。Windows 使用重叠 `ReadDirectoryChangesW`；Linux 使用 `inotify` + `eventfd` 并通过 `openat(..., O_NOFOLLOW)` 与 `/proc/self/fd` 固定目录（不支持缺少 `procfs` 或 `9p` 挂载盘）；Apple 使用纯 C 的 `FSEvents` / `CoreFoundation`（参见 [Apple FSEvents 调度说明](https://developer.apple.com/documentation/coreservices/1447824-fseventstreamschedulewithrunloop)）。
  - **安全关闭与启动失败残留清理**：调用 `hx_os_watch_close_until` 停止并等待线程退出（若在创建阶段启动失败且回滚时工作线程未能立即完成 `join`，创建函数会在返回错误码的同时交付一个非空、已处于停止状态的 `watch` 指针，请对该指针调用 `hx_os_watch_close_until` 完成最终回收）。
- **POSIX 外部文件描述符就绪监听（`HX_OS_ENABLE_LOOP_FD`）**：允许将任意支持轮询的 POSIX 文件描述符附加到 `hx_os_loop` 上监听可读/可写就绪事件（Windows 返回 `HX_OS_NOT_SUPPORTED`）：

```c
hx_os_loop_watch *watch = NULL;

hx_os_status status = hx_os_loop_attach_fd(loop, fd, HX_OS_WAIT_READ,
	ready_callback, user, &watch, &error);
// The callback can use hx_os_loop_watch_descriptor(watch), then request stop.
// Drive loop; finally check hx_os_loop_detach_fd(&watch, &error).
```

<a id="进程输出捕获与系统工具第-18-批"></a>

## 进程、输出捕获与系统工具

`HX_OS_ENABLE_PROCESS` 与 `HX_OS_ENABLE_SYSTEM_UTILS` 默认开启，提供无 Shell 注入风险的子进程启动、标准输出/错误并发捕获以及系统参数/CPU 指令集查询：

- **直接启动子进程（`hx_os_process_spawn` / `hx::Process`）**：
  - 接收 UTF-8 可执行文件路径、不含 `argv[0]` 的参数数组（`argv[0]` 由库自动补入）、可选完整环境变量表以及标准流重定向。**直接启动目标可执行文件，绝不经过 Shell 解析，也不会沿 `PATH` 搜索裸文件名**。参数表与环境表各限 `4 MiB` / `65,536` 项（传入 `NULL` 环境表表示继承当前进程环境，传入 `{NULL}` 表示空环境；在 Windows 上传入显式环境表时不会自动塞入 `SystemRoot` 或 `PATH`，请按子程序需要显式提供）。
  - **启动成功与 POSIX 退出码 `127`**：`spawn` 返回 `HX_OS_OK` 表示子进程已创建，但如 [POSIX `posix_spawn` 规范](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html)所述，部分 `exec` 前的失败（如 Android Bionic 下文件不存在）可能延后体现为子进程以退出码 `127` 结束，因此请同时检查等待结果。
  - **等待与销毁**：`wait_until` 支持单调截止时间（非零退出码也属于等待成功；POSIX 信号终止单独标记，Windows 先检查进程完成状态再读取退出码，因此真实退出码 `259` 不会被误判为仍在运行）。进程仍在运行时调用 `destroy` 会返回 `HX_OS_BUSY`；`close_until` 则先等待退出再销毁。库绝不强杀子进程、不分离句柄，也不篡改 `SIGCHLD`。
- **一键运行并捕获子进程输出（`hx_os_process_capture_run` / `hx::ProcessCapture`）**：
  - 在内部启动子进程（其 `stdin` 设为 `EOF`），并在当前调用线程的私有 `loop` 上并发收集 `stdout` 和 `stderr`，无需启动后台工作线程（默认每路流上限 `1 MiB` 原始字节，完整示例见 [capture_process.c](../examples/capture_process.c)）。
  - **会话保留与超限处理**：一旦子进程成功启动，即便等待中途遇到 `HX_OS_TIMEOUT`、达到输出上限 `HX_OS_LIMIT` 或发生 I/O 错误，函数也会向调用方交付非空的 `capture` 会话对象，供调用方读取已捕获的前缀、调用 `raise_limit` 扩容或调用 `discard` 丢弃后续输出并继续排空，最后调用 `hx_os_process_capture_close_until` 完成清理（若启动阶段本身失败且回滚清理也未完成，同样会返回一个仅用于调用 `close_until` 重试清理的残留会话对象）。
- **命令行快照、主机名与 CPU 特性检测（`HX_OS_ENABLE_SYSTEM_UTILS`）**：
  - `hx_os_process_arguments` 返回包含 `argv[0]` 的原生 UTF-8 命令行参数快照（Windows 读取宽字符命令行，Linux 读取 `/proc/self/cmdline`，macOS 读取原生参数）。同时提供主机名、原生进程 ID、线程安全的环境变量枚举、CPU SIMD 指令集检测（`SSE`/`SSE2`/`SSE4.1`/`AVX`/`AVX2`/`NEON`，其中 `AVX` 同时校验操作系统 `XSAVE`/`XCR0` 状态）、`WOW64` / `Rosetta` 转译状态查询、字节序交换、`udiv64` 以及 POSIX `timespec` 单调/实时时钟转换。

<a id="调度请求与延迟任务第-19-批"></a>

## 调度请求与延迟任务

- **线程优先级与 CPU 亲和性（`HX_OS_ENABLE_THREAD_EX`）**：
  - 支持为指定线程或当前线程请求相对调度优先级（Windows 映射 `idle` / `normal` / `time-critical`；Linux 请求 `SCHED_IDLE` / `SCHED_OTHER` 或最高 `SCHED_RR`；macOS 映射原生低/中/高范围）与 CPU 亲和性掩码（`0` 掩码返回 `HX_OS_INVALID`；Linux 设置后会读回内核实际生效的掩码，若因 `cpuset` 限制导致不一致则返回 `HX_OS_NOT_SUPPORTED` 并尝试恢复旧掩码；macOS 不支持严格核心绑定，固定返回 `HX_OS_NOT_SUPPORTED`）。
- **线程池延迟任务（`HX_OS_ENABLE_DELAYED_TASKS`，默认开启）**：
  - 依赖 `HX_OS_ENABLE_THREAD_POOL`（独立于 `LOOP` 和 `TIMER`）。调用 `hx_os_thread_pool_submit_delayed`（C++11 为 `pool.submitAfter` / `pool.submitAt`，完整示例见 [delayed_task.c](../examples/delayed_task.c)）可提交一个在指定单调时间点（Due Time）之后才允许开始执行的任务：

```c
hx_os_task *task = NULL;

hx_os_status status = hx_os_thread_pool_submit_delayed(pool,
	hx_os_deadline_after_ms(100), work, user, cleanup, &task, &error);
// On acceptance, use the ordinary task wait/cancel/release API.
```

```cpp
#include <hx/ThreadPool.hpp>

hx::Result<hx::Task<int>> delayed_answer(hx::ThreadPool &pool) noexcept
{
	return pool.submitAfter(std::chrono::milliseconds(100),
		[](int value) noexcept { return value * 2; }, 21);
}
```

有序未来任务链表与普通就绪队列共享线程池容量配额，直接由池内现有工作线程通过条件变量定时等待，不额外创建定时器管理线程。若对尚未到期的延迟任务发起取消（`cancel`），该任务会被立即移入就绪队列并由工作线程在锁外执行一次 `cleanup`，无需傻等到原定到期时间。

<a id="本地套接字与双向文件端点第-20-批"></a>

## 本地套接字与双向文件端点

- **本地路径流套接字（`HX_OS_ENABLE_LOCAL_SOCKETS`，默认开启）**：
  - 依赖 `HX_OS_ENABLE_NETWORK`。使用 `HX_OS_LOCAL` / `HX_OS_LOCAL_STREAM`（即 `AF_UNIX` 流套接字，路径为含末尾 `NUL` 在内不超过 `108` 字节的严格 UTF-8 文件路径，支持 Linux、macOS 及 Windows 10+ 原生 `AF_UNIX` 提供程序；Windows 7 返回 `HX_OS_NOT_SUPPORTED`）。
  - `hx_os_socket_local_listen` 一步完成创建、绑定与监听（不会自动删除已有同名文件，关闭监听套接字后也需由调用方自己删除套接字路径文件）。`hx_os_socket_local_connect` 支持带截止时间的连接与超时后的 `finish_connect` 续连。已建立连接的本地流套接字可直接复用 `send`、`receive`、`hx_os_socket_receive_until` 以及事件循环 `loop` 的异步读写（注：由于历史 IP 完成结构放不下长路径，本地套接字的 `accept` 与 `connect` 请直接调用带截止时间的 `accept_until` / `connect_until`，不要提交给 `loop` 的 `ACCEPT`/`CONNECT`）。
- **免网络初始化的全双工文件端点对（`HX_OS_ENABLE_DUPLEX_PIPE`，默认开启）**：
  - 独立于 `NETWORK`、`PIPE` 和 `LOOP`。调用 `hx_os_pipe_pair_socket`（C++11 为 `hx::DuplexPipe::pair`，完整示例见 [duplex_message.c](../examples/duplex_message.c)）返回一对各自可读可写的同步 `hx_os_file` 端点（POSIX 底层使用 `socketpair(AF_UNIX, SOCK_STREAM)`，Windows 底层使用带严格本机回环地址校验的私有 IPv4 TCP Loopback 配对并自动管理独立的 Winsock 引用计数）：

```c
hx_os_file *first = NULL, *second = NULL;

hx_os_status status = hx_os_pipe_pair_socket(NULL, &first, &second, &error);
```

```cpp
#include <hx/Local.hpp>

hx::Result<std::pair<hx::File, hx::File>> make_channel() noexcept
{
	auto channel = hx::DuplexPipe::pair();
	if (!channel)
		return hx::Failure(channel.error());

	auto written = channel.value().first.writeAll("hello", 5);
	if (!written)
		return hx::Failure(written.error());

	auto stopped = hx::DuplexPipe::shutdown(channel.value().first);
	if (!stopped)
		return hx::Failure(stopped.error());

	return channel;
}
```

同一个端点支持一个读线程与一个写线程全双工并行；调用 `shutdown` 可单向关闭写端（向对端发送 `EOF`）同时继续保留读端。

<a id="注册表电源请求和字符串混淆第-21-批"></a>

## 注册表、电源请求和字符串混淆

- **Windows 注册表读写（`HX_OS_ENABLE_REGISTRY`）**：
  - `hx_os_registry_open_user` 打开 `HKCU` 下的反斜杠相对路径，`hx_os_registry_read_string` 直接输出校验过 UTF-8 的 `hx_string`（字面读取 `REG_SZ` / `REG_EXPAND_SZ`，绝不隐式展开 `%环境变量%`，单值上限 `1 MiB`）。创建键、写权限与 32/64 位 WOW64 注册表视图均须显式指定；非 Windows 平台返回 `HX_OS_NOT_SUPPORTED`（C++11 为 `hx::Registry`）。
- **防睡眠与性能活动请求（`HX_OS_ENABLE_SLEEP_INHIBIT` / `HX_OS_ENABLE_PERFORMANCE_REQUEST`）**：
  - 调用 `hx_os_sleep_inhibit_create(NULL, "Exporting project", &request, &error)`（C++11 为 `hx::SleepInhibit`）创建按句柄独立隔离的防系统休眠请求，销毁或停用该对象只会撤销其自身持有的请求，绝不干扰同一进程内其他模块的防睡眠状态。
  - **各平台后端**：Windows 使用按句柄隔离的 `PowerCreateRequest` + `PowerRequestSystemRequired`（Windows 8+ 性能请求使用 `PowerRequestExecutionRequired`）；macOS 防睡眠使用纯 C 的 `IOKit` 断言，性能请求使用 `NSProcessInfo`（关闭 `HX_OS_ENABLE_PERFORMANCE_REQUEST` 即可完全移除 Objective-C / Foundation 依赖）；Linux 桌面防睡眠通过可选的 `sd-bus` 会话总线适配实现（详见[Linux 桌面](LINUX_DESKTOP.zh-CN.md)）。
- **编译期/构建期字符串可逆混淆（`HX_OS_ENABLE_OBFUSCATION`）**：
  - 使用开发脚本 `python tools/obfuscate_string.py --name app_label --text "example"` 生成预编码的 C99 数组，运行时调用 `hx_os_obfuscate_string(&app_label, &text, &error)`（或 C++11 编译期 `constexpr auto text = hx::obfuscate<0x5a>("example")`）通过可逆 XOR 还原明文（示例见 `examples/obfuscated_string.c`）。注意：**可逆混淆仅用于防止二进制文件中直接裸露明文字面量，不是密码学加密，切勿用于存储密钥或密码**。

<a id="日期翻译表和位流第-22-批"></a>

## 日期、翻译表和位流

- **跨平台 UTC 日历格式化与本地时区偏移（`HX_OS_ENABLE_CALENDAR`）**：
  - `hx_os_wall_time_format_utc_string(milliseconds, NULL, &text, &error)`（C++11 为 `hx::Calendar::utc`）将 Unix 毫秒时间戳（向负无穷取整到秒）按公历规则格式化为覆盖 `0001–9999` 年的 UTF-8 字符串，不受 32 位系统 `time_t` 2038 年上限限制。模式串支持 `%CCYY`、`%MM`、`%DD`、`%hh`、`%mm`、`%ss` 和 `%%`（传 `NULL` 默认输出 ISO 8601 格式 `%CCYY-%MM-%DDT%hh:%mm:%ssZ`）。`hx_os_wall_time_local_offset_minutes` 则查询当前本地时区相对 UTC 向东的分钟偏移与夏令时（DST）标志。
- **轻量级内存翻译表（`HX_OS_ENABLE_TEXT_LOOKUP`）**：
  - `hx_os_text_lookup_create`（C++11 为 `hx::TextLookup`，示例见 `examples/localized_time.c`）创建一个基于有序数组二分查找的独立键值翻译目录（默认上限 `4,096` 词条、总文本 `1 MiB`）。调用 `get_string` 查询时，仅当键完全不存在时才回退返回传入的原文；构建完成后支持多线程无锁并发只读查询。
- **大端序按位读写器（`HX_OS_ENABLE_BITSTREAM`）**：
  - `hx_os_bit_reader` 与 `hx_os_bit_writer`（C++11 为 `hx::BitReader` / `hx::BitWriter`）是纯栈上结构体，直接借用调用方传入的字节缓冲区（初始化长度按**字节**计，`read_bits` / `write_bits` 每次读写 `0..64` **比特**）。在所有 CPU 架构上统一采用字节内与数值内最高有效位优先（MSB-first）编码，零内存分配、零系统调用。读越界返回 `HX_OS_EOF`，写越界返回 `HX_OS_OUTPUT_FULL`，数值超出所给位宽返回 `HX_OS_RANGE`，且失败时不移动位游标。

<a id="同步信号和栈上参数包第-23a-批"></a>

## 同步信号和栈上参数包

`HX_OS_ENABLE_SIGNALS` 默认开启，提供零后台线程的观察者信号分发器（`hx_os_signal_handler` / `hx::SignalHandler`）与固定布局的栈上命名参数包（`hx_os_calldata` / `hx::CallData`，完整示例见 `examples/signal_notifications.c`）：

- **栈上命名参数包（`hx_os_calldata`）**：固定包含 `16` 个键值槽位与 `512` 字节的共享名称/字符串存储区（全程零堆内存分配，且内部采用相对偏移量存储，因此整个结构体按值拷贝依然安全有效）。支持读写 `bool`、`int64_t`、`double`、UTF-8 字符串与借用裸指针，也可使用 `hx_os_calldata_edit_*` / `CallData::edit()` 链式连续赋值并在最后统一检查状态码。
- **同步信号连接与触发（`connect` / `emit`）**：
  - 调用 `emit` 时，分发器会在当前触发线程、内部锁外按注册顺序同步调用匹配的槽函数（如果其中某个回调返回非 `HX_OS_OK` 错误，不会阻止后续已匹配的回调继续执行，最终 `emit` 返回第一个遇到的错误状态码）。
  - **连接代次与安全断开**：每个连接 ID 包含代次号（Generation），防止槽位复用后旧 ID 误删新连接。`disconnect` 立即阻止后续新触发；`disconnect_until` 还会等待当前正在执行中的该回调及其 `cleanup` 彻底返回（在回调内部同步等待自身或外层嵌套祖先会返回 `HX_OS_DEADLOCK`）。卸载插件前，请务必先通过 `disconnect_until` 或 `close_until` 退役插件注册的所有回调。

<a id="可变设置与保留快照第-23b-批"></a>

## 可变设置与保留快照

`HX_OS_ENABLE_SETTINGS` 默认开启，提供支持多线程并发读写的分层键值设置中心（`hx_settings` / `hx::Settings`）与不可变引用计数快照（`hx_setting_value` / `hx::SettingValue`，完整示例见 `examples/settings.c`）：

- **显式值与默认值双层隔离**：通过 `set_*` 写入显式值，通过 `set_default_*` 配置默认值。读取时**仅当键在显式层中完全不存在时才回退默认值**（显式层中的 `null` 或类型不匹配不会回退默认值）。键名按字面字节精确匹配（点号 `.` 只是普通字符，不自动拆分为嵌套路径）。
- **写时复制不可变根与并发快照**：支持 `null`、`bool`、`int64`、有限 `double`、UTF-8 字符串、数组、对象以及借用用户指针。底层采用不可变树根（Immutable Root）实现：标量读取仅经过极短的互斥区获取根引用，零内存分配；写入操作在锁外准备好新节点并校验版本号（默认最多重试 8 次，并发争用耗尽时返回 `HX_OS_BUSY`，绝不静默覆盖他人更新）。已取出的快照或零拷贝字符串引用（`hx_settings_get_string_ref` / `Settings::stringView`）在设置中心继续更新甚至关闭后依然合法有效。
- **同步变更观察者与配置导入导出**：创建时可注册在锁外同步执行的变更观察者回调。当同时启用 `HX_OS_ENABLE_CONFIG` 与 `HX_OS_ENABLE_CONFIG_SERIALIZE` 时，还可通过 `to_config`、`to_json`、`load_json` 和 `save_json` 与配置文件无缝互转（批量连续更新可使用 `hx_settings_update_*` / `Settings::update`，详见[常用调用所有权](COMMON_UTILITIES.zh-CN.md)）。

<a id="有界区间计时树第-23c-批"></a>

## 有界区间计时树

`HX_OS_ENABLE_PROFILER` 默认开启，提供无需后台线程、无需捕获原生调用栈的显式层级区间计时器（`hx_os_profiler` / `hx::Profiler` 及 RAII 作用域守卫 `hx::ProfileScope`，基础示例见 `examples/profile_intervals.c`）。如果你还需要获取独立持有的结构化统计快照、均值/标准差以及有界对数直方图分位数（P50 / P95 / P99），请参阅专门的 [Profiler 教程](PROFILER.zh-CN.md)：

- **配对计时与独占耗时扣除**：进入根区间调用 `start_root`，进入子区间调用 `reach`，退出调用 `leave`（每次成功进入必须在同一线程或协程上配对一次 `leave`）。已完成子区间的耗时会自动从父区间的“独占耗时（Exclusive Duration）”中扣除；不同线程下相同父路径与名称的区间会自动合并统计。
- **预热零分配与安全关闭**：默认支持最多 `1,024` 条路径记录、`64` 个线程/纤程局部栈缓存和 `32` 层嵌套。路径与线程缓存预热后，重复进入/离开不再触发动态内存分配。调用 `close` 时若当前线程仍有未退出的活动区间，会明确返回 `HX_OS_DEADLOCK`；对于长期存活的外部线程，在卸载插件或销毁 Profiler 前请先调用 `restart_thread` 释放其线程局部缓存。
