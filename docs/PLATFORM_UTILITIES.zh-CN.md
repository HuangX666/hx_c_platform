# 平台便利功能

[English](PLATFORM_UTILITIES.md)

常用操作提供直接可用的 C 入口，可选 C++11 头文件封装同一份实现。
NULL context 使用现有不可变默认分配器。关闭功能时保留声明和失败桩，
对应能力位清零。

## 系统信息

`hx_os_sysinfo.h` 提供内存字节数、CPU 拓扑、页大小、文件系统空间、
当前进程内存和系统版本。`HX_OS_ENABLE_SYSINFO` 默认 ON；关闭后，
库版本和 ABI 查询仍然可用。这些操作不启动线程、安装处理器或修改进程状态。

```c
hx_string version = HX_STRING_INIT;
hx_os_error error;
hx_os_status status = hx_os_sys_version_string(&version, &error);
/* Check status, then use hx_string_c_str(&version). */
hx_string_destroy(&version);
```

带版本的磁盘／进程结果结构使用对应的 `*_INFO_INIT` 宏初始化。
缓冲区文本接口返回的所需容量包含 NUL。标量和结构化输出在失败时保持不变。

CPU 数量表示系统在线 CPU，Windows 包含全部处理器组，不等同于进程亲和性
掩码或 cgroup 配额。Linux 通过 sysfs 去重在线 package/core 配对；
拓扑不可取得时返回 NOT_SUPPORTED，不用逻辑核心数冒充物理核心数。

可用内存分别采用 Windows 可用物理内存、Linux `MemAvailable`、macOS
空闲页加非活跃页。macOS 的值为估计值，各平台快照都不保证随后分配成功。
Linux 数据反映操作系统视图，不等同于容器专属内存上限。
POSIX 磁盘可用空间使用 `f_bavail`，与所有空闲块 `f_bfree` 分开。

当前进程驻留字节表示工作集／RSS；虚拟字节分别采用 Windows 已保留和已提交
虚拟地址空间、Linux `/proc/self/statm` 大小、macOS Mach virtual size。
它不表示提交费用或物理内存消耗。原生资源查询可能阻塞。

`hx_os_cpu_usage_create` 保存进程 CPU 与单调时钟起点。每次成功查询返回
上次采样以来的平均值：
`100 * process_cpu_delta / elapsed_delta / initial_logical_cpu_count`。
不计入子进程。每个采样器需由调用者串行使用，采样器彼此独立，并使用自己的
context 分配内存。间隔为零时返回 WOULD_BLOCK，保留原采样。
计数器精度及 CPU 拓扑变化可能影响短间隔结果；库不强行截断百分比。

```cpp
#include <hx/SystemInfo.hpp>
auto version = hx::SystemInfo::version();
auto space = hx::SystemInfo::diskSpace(".");
hx::CpuUsage cpu;
/* Do work or wait before sampling. */
double percent = cpu.query();
```

C++ 便利方法抛出现有 `hx::StatusError`，保留其原生错误字段。
`CpuUsage` 持有采样器并支持移动。这些头文件要求 C++11，底层库仍为 C99。
独立 C 示例为 `examples/system_info.c`。

## 动态库

`HX_OS_ENABLE_LIBRARY` 默认 ON。`hx_os_library_open`、`symbol`、`close`
提供基础 C 入口；`_ex` 版本可以接收已初始化的 `hx_string`，保存属于调用者
的诊断文本。`hx::Library` 提供可移动的所有权和 `symbol<Pointer>(name)`，
失败时抛出持有错误文本和原状态／错误域／代码的 `LibraryError`。

```cpp
#include <hx/Library.hpp>
hx::Library plugin("./plugin.so");
using Abi = uint32_t(HX_OS_CALL *)(void);
auto abi = plugin.symbol<Abi>("hx_plugin_abi_version");
// Check ABI before calling the rest of the plugin interface.
```

输入是 UTF-8 文件系统路径，相对文件名按当前目录解析，不按裸 POSIX soname
搜索；被加载模块的依赖仍遵循原生加载器规则。POSIX 使用 RTLD_NOW 和
LOCAL/GLOBAL，Windows 忽略可见性选项。库不修改加载器搜索路径。
POSIX 加载错误没有可移植的数值代码，因此主错误使用 HX_OS_ERROR_LOADER，
可选诊断保存文本。在调用可能重入加载器的分配器前，先复制最多 2047 字节，
必要时标记截断，并转义无效 UTF-8 字节。诊断分配失败不覆盖原始操作错误。

每个句柄的使用与关闭由调用者串行协调。符号借用模块存储；POSIX 数据符号
地址即使为 NULL，也可能成功。卸载前必须结束调用、释放插件对象、断开回调，
并完成插件 TLS 析构。关闭失败也消耗句柄；C++ 显式 close 可报告错误，
析构不抛异常。加载／卸载可能执行模块构造／析构代码，因此可能阻塞。

`examples/load_plugin.c` 加载测试模块，使用基础库 ABI 查询协商版本，
再读取插件版本文本。CTest 自行构建配套模块，不依赖预装插件或外部进程。

C 调用者可用 `hx_os_library_bind` 或 `_bind_ex`，传入函数指针变量的地址
和 `sizeof(variable)`，直接取得可调用的指针，不必重复编写转换代码。
失败时目标变量保持不变。

## 文件与路径便利操作

`HX_OS_ENABLE_FILE_UTILS` 默认 ON。用
`hx_os_file_read_text(path, &text, &error)` 直接读取文本，使用
`hx_os_file_write_text(path, &text, &error)` 发布已有 `hx_string`。
默认严格校验 UTF-8、移除开头的 UTF-8 BOM，并限制整个输入为 16 MiB；
显式 load 接口可调整上限和 BOM 策略。无效编码、超限、分配或 I/O 失败均保留
已有输出字符串。二进制读取交付调用者持有的内存；空文件同样交付可释放的空
分配，统一用 `hx_os_memory_free` 释放。

原子保存依次在目标目录独占创建临时文件、完整写入、请求文件同步、关闭，
再重命名覆盖目标。它替换目录项，包括目标链接，可能改变文件标识和元数据。
扩展结果区分文件发布与父目录同步；发布成功不保证掉电后仍然存在。
Windows 的父目录同步在扩展结果中报告 NOT_SUPPORTED，不把已经完成的发布
改报为失败。发布前失败会尽量删除临时文件；权限或外部修改可能阻止清理。

复制会拒绝覆盖同一文件对象，不承诺元数据保留，也不提供并发变化源文件的
一致快照。普通保存／复制失败可能留下部分输出。显式备份是复制，不承诺
持久化。带备份替换在 Windows 使用原生操作，POSIX 使用两次重命名。
已有备份名称会被拒绝，调用者负责排除参与路径的并发修改；后续步骤失败没有
自动回滚。POSIX 在冲突检查后使用普通 rename，也适用于不支持
no-replace rename 的 WSL 挂载盘。

当前／绝对路径都有缓冲区和 `hx_string` 入口。绝对路径要求目标存在并解析
链接。切换 cwd 明确影响整个进程，调用者需协调相对路径操作。
basename／dirname／extension 为无分配的借用视图，两种分隔符都识别，
关闭 FILE_UTILS 后仍可使用。扩展名包含点，文件开头唯一的点不算扩展名。

glob 返回持有 `hx_string` 元素的 vector，用 `hx_os_sequence_destroy`
释放。支持 UTF-8 `*`／`?`，后者匹配一个码点；参与匹配的组件区分大小写，
字面目录前缀仍按原生文件系统查找。不提供字符类、转义或 `**` 递归展开。
点开头的名称要求模式显式以点开头。无匹配返回空结果，顺序不保证。
该功能同时依赖 CONTAINERS 与 FILE_UTILS。

遍历默认深度 64（可配置上限 256）、最多访问一百万项、最多 65,536 个匹配，
vector 存储和保留文本各限 16 MiB。遍历帧同样受限，原生目录缓冲不计入
分配器预算。递归删除不跟随链接，使用堆上的遍历帧；超限或失败可能已经删除
部分条目。POSIX 相对目录描述符并用 NOFOLLOW 打开子目录；Windows 打开
重解析点本身，遍历期间保持目录句柄以阻止重命名。外部并发修改仍需调用者协调。

```cpp
#include <hx/Files.hpp>
auto text = hx::Files::readText("settings.json");
hx::Files::writeText("settings.json", text);
hx::File file("input.bin");
hx::Directory directory(".");
std::string name;
while (directory.next(name)) {
    // Use the owned UTF-8 name.
}
```

`File`／`Directory` 独占持有基础句柄，关闭 FILE_UTILS 后仍然可用。
`File::get()` 借出句柄；位置相关操作由调用者串行使用。
`readSome` 区分 EOF 与零字节请求，`IoError::completed()` 保留失败前的
字节进度。`Files` 封装可选便利层，返回标准 C++ 字符串和 vector。

`examples/text_file.c` 一次调用读取 UTF-8，并可原子发布到第二个路径；
CTest 在两个平台运行同一份 C 源码。

## 环境变量

`HX_OS_ENABLE_ENV` 默认 ON。`hx_os_env_get_string` 使用输出字符串的
context 返回拥有所有权的 UTF-8，`hx::Environment::get` 返回 std::string。
`getOr` 仅在变量不存在时使用默认值，空值仍然返回空值。
缓冲区接口所需容量包含 NUL。

`hx_os_env_set(name, value, replace, error)` 明确修改当前进程环境；
replace=0 保留已有值，包括空值。删除不存在的变量成功。
输入仅在调用期间借用，名称不得为空或含 '='，无效 UTF-8 会被拒绝。
操作系统复制设置值，修改影响以后启动的子进程，不影响父进程或已运行子进程。

同一份已加载库内的调用互相同步；原生 API、CRT 或另一份库实例的修改仍需
宿主协调。输出分配器回调在锁外执行，允许再次调用读取接口。
字符串容量最多重试八次；值持续增长时返回 BUSY 并保留原输出。
Windows 使用 Win32 Unicode 环境块，CRT 的 `getenv`／`_environ` 缓存
独立，可能看不到 Win32 修改，应成组使用本库读写接口。
POSIX 原生值不是 UTF-8 时返回 INVALID_ENCODING，不修改 locale 或猜测编码。

```cpp
#include <hx/Environment.hpp>
auto value = hx::Environment::getOr("APP_MODE", "normal");
hx::Environment::set("APP_MODE", "diagnostic");
```

`examples/environment.c` 直接查询指定变量。CTest 只在测试子进程环境中
注入专用测试值，不修改宿主设置。

## 常用目录

PATHS 默认开启。C 直接调用 `hx_os_path_config_home_string(&path, &error)`，
C++ 调用 `hx::Paths::configHome()`。七个查询均有缓冲区和 `hx_string` 版本，
无需全局初始化，不创建目录、不修改工作目录。返回绝对 UTF-8 路径，根目录以外
不保留尾分隔符；路径不保证存在或可写。所需容量含 NUL；失败保留字符串原值，
内存使用输出字符串自己的 context。

| 查询 | Windows | Linux | macOS |
| --- | --- | --- | --- |
| home | 绝对 USERPROFILE，否则 Profile 已知目录 | 绝对 HOME，否则当前 UID 数据库 | 相同 POSIX 查询 |
| config_home | RoamingAppData | 绝对 XDG_CONFIG_HOME，否则 HOME/.config | HOME/Library/Application Support |
| data_home | LocalAppData | 绝对 XDG_DATA_HOME，否则 HOME/.local/share | HOME/Library/Application Support |
| program_data | ProgramData | /var/lib | /Library/Application Support |
| temp | GetTempPathW | 绝对 TMPDIR，否则 /tmp | 绝对 TMPDIR，否则 Darwin 用户临时目录，再回退 /tmp |
| executable | GetModuleFileNameW | /proc/self/exe | _NSGetExecutablePath + realpath |
| module | 本库所在目录 | dladdr 返回绝对名称时的目录 | 同左 |

`program_data` 表示机器共享的应用数据和状态，与用户数据、只读安装资源分开。
消费者自行添加应用子目录，并明确决定何时创建。[XDG 规范](https://specifications.freedesktop.org/basedir/latest/)
要求忽略空值及相对路径；无效 UTF-8 仍报错。Windows 使用 SHGetKnownFolderPath、
KF_FLAG_DONT_VERIFY 和 CoTaskMemFree，测试进程没有调用 CoInitialize。
不引入 Objective-C 或新的第三方依赖。

静态链接时 `module` 指可执行文件所在目录。POSIX 加载器若仅给出相对模块名称，
返回 NOT_SUPPORTED，避免工作目录改变后猜错位置。Linux 可执行路径查询依赖
procfs，并保留原生失败；已移动或删除的镜像可能返回系统记录的原名称，不保证
路径仍存在。POSIX 原生路径临时容量上限为 1 MiB，目录服务及 UID 查询可能阻塞。

PATHS 独立于 ENV、FILE_UTILS、LIBRARY 开关。内部环境读取共用锁和编码处理，
关闭 ENV 后其公共入口仍报不支持。宿主协调原生环境或 cwd 的并发修改。没有隐藏
线程，不影响已有热路径；显式查询会分配临时字符串及原生缓冲区。
`examples/special_paths.c` 在 Windows/Linux 使用同一份 C 源码直接调用。

## 线程选项、命名与信号量

THREAD_EX 默认开启，不自动启动线程。使用 `HX_OS_THREAD_OPTIONS_INIT` 后设置
以字节表示的栈预留大小，传给 `hx_os_thread_create_with_options`；零保持原生
默认值。显式初始化函数接收调用方容量，只写已知前缀，创建先验证 size/version。
POSIX 按原生最小值和对齐要求拒绝无效栈大小。Windows 保留 `_beginthreadex` 的
CRT 清理，配合 STACK_SIZE_PARAM_IS_A_RESERVATION；超出 unsigned 参数范围报
RANGE。原有 create 保持默认原生路径，不增加分配或锁。

名称严格使用 UTF-8。Linux/macOS 分别在 15/63 字节内截断到完整码点；macOS
只能给当前线程命名。Windows 从 KernelBase 动态查找 SetThreadDescription，
兼容较早 Windows 10 的导出位置；Win7 等不可用环境返回 NOT_SUPPORTED。原生
失败保留 errno/HRESULT。对对象命名时由调用方排除并发 join/destroy，已 join
线程报 CLOSED。

信号量用互斥锁和条件变量实现有界计数。post(count>0) 无等待者时仍保留通知，
超过上限整体报 LIMIT，不部分增加。wait_until 使用单调绝对截止时间；NOW 时
有令牌立即消费，无令牌报 TIMEOUT；允许显式 INFINITE。超时保留对象可重试，
不承诺 FIFO 或硬截止时间。销毁前排除所有操作及等待者。创建为三次 context
分配，post/wait 不分配、各持有一次互斥锁，不建立线程或队列。

`hx::Semaphore` 提供可移动 RAII；`hx::Thread` 沿用 ThreadPool 的显式所有者
变量封装：closeUntil 成功后 join 并销毁，超时保留所有者，析构不等待、不终止。
回调数据借用至 join 完成。`examples/thread_options.c` 演示栈配置及明确的关闭
策略；嵌入式小栈还须计入回调、TLS 和 sanitizer 的额外开销。

## 管道与本地 I/O 循环

`hx_os_pipe_pair_anonymous` 返回两个同步文件端点，直接使用文件读写和关闭入口。
两平台均为阻塞字节流，不创建等待线程；磁盘专用操作返回 NOT_SUPPORTED。零字节
操作成功；全部写端关闭后，读完缓冲数据才返回 EOF。断开后写入返回 CLOSED，保留
原生错误。关闭不能与同步读写竞争。Linux 使用带 CLOEXEC 的 pipe2；macOS 通过
fcntl 设置 CLOEXEC，创建期间宿主须协调并发 fork/exec。

需要截止时间、取消时，使用 `hx_os_pipe_pair_async` 和 read_async/write_async。
默认写入复制缓冲区并完成全部字节，扩展入口可借用；读取返回当前可读字节或 EOF。
新的管道操作结构按容量初始化，不改变 hx_os_operation。内容保持原始字节，
不自动解码。`hx_os_pipe_pair_child` 支持两个方向的“异步父端、同步子端”，
初始均不可继承；后续进程创建明确选择复制给子进程的端点。

`hx_os_loop_create_local(NULL, NULL, &loop, &error)` 无需网络对象或网络初始化。
支持投递、定时器与本地 I/O，网络和 DNS 操作明确报不支持。原网络工厂继续可用。
LOOP 和 NETWORK 可独立裁剪；PIPE 开启而 LOOP 关闭时仍提供同步端点，
ASYNC_PIPE 能力需要两者同时开启。

每个端点同一时间接受一个请求，由 loop 创建线程提交。管道共享 loop 的请求数与
待处理字节预算，借用缓冲区也计入字节预算。拒绝不持有资源、不触发回调；接受后
在轮询线程回调一次，不在提交内联调用。request 等待包括回调返回，但不代替轮询。
POSIX 使用非阻塞描述符及与套接字共用的 epoll/kqueue 注册。Windows 使用私有
单实例字节模式命名管道、重叠端点和 IOCP；CreatePipe 仅用于同步端点。
不承诺硬截止时间。

close_until 禁止新请求，发起取消并驱动原生操作退出。超时保留指针、原生状态和
缓冲区供重试；期间可能执行其他 loop 回调。回调等待同 loop 未完成操作或重入
正在进行的关闭，报 DEADLOCK。销毁 loop 前须关闭附属管道。最终原生 close 即使
报错也消费端点，不能对已消费指针重试。关闭 loop 会取消请求，不隐式释放端点。

POSIX 写管道仅临时阻塞调用线程的 SIGPIPE，保留已有待决信号、消费本次断开写入
新产生的信号，然后恢复原掩码。不安装进程级信号处理器。宿主不能用其他信号等待
机制并发消费该线程的待决 SIGPIPE。失败或取消报告原生提供的部分字节进度，
不代表对端一定没有收到数据。

C++11 的 `hx::Pipes::anonymous()` 返回两个 RAII File。`IoLoop`、`AsyncPipe`
保留显式 C 所有者变量以支持关闭重试；`IoRequest` 持有请求引用，析构不隐式取消，
也不替用户持有借用的读缓冲区或引用捕获。lambda 复制到保留状态；回调异常在 C
边界捕获，完成后可调用 rethrowCallbackError 检查。回调及捕获使用 C++ 分配器，
不计入 C 缓冲区预算；接受请求数量仍限制其数量。C 入口不增加这些分配。

`examples/pipe_transfer.c` 完成一次本地异步传输和明确关闭。已有普通文件 I/O
只增加类型判断，不增加锁或分配；套接字 readiness 改为共用的两个 int 记录，
不增加其分配或系统调用。异步管道复用请求分配及队列锁；默认写另有数据副本。
POSIX 写管道额外执行线程信号掩码和待决信号查询。
