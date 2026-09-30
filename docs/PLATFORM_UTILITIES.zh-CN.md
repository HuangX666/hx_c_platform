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

## 套接字选项与定位文件 I/O

NET_OPTIONS 默认开启，能力位同时依赖 NETWORK。直接调用
hx_os_socket_set_nodelay、set_keepalive、set_reuseaddr，库内部选择原生层级和
名称。地址复用保持显式开启，create/bind 不改变默认值。原生地址复用行为和
keepalive 时间安排仍遵守各系统契约。套接字一旦附属 loop，setter 即报 BUSY，
即使当前无请求也不能设置。`hx::SocketOptions` 是借用外观，提供这些设置和
peerAddress()，不关闭对象。

原始 option_set/get 使用目标 SDK 的 level/name/值布局，属于平台扩展入口。
长度以字节计，最大 INT_MAX；get 成功才更新实际长度，不提供两段式容量查询。
失败保留输入长度，但原生缓冲区内容可能已变。本机 Windows 提供者的 TCP_NODELAY
在 int 大小缓冲区中返回 1 字节，调用方应检查实际长度。C++ get<T> 要求原生结果
大小与类型完全匹配，否则报 RANGE，不推测任意原生选项的类型。对端查询失败保留
输出，未连接时保留 getpeername 原生错误。与套接字操作串行并排除并发关闭。

POSITIONED_IO 独立于 FILE_UTILS。read_at/write_at 接收绝对字节偏移，返回一次
实际传输的字节数。短读写成功、EOF 和错误分别表示；请求零字节成功。单次最多
INT_MAX 字节，偏移及本次范围必须适配有符号 64 位原生位置。后续分块和重试由
调用方决定。追加模式定位写报 NOT_SUPPORTED，打开模式不允许访问时报
ACCESS_DENIED，管道不支持定位。调用可能阻塞，不提供取消或截止时间保证。

POSIX 使用 pread/pwrite 并重试 EINTR。Windows 首次定位操作通过 ReOpenFile
重新打开同一文件对象的重叠句柄，以原生 interlocked 安全发布缓存；每次调用
使用独立事件和 OVERLAPPED。不会定位再恢复共用游标，也不会扩大原打开权限。
对象由调用方保留到所有操作完成，关闭/截断须外部排除，重叠写入范围自行协调。
文件最终关闭会释放两个句柄。C++ File::readAt 区分 EOF，writeAt 返回部分进度，
失败通过 IoError 保留实际进度。

普通文件打开和顺序读写不为定位功能增加分配或锁。Windows 内部文件对象 x64
变为 24 字节、x86 为 16 字节，POSIX 为 12 字节。首次并发调用可能创建竞争的
候选句柄，最终只保留已发布的一个。Windows 每次定位调用增加原子缓存查询、
事件创建/关闭、原生 I/O 及必要等待；POSIX 仅定位系统调用。这是成本清单，
不作为吞吐提升证据。

`examples/positioned_read.c` 无需 seek 即读取偏移 4 起的四个字节。同时修正了
Windows 管道 ReadFile/WriteFile 的参数，提供 Win7 文档要求的非空、随请求保留
的字节数存储；异步最终进度仍取自 IOCP 完成结果。Win7 实机执行仍待验证。

## 文件属性、目录项类型与 BOM

FILE_INFO 默认开启，提供 hx_os_path_stat_extended 和 hx_os_directory_next_info。
扩展输出使用 HX_OS_FILE_INFO_EXTENDED_INIT 或带容量的初始化函数。既有文件
信息结构及目录 next 签名保持不变；扩展查询失败保留输出，成功仅写已知结构大小。
跟随链接的选择显式传入，名称继续使用 UTF-8；原生属性投影与旧 stat 共用。

时间为有符号 Unix 秒加 [0, 1e9) 范围的纳秒。元数据变更时间使用 POSIX ctime 或
Windows FILE_BASIC_INFO.ChangeTime，不拿创建时间替代。精度、延迟更新和访问
时间关闭策略由文件系统决定。Windows 两次元数据查询持有同一个文件句柄，仍不
保证并发修改期间所有字段来自同一时刻。read_only 取 Windows 属性或 POSIX
owner-write 模式位缺失。POSIX 标记 owner-read/write 模式摘要有效；Windows
两者均标为未知，不推测 ACL。摘要不能充当访问鉴权，也不表示挂载只读；Windows
的只读属性不对目录实施写保护。

目录类型针对条目本身，不跟随链接。Windows 重解析点报告 LINK；POSIX 优先
使用 d_type，未知时通过已打开目录执行相对 fstatat。并发删除可能造成原生查询
失败；容量不足及原生属性查询失败保留待返回条目和 kind 输出，可重试或调用旧
next 跳过过期条目。无效 UTF-8 沿用既有跳过并报错行为。不承诺枚举顺序和快照
一致性。旧 next 不新增类型查询系统调用。C++ Files::info、Directory::next(name,
kind) 直接返回结果；examples/file_info.c 展示一次调用查询属性。

BOM 为独立默认开启的纯工具。hx_os_text_bom_detect 识别完整前缀中的 UTF-8、
UTF-16LE/BE、UTF-32LE/BE 标记，优先检查 UTF-32。无标记报 NOT_FOUND，前缀长度
为零，编码输出保持原值。它不做流式编码猜测：仅有 FF FE 两字节时是完整 UTF-16LE
标记；识别可能的 UTF-32 流应先取得四字节。检测不验证或转换正文。
hx_os_text_bom_bytes 最多生成四字节、不加终止符，无受支持 BOM 的编码报不支持。
C++ Text::detectBom 返回 found/bytes/encoding，encoding 仅在 found 时有效；生成
入口返回拥有内存的字节 vector。原有编解码器仍不隐式增删 BOM。

C BOM 函数不分配、不加锁；属性查询不建立后台任务，但原生查询可能阻塞。POSIX
已知目录类型不调用 stat，未知时增加一次 fstatat。Windows 扩展 stat 在共用句柄
及基础投影的基础上增加一次 FileBasicInfo 查询。

## 配置序列化与数字文本

CONFIG_SERIALIZE 默认开启，依赖 CONFIG。直接调用 hx_os_config_serialize 将
结果写入 hx_string，或用 hx_os_config_save 一次完成序列化和原子发布。保存还
依赖 FILE_UTILS，遵守原子发布既有的持久化及部分失败边界。文档保持不可变。
写入器统一使用内置实现，不受原文档解析后端影响；显式 writer options 的 backend
须为 BUILTIN。YAML/XML/TOML/ENV 输出目前报 NOT_SUPPORTED。

JSON 输出紧凑 UTF-8，合法 JSON 数字保留原始拼写，不经过 double 舍入。TOML
进制整数精确转为十进制，十进制下划线和前导加号去除；非有限数字、XML 节点和
日期节点不能悄悄改成 JSON 字符串。INI 支持根字符串键值及一层字符串 section，
先写全局键再写 section，各组内部保持顺序，不保留注释和排版。会被 INI 解析器
裁剪或误解的内容报 UNREPRESENTABLE。properties 支持平面字符串对象，转义
分隔符、空格、反斜杠及控制字符，Unicode 保持 UTF-8。数字、布尔、数组、null
和嵌套对象不能静默转成行式格式的字符串。无法表达时保留原输出。

序列化先有界测量，再一次分配输出并写入。两个遍历共同计入 max_work，每次遵守
max_nodes/max_depth，递归最多 128 帧。max_input_bytes 在写入时限制输出字节；
max_memory_bytes 限制新输出分配及分配器元数据，不包含调用方持有的文档、旧输出
和有界 C 栈。输出使用 hx_string 自己的 context。分配失败或任一阶段超限均保留
旧值。并发读取使用独立输出并保留文档至调用结束；不增加锁、全局可变设置或后台
活动。

TEXT_NUMBER 可独立于 CONFIG 使用 parse_double/format_double。配置 getter、
数值语法和第三方适配共用同一份自有助手；关闭公共数字文本模块不改变配置 getter
行为。解析接受既有数值语法的完整 ASCII token，包括 TOML 下划线/进制及带符号
inf/nan，不接受外围空白或内嵌 NUL；上溢/下溢报 RANGE。IEEE binary64 格式化使用
17 位有效数字，nan/inf 规范化，并使用 C 数值 locale。不修改进程 locale；POSIX
暂时选择调用线程 locale，随后恢复。locale 对象沿用原配置 getter 的原生 CRT
分配方式，不计入注入分配器账目。

C++ ConfigDocument 持有不可变引用，支持复制/移动、类型化 pointer 查询、仅在键
缺失时回退的 stringOr、serialize/save。Text::parseDouble/formatDouble 提供
C++ 结果。C++ 序列化用默认 context 临时缓冲区并返回 std::string；需要指定
context 时使用 C 输出入口。examples/config_save.c 演示精确保留大整数的序列化，
传入文件名时直接完成保存。

## 周期定时器

TIMER 默认开启，依赖 LOOP，可在 NETWORK 关闭时使用。
hx_os_timer_create(loop, interval_ns, callback, user, &timer, &error)
在创建后一个正数间隔开始触发，由创建线程驱动 loop。间隔单位为单调纳秒，
不保证纳秒精度或硬实时截止时间。后续沿原定时间轴调度，错过的周期以及回调
消耗的时间合并处理，每次 poll 最多为该定时器触发一次。原有一次性请求及
公共 operation 布局保持不变。

回调在 poll 线程锁外执行，error 指针仅借用到回调返回；OK 表示一次触发，
终止性的时钟／范围错误表示停止。stop 可跨线程请求，不额外发送取消回调。
wait_until 包含最终回调和可选 cleanup 返回；创建线程等待未完成对象返回
DEADLOCK。close_until 仅创建线程调用，驱动取消，TIMEOUT 保留对象供重试。
完成退役后即使报告终止错误也消费对象。触发后的时钟失败可能另行通知终止错误。

带 cleanup 的创建入口仅在接受成功后接管清理职责，在锁外执行一次，清理返回后
才发布完成。回调／cleanup 内同步关闭自身返回 DEADLOCK，可改为请求 stop。
user 借用数据保持到退役；其他线程结束使用后才能销毁对象。loop 关闭会取消
定时器，但仍须逐个关闭定时器对象，否则 loop_destroy 返回 BUSY。

每个活动定时器占用一个现有 loop 请求名额，仅创建时增加一个小对象；每次
触发无分配、无建线程、无队列增长。到期路径读取单调时钟并直接调用 C 回调，
常规请求路径增加私有 timer 指针及分支。对象大小／对齐实测为 Windows x86
40/8、Windows/Linux x64 64/8；原有公共布局不变。这是资源成本记录，
不能代替运行性能测试。

C++ hx::PeriodicTimer::every(owner, loop, milliseconds, lambda) 保留宿主
C 指针和显式 closeUntil；析构不等待、不隐藏资源。lambda 异常请求停止，
退役后通过 rethrowCallbackError 取回；即使封装对象仍存在，捕获数据也在
cleanup 中释放。wait/close 保留原生终止错误。直接 C 用法见
examples/periodic_timer.c。

## 线程池身份查询

hx_os_thread_pool_inside(pool, &inside) 查询当前线程是否属于指定的存活线程池，
任务 cleanup 中也返回真。无效参数／模块关闭返回 INVALID／NOT_SUPPORTED，
输出不变；调用期间保持池存活，不转移所有权。复用现有线程局部状态，不新增锁、
分配、系统调用或工作线程记账；C++ 直接调用 pool.inside()。

工作线程等待本池未完成任务返回 DEADLOCK，零截止时间同样如此；等待已经完成
的任务可以成功。关闭自身线程池也返回 DEADLOCK。查询其他池为假不代表跨池等待
必然安全，循环依赖仍由调用方避免。本批不加入任务依赖图、窃取或内联执行。

```c
int inside;
hx_os_status status = hx_os_thread_pool_inside(pool, &inside);
if (status == HX_OS_OK && !inside)
	status = hx_os_task_wait_until(task, deadline, &work_status, &error);
/* Inside a worker, continue asynchronously instead of waiting for new work. */
```

## 原生错误文本

hx_os_error_message 为 GAI 错误追加严格 UTF-8 的原生说明，Apple 上还支持
Mach 域。既有状态／domain／code 前缀和其他域行为保持不变。说明不可用、过长、
不符合 UTF-8 或原生值超出 int 范围时，保留数值诊断；原始 error 中的
cause_code 不变。required 包含 NUL，小缓冲区会终止，但截断结果不保证落在
UTF-8 字符边界；按所需容量取得完整文本。

POSIX 使用 gai_strerror，Apple 增加 mach_error_string。Windows 用调用内
宽字符缓冲区接收 FormatMessageW，再严格转为 UTF-8；无须 WSAStartup，
NETWORK 关闭也可用。[Microsoft 文档](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-gai_strerrorw)
明确 gai_strerrorW 使用非线程安全存储。原生措辞取决于系统及 locale，不应用于
程序分支判断，也不要并发修改进程 locale。C 路径不新增本库分配或锁。
C++ StatusError::message() 返回持有完整原生详情的 std::string；what() 的
原有非抛异常状态名行为和对象布局不变。hello C 示例已接入详细错误输出。

## 随机数、UUID、CRC32 和安全清零

RANDOM 默认开启。hx_os_random_bytes 使用系统密码学随机源填满调用方缓冲区，
不以时间种子或普通 PRNG 回退。可能等待系统熵，不承诺截止时间或取消。NULL
仅可搭配零长度；原生失败会清零整个输出，无效参数／模块关闭则保持输出不变。
不需要 context、本库分配或后台线程；操作系统提供程序可能自行分配内存。

Windows 从绝对系统目录加载 BCryptGenRandom，仅入口不可用时回退
CryptGenRandom；每次释放模块／provider，不建立隐藏的全进程缓存。CNG 失败
保留有符号 NTSTATUS，使用新增枚举域输出数值；传统路径保留 Win32 错误。
advapi32 是私有原生依赖，也记入静态 pkg-config 依赖。Linux 使用 getrandom
系统调用，重试 EINTR／短读，仅 ENOSYS 或目标头无系统调用编号时走旧路径。
旧路径先从 /dev/random 读 32 字节，再使用 /dev/urandom，核对设备类型与编号，
关闭描述符；熵不足的旧嵌入式内核可能无限等待，权限拒绝明确失败。由于公开
file_open 只支持普通文件，随机设备由专用底层适配处理。Apple 使用
arc4random_buf，原生环境待验证。[Linux 设备契约](https://www.man7.org/linux/man-pages/man4/random.4.html)
说明了启动早期不能直接依赖 /dev/urandom 的原因。

UUID 设置 v4 版本和 variant 位，失败保持输出。格式化为小写 8-4-4-4-12 加
NUL，允许输入输出重叠，RANDOM 关闭也能格式化已有 UUID。
hx_os_uuid_generate_string(&text, &error) 一次完成生成与格式化，使用字符串
自己的 context；分配失败也保持原值。C++ Random::uuid 返回持有字符串，
fill/bytes 提供原始字节。测试不代表唯一性保证或熵质量认证，C 示例见
examples/random_uuid.c。

CRC32 始终可用：反射 CRC-32/ISO-HDLC，多项式 0xedb88320，初末异或全 1；
种子 0 开始，前次结果继续增量。固定 16 项表占 64 字节只读空间，每字节两次
查表，无延迟初始化、分配或锁。仅用于意外损坏检测，不是密码学完整性验证。
secure_zero 使用 SecureZeroMemory、Linux 检测到的 explicit_bzero，或 volatile
逐字节写入；防止删除这些写入，不保证擦除其他副本／寄存器／交换区，也不提供
线程同步。两项微工具均不建立全局配置。

## 按页内存与文件映射

VIRTUAL_MEMORY 默认开启，不依赖 SYSINFO 或 FILE_UTILS。
hx_os_pages_alloc 把正数字节数向上取整至系统页大小，拥有初始清零的匿名内存
和使用 context 分配的元数据。支持 NONE、READ、READ|WRITE、READ|EXECUTE、
READ|WRITE|EXECUTE 请求；缺少 READ 的 WRITE／EXECUTE 返回 NOT_SUPPORTED。
系统策略可能拒绝可执行页，库不静默降低保护要求；整块 protect 开启 EXECUTE
时包含指令缓存同步。保护失败可能已经部分生效，须恢复已知权限后使用。
操作系统／硬件权限不等于安全隔离保证；修改保护与销毁期间由调用方排除访问。

hx_os_file_map_open(path, mode, &map, &error) 一次映射整个 UTF-8 路径普通
文件；create 可指定 context、已打开文件、字节偏移和长度，长度 0 表示到 EOF。
空区间返回 EOF，越界返回 RANGE，不延长文件。内部对齐原生偏移，返回用户
请求的视图，不修改文件位置。每个映射持有独立复制的文件资源，Windows 另持有
映射对象；create 返回后可关闭原文件，创建期间仍须排除关闭与截断。

READ 返回只读借用视图，write_view 为 NULL；SHARED_WRITE 要求读写打开，
拒绝 APPEND；PRIVATE_COPY 可从只读文件句柄建立私有写入，不回写文件。
两种映射都不是快照，其他修改和并发截断需调用方协调。截断或存储失败产生的
页错误可能表现为 SIGBUS／原生异常，无法由普通 C 状态返回兜底。远程文件系统、
映射与普通 I/O 混用的一致性遵循原生系统，不额外承诺。

仅 SHARED_WRITE 可以 flush，先用 msync(MS_SYNC)／FlushViewOfFile 写回，
durable=1 再对独立持有资源调用 fsync／FlushFileBuffers。成功不等于硬件绝对
持久化；失败保留映射可重试，销毁不隐式 sync。原生 unmap 失败保留对象；
unmap 成功后，即使句柄关闭报告错误也消费对象。页释放失败同样保留；借用
视图不得越过对象寿命。参见 [Windows 冲刷契约](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-flushviewoffile)
及 [Linux 映射契约](https://www.man7.org/linux/man-pages/man2/mmap.2.html)。

C++ Pages／MappedFile 提供只移动的同步 RAII，显式 close 可观察释放错误，
析构无法报告错误且不隐式冲刷。C 便利示例为 examples/mapped_file.c。
每个对象增加一次元数据分配，真实页由系统管理、不计入 context 堆字节预算。
映射创建增加复制和映射原生 API，无库内共享锁、后台工作或逐次访问封装。
实测大小／对齐：Windows x86 pages 8/4、map 28/4；Windows x64 16/8、56/8；
Linux x64 16/8、40/8。既有公共结构体保持不变。

## 文件区间锁、时间戳与权限摘要

FILE_EXTRA 默认开启，不依赖 FILE_INFO 或 FILE_UTILS。lock_at(file, offset,
bytes, shared, deadline, error) 对正数字节范围尝试加锁，不改变文件位置，
区间终点须可用有符号 64 位表达。共享锁要求 READ，排他锁要求 WRITE。NOW
尝试一次，冲突返回 TIMEOUT；有限／无限等待使用单调截止时间，原生非阻塞尝试
与最多请求 10 ms 的睡眠间隔。系统调用和调度可能越过截止时间。失败保留文件，
超时保留原生冲突详情；按已取得锁的原区间解锁。库不为锁操作分配、加内部锁或
启动线程。

POSIX fcntl 记录锁属于进程且为建议锁，关闭同一 inode 的任何描述符，包括
映射持有的副本，都可能释放进程的锁。Windows LockFileEx 按句柄持有，会限制
普通冲突 I/O，不限制映射访问。Windows 定位 I/O 使用另一个句柄，因此持锁时
应通过加锁句柄使用带游标的读写。不要用区间锁代替线程互斥，不重叠、升级或
重入同一区间，也不承诺公平性。参见 [Linux 记录锁](https://www.man7.org/linux/man-pages/man2/fcntl_locking.2.html)
和 [Windows 区间锁](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-lockfileex)。
C++ FileRangeLock 借用 File 到解锁／析构，默认 NOW，允许移动构造转移持有；
显式 unlock 可报告错误。

时间结构用 HX_OS_FILE_TIMES_INIT 或按容量初始化。fields 选择访问／修改
时间，单位是 Unix 秒加纳秒；未选择字段保持不变。fields=0 是无操作，不检查
路径是否存在或权限。纳秒越过 [0,1e9)、原生时间范围不足、size/version 错误
均明确报告。POSIX 使用 utimensat/UTIME_OMIT 并指定是否跟随链接；Windows
使用宽字符属性句柄和 SetFileTime，截断到 100 ns，文件系统精度可更低。
不设置创建／元数据变更时间；Windows 不表达 1601 年前日期，time_t 为 32 位
的目标可能拒绝部分 64 位秒值。关闭句柄失败时，时间更新可能已经完成。

POSIX 权限更新替换 owner 的读／写／执行位，保留 group/other；调用方排除
并发路径替换和权限修改。不跟随链接必须真正支持，否则失败。Windows 必须包含
READ，不支持 EXECUTE；WRITE 仅控制只读属性，不编辑 ACL、不保证有效访问权限。
原有 v1 stat 权限摘要仍只覆盖读写。C++ FileAttributes::setTimes/permissions
及 examples/file_attributes.c 提供直接调用。Windows 路径转换和属性句柄
为短期资源；POSIX 元数据原生调用不增加注入堆分配。新时间结构在 Windows
x86/x64 和 Linux x64 均为 40/8 字节大小／对齐；既有布局不变。

## 目录监听与外部描述符就绪通知

WATCH 默认开启，不依赖 LOOP／NETWORK／FILE_UTILS。一次
hx_os_watch_create(context, path, recursive, callback, user, &watch, &error)
创建一个可等待退出的线程，并等到原生订阅就绪。仅链接或开启模块不会启动线程。
回调开始前已经写入输出指针，但回调可能先于 create 返回运行。用户回调在该线程
锁外执行，可请求 stop。close_until 停止并等待线程、cleanup、TLS 清理返回；
完成退役后即使报告后端终止错误也消费对象。TIMEOUT 保留对象供重试，自身关闭
返回 DEADLOCK。排除并发销毁／使用；仅接受成功后接管可选 cleanup，执行一次，
拒绝不转移用户数据。

事件路径为回调期间借用的严格 UTF-8 相对路径，分隔符为斜线，空字符串表示根。
变更／新增／删除／重命名为尽力通知，不承诺事务顺序或完整审计历史。old_path
可为空：Linux 未组装配对时报告观察到的重命名路径，Windows 配对同一缓冲区
相邻的新旧名称，Apple 提供观察路径。DIRECTORY 标志只表示确定为目录，缺少
标志可能是未知，Windows 通知不提供类型。ERROR 终止监听；OVERFLOW 要求重新
扫描目录快照，不能只重试最后一条事件。无法表示的原生文件名也明确报告溢出与
编码错误详情。

默认原生输入缓冲 64 KiB，路径上限 32 KiB，软件监听记录 1024，深度 64，持有
可变存储预算 4 MiB。分配头、增长／重建期间并存的新旧副本计入预算。固定线程／
事件控制对象、原生线程栈、有界的临时 Windows 路径转换、系统和 CF 管理存储
不在此预算内。路径暂存与树记录按需增长，没有库内事件队列；慢回调可能使
原生缓冲溢出。目录数／深度限制作用于 Linux 软件递归扫描，Windows／Apple
树订阅使用一个原生源。

Windows 使用重叠 ReadDirectoryChangesW 和独立停止／完成事件，在线程内等待
原生取消退役后才释放缓冲。Linux 使用 inotify/eventfd，通过 openat 的
O_NOFOLLOW 与 /proc/self/fd 固定目录；缺少 procfs、重复 inode 别名或 9p
返回 NOT_SUPPORTED。目录结构变更后重建有界覆盖，再报告 OVERFLOW，因为扫描
期间可能丢增量事件。记录查找在有界目录数内线性进行；重建可短期并存两棵树，
预算或扫描失败会终止，不能静默变成不完整递归。根移动／删除结束覆盖。其他
远程文件系统的通知仍受原生限制。

Apple 使用纯 C 的 FSEvents/CoreFoundation，无 Objective-C 或第三方运行库。
stream 在自有线程 CFRunLoop 上运行，run loop 保留到等待退出完成；局部使用
已弃用的调度 API，以保留单线程回调契约及 10.13 基线。合并／丢弃／根变更标志
转成溢出或终止事件。源码已实现，原生编译运行及沙箱行为待验证，参见
[Apple 调度契约](https://developer.apple.com/documentation/coreservices/1447824-fseventstreamschedulewithrunloop)。
C++ DirectoryWatch 保留宿主 C 指针，显式 closeUntil，捕获回调异常，cleanup
释放捕获内容；关闭后调用 rethrowCallbackError。C 示例为 watch_directory.c。

LOOP_FD 是单独默认开启的 POSIX 扩展，依赖 LOOP。attach_fd 持有 CLOEXEC
副本和一个固定请求名额，不修改 O_NONBLOCK；创建线程驱动 loop，回调通过借用
描述符执行自己的非阻塞 I/O。READ／WRITE 也可表示 EOF／错误就绪，由实际
I/O 确定。descriptor 查询仅创建线程调用，不得关闭返回的借用句柄。原描述符
可独立关闭，但文件位置和打开标志仍共享，也受 POSIX 记录锁关闭语义影响。
Windows 明确返回 NOT_SUPPORTED，不声明对应能力位。

停止／等待／关闭包含最终回调与 cleanup；创建线程的 close 驱动取消，取消
注册失败保留源供重试；取消注册成功后，描述符关闭错误也消费对象。所有监听
对象关闭前 loop_destroy 返回 BUSY。每次就绪不分配；定时器和描述符共用同一份
取消／驱动等待逻辑。C++ DescriptorWatch 提供 lambda 和异常封装。
loop/request 各增加一个私有指针，Windows x86/x64、Linux x64 实测各增加
8 字节；既有公共布局不变。watch 大小／对齐：x86 152/8、两端 x64 216/8；
options/event：x86 28/4、48/8，x64 48/8、56/8；描述符监听为 44/4 或 72/8。

```c
hx_os_loop_watch *watch = NULL;
hx_os_status status = hx_os_loop_attach_fd(loop, fd, HX_OS_WAIT_READ,
	ready_callback, user, &watch, &error);
/* The callback can use hx_os_loop_watch_descriptor(watch), then request stop. */
/* Drive loop; finally check hx_os_loop_detach_fd(&watch, &error). */
```

## 进程、输出捕获与系统工具（第 18 批）

PROCESS 与 SYSTEM_UTILS 默认开启，可独立关闭。Windows 的系统工具为原生
Unicode 参数读取增加 Shell32 链接。捕获依赖 PROCESS、PIPE、LOOP，关闭
NETWORK 仍可用。整数微工具和显式 POSIX timespec 转换不依赖这两个模块；
Windows 的 timespec 操作返回 NOT_SUPPORTED。

`hx_os_process_spawn` 接收 UTF-8 可执行文件路径、参数数组、可选完整环境
和标准流重定向。参数不包含 argv[0]，由库根据路径补入；不执行 shell，
不搜索 PATH。NULL 参数表示无参数；NULL 环境表示继承快照，{NULL} 表示
空环境。环境项为非空名称=value，名称必须唯一，Windows 不区分名称大小写。
显式环境不会自动补入 SystemRoot/PATH，调用者提供子程序需要的变量。
输入只在启动期间借用，启动成功后由进程对象持有原生资源。

参数和环境分别限制为 4 MiB、65536 项。Windows 转为 UTF-16，按 CRT /
CommandLineToArgvW 规则转义引号和反斜杠，检查原生 32767 宽字符命令行限制，
排序 Unicode 环境，显式选择可执行文件。自定义命令行解析器可能采用不同
规则。标准句柄通过私有允许列表继承；缺失的 Windows 父标准流用 NUL。
临时可继承副本存在期间，宿主需协调其他“不限制继承列表”的启动操作。
POSIX 使用 posix_spawn、CLOEXEC 私有副本及 dup2 动作；宿主没有设置 CLOEXEC
的描述符仍会继承，相关关闭与启动操作需宿主协调。继承的 POSIX 环境保留
原始字节，公共环境枚举则在任何回调前严格验证 UTF-8。

同一进程对象的操作需要外部互斥。NOW 尝试一次；有限及无限等待沿用单调
截止时间，原生完成查询之间请求最多 5 ms 睡眠，不保证硬截止时间。
超时保留对象与结果，可重试；成功结果缓存，非零退出码也属于等待成功。
POSIX 信号退出单独报告，不伪造 128+signal 退出码；Windows 先判断完成再取
退出码，因此真实退出码 259 不会被误判为仍运行。
destroy 在仍运行时返回 BUSY；close_until 先等待再销毁。
库不强杀、不分离、不创建回收线程、不修改 SIGCHLD；宿主不能自行回收同一
子进程或将 SIGCHLD 设为忽略。

`hx_os_process_capture_run` 内部启动进程并通过私有 loop 同时收集 stdout /
stderr，子进程 stdin 为 EOF；不启动工作线程。默认每路保留 1 MiB 原始字节。
管道、loop 和首批读请求在启动前准备好；子进程一旦启动，即使返回 TIMEOUT、
LIMIT 或 I/O 错误，仍向调用者交付会话对象，可在原对象上继续等待或关闭。
成功要求子进程退出且两路都达到 EOF；继承写端的后代进程可能延迟 EOF。

恰好达到上限也会返回 LIMIT，即使下一次读取可能就是 EOF。调用者可提高
上限，或显式选择丢弃后续输出并继续排空。已经接受的读请求仍提交保留的
前缀；I/O 失败后选择丢弃，会关闭已经退役的失败流，子进程可能看到断管。
库不静默截断。字节视图借用到下次等待／关闭；文本转换默认严格 UTF-8，
保留 BOM，使用输出字符串自己的 context，失败保持输出不变。
超时快照可能停在字符中间，此时转换可以失败。旧编码仍取决于已有可选编码
后端，内置 Unicode／ISO-8859-1 转换不依赖该后端。

会话占用两个请求槽、32 KiB 在途字节预算，保留缓冲按需几何增长；显式
丢弃时每路可再分配 16 KiB 临时缓冲。上限约束保留输出，不等于整个堆内存
上限：扩容峰值同时持有旧／新缓冲，还需请求对象、分配器元数据、启动快照
和原生资源。每次读取复用现有请求的分配、锁、事件机制。既有文件、线程、
线程池热路径没有增加锁、间接调用、分配或系统调用；同步管道创建补记实际
读写方向，以便验证进程重定向权限。

`hx_os_process_arguments` 返回独立持有的 UTF-8 快照，包含 argv[0]：
Windows 从原生宽字符命令行读取，Linux 读取 /proc/self/cmdline，Apple 使用
原生 argc/argv。Linux 缺失 procfs 时返回 NOT_SUPPORTED。C 示例共用参数
适配入口；裁剪版本要求宿主提供 UTF-8 argv，不能保证 Windows 窄字符 CRT
的 Unicode 保真。主机名支持缓冲和 hx_string 输出，进程 ID 使用原生标识。
环境枚举在库内环境锁下取有界快照，锁外回调允许重入和修改；外部 CRT／原生
环境修改仍需宿主互斥。锁内暂存采用 CRT 分配，避免调用注入分配器回调。

CPU 位集覆盖能确认可用的 SSE/SSE2/SSE4.1/AVX/AVX2/NEON；AVX 同时检查 XSAVE、
OSXSAVE 与 XCR0 的 XMM/YMM 状态。ARM 使用可用的原生辅助向量／系统能力查询，
未知位不声称支持。WOW64／Rosetta 是系统兼容／翻译状态，不是虚拟机探测；
无法可靠确定的 Linux 仿真状态返回 NOT_SUPPORTED 并保留输出。
字节序交换和 udiv64 使用可移植头内联 C；除零返回 INVALID 并保留输出。
ARM 工具链选择自身除法 ABI 辅助实现；除法基准与相同原生表达式对照，
不声称采用汇编获得加速。

POSIX 时长转换检查 time_t 范围。截止时间转换要求明确传入 CLOCK_MONOTONIC
或 CLOCK_REALTIME，通过采样原生时钟并加上库截止时间的剩余时长完成，
不直接转换不同时钟的纪元；无限截止时间返回 NOT_SUPPORTED。墙钟调整、
休眠及调度仍影响原生等待，唤醒后必须重新检查条件和截止时间。

C++11 的 `hx::System` 返回独立字符串／vector；`hx::Process` 和
`hx::ProcessCapture` 引用宿主持有的 C 句柄，异常／超时后仍可找回资源；
析构不会等待、分离或强杀。完整可执行用法见
[capture_process.c](../examples/capture_process.c)，其中明确选择超时重试和
达到上限后丢弃未来输出的策略。macOS、Win7、嵌入式目标仍待原生验证。

## 调度请求与延迟任务（第 19 批）

THREAD_EX 增加指定线程及当前线程的优先级、亲和性请求。Windows 映射原生
idle/normal/time-critical 相对优先级，仍受进程优先级类影响；Linux 请求
SCHED_IDLE／SCHED_OTHER 的零优先级，或 SCHED_RR 的原生最高优先级。
Apple 使用原生调度器低／中／高范围，低与普通对应 SCHED_OTHER，高对应 RR。
权限及原生失败明确返回；这些请求不保证固定 CPU 频率或硬实时响应。
测试提高被阻塞线程的优先级后，先恢复普通再放行；旧系统恢复优先级也可能
需要权限。调用者需排除同一目标的并发 join、销毁及调度参数修改。

亲和性零掩码返回 INVALID。Linux 位对应 CPU 0..63，设置后读回实际掩码，
避免内核与 cpuset／在线 CPU 求交后被误认为完整成功。不一致时返回
NOT_SUPPORTED 并尝试恢复旧掩码，恢复 errno 放入 error.cause_code。
失败可能已经短暂或永久改变亲和性，成功读回也不能阻止之后的热插拔／
cpuset 调整。Windows 位对应线程当前处理器组，受指针宽度限制，x86 高位
明确返回 NOT_SUPPORTED；macOS 严格绑定返回 NOT_SUPPORTED。
C++ Thread 提供 setPriority／setAffinity。

DELAYED_TASKS 默认开启，依赖 THREAD_POOL，独立于 LOOP／TIMER／NETWORK，
有单独能力位。绝对单调 due 时间表示“最早可运行”，不表示完成截止时间。
NOW 进入普通就绪队列；INFINITE 返回 INVALID。C++ submitAfter 接收毫秒，
非正值等同 NOW；submitAt 接收库截止时间值。可取消版本沿用现有取消令牌、
类型化结果、异常和有界准备流程。

有序未来任务链表与就绪 FIFO 共用 queued+submitting 容量，不增加管理
定时线程、第二份队列额度或任务依赖图。现有工作线程通过条件变量等待
最早 due 时间或通知；同一 due 按入队顺序转移到已有就绪工作后面。
多工作线程实际开始／完成顺序仍可能不同。插入和排队取消最多扫描有界
未来链表的长度；转移时唤醒其他工作线程。时钟或条件等待失败时，受影响
未来任务返回失败的 work_status，仍走正常 cleanup。

取消未来任务会将其移入就绪队列，不用等到原 due 时间；cleanup 在工作
线程、池锁外恰好执行一次。DRAIN 保留未来时间；CANCEL_PENDING 将排队的
未来任务标为取消并尽快清理。超时保留池和任务，重试可从排空升级为取消
待执行任务；运行中的任务仍靠协作取消。

未来任务的元数据与任务共用一次分配，额外创建一个私有互斥锁。取消方
先持有此锁再访问池；工作线程在池锁外清除元数据中的池指针，然后才执行
任务／cleanup。所有工作线程 join 后，保留的任务和取消源可安全晚于池销毁。
内部关闭路径在池锁下直接标记取消，不取得任务私有锁，避免锁顺序反转。
分配器和 prepare 回调继续在池锁外执行，关闭与准备并发时也遵守原契约。

开启模块后，普通任务增加一个可空私有指针和分支；没有未来任务时，不增加
分配次数、锁次数、间接调用、时钟读取或系统调用。关闭模块会裁掉这些私有
字段和延迟逻辑。实测大小／对齐：Windows／Linux x64 的 task 88/8、
pool 104/8、延迟元数据 32/8；Windows x86 分别 56/8、64/8、24/8。
既有公共结构不变。未来任务另承担互斥锁分配、挂起期间的时钟／定时等待
开销。前后对照见 VALIDATION；这些数据不能替代 hx_c_log 接入及其性能验收。

```c
hx_os_task *task = NULL;
hx_os_status status = hx_os_thread_pool_submit_delayed(pool,
	hx_os_deadline_after_ms(100), work, user, cleanup, &task, &error);
/* On acceptance, use the ordinary task wait/cancel/release API. */
```

```cpp
auto task = pool.submitAfter(std::chrono::milliseconds(100),
	[](int value) { return value * 2; }, 21);
int answer = task.get();
```

C 示例见 [delayed_task.c](../examples/delayed_task.c)；关闭与持有规则沿用
现有线程池契约。macOS、Win7、ARM/musl、处理器组及真实板卡仍待目标验证。

## 本地套接字与双向文件端点（第 20 批）

LOCAL_SOCKETS 默认开启，依赖 NETWORK。本地路径流使用 HX_OS_LOCAL /
HX_OS_LOCAL_STREAM，专用工厂和普通 socket_create 都支持这一组合。
路径地址使用独立类型，不扩展既有 IP 地址及 operation ABI。路径为严格
UTF-8，108 字节内必须有 NUL；macOS 继续检查更短的原生 sun_path 上限。
本接口不含抽象名字、传递描述符、对端凭据及数据报。查询返回的原生名字
也必须放得下且为合法 UTF-8，不静默截断；空输出表示未命名对端。

local_listen 合并创建／绑定／监听，不删除已有路径；close 不移除文件系统
名字，调用者拥有目录并在使用后删除自己的套接字名字。绑定成功后监听
失败，可能遗留新创建的名字。Windows 使用原生 AF_UNIX 提供程序及 Win32
UTF-8 路径语义，适用于支持该能力的 Windows 10+。Win7 的其他基础能力仍
保留，本工厂在不支持时返回 NOT_SUPPORTED。能力位表示适配已编译，实际
提供程序是否可用仍由原生操作判断。

local_connect 合并创建及带截止时间的连接；TIMEOUT 保留套接字，
finish_connect 使用已复制的路径继续。重试必须保持同一路径，终止失败后
需要关闭。Linux AF_UNIX 队列满的 EAGAIN 表示连接还没开始，库会在单调
截止时间内重新发起连接，不会把可写误报为已连通；真正进行中的连接同时
检查 SO_ERROR 与 getpeername。便利工厂其他失败会关闭新对象并保留原错误。

本地流复用 send／receive／wait／shutdown／close。新增 socket_receive_until
合并一次流读取和就绪等待，返回实际字节、EOF、错误或 TIMEOUT，没有暗中缓冲。
已经接受／连接的本地流支持现有 loop READ／WRITE 以及取消退役。
loop CONNECT／ACCEPT 明确拒绝本地套接字，因为旧 IP operation／completion
放不下路径；相应操作用直接 accept_until 和 connect_until／finish_connect。
不能混用直接 I/O 与未完成 loop 请求，线程互斥、loop 所有者、network 持有
规则沿用既有契约。

DUPLEX_PIPE 默认开启，独立于 NETWORK、PIPE、LOOP。pipe_pair_socket 返回
两个各自可读写的 hx_os_file。POSIX 使用 socketpair(AF_UNIX, SOCK_STREAM)，
Windows 使用私有 IPv4 TCP loopback 回退。默认创建截止时间为单调 5 秒，
_until 入口允许调用者指定；POSIX 执行一次本地 socketpair 操作，截止时间
不会取消同步原生调用，也不承诺硬返回时间。失败时两端均为 NULL。

Windows 握手使用非阻塞连接／接受和截止时间等待、排他的本地绑定、
最多 64 次接受尝试，并核对本进程客户端的回环地址／端口。交付后两端为
阻塞、不可继承对象，各持有一份平衡的 Winsock 引用，因此不需要 network
实例。读写／关闭分别使用 recv／send／closesocket，不把套接字交给文件
句柄 API。只有这种文件对象分配附加存储；Windows 进程启动明确拒绝把它
用作子进程标准句柄，POSIX 的描述符可作为标准流。

遵守文件短读、短写、EOF 契约；同一端允许一个读取者和一个写入者并行，
同方向操作及关闭需互斥。shutdown(SHUT_WRITE) 发出 EOF 而保留读取方向。
定位、查询大小、sync、截断及映射不支持，也没有持久化、消息边界或凭据
保证。Windows 回退具有端口、TCP 缓冲、防火墙／提供程序失败差异。
同步双向配对不替代已有专用异步管道。POSIX 管道与套接字文件写入共用
线程局部 SIGPIPE 处理，不安装进程级信号处理器。

C++ DuplexPipe::pair 返回两个可移动 File 所有者；LocalSocket 引用宿主持有
的指针，提供 connect／finishConnect／acceptUntil、显式 close、
readUntil／writeUntil、路径字符串及 shutdown。超时或 I/O 未退役时仍能
找到原对象。

网络和独立双向端点共用套接字设置、错误、就绪等待代码；普通套接字创建
不增加原生调用数和分配次数。开启本地模块会增加一个私有可空指针；
只有本地套接字在同一次分配尾部附加 112 字节路径／状态。Windows 普通文件
读写／关闭增加类型判断分支，不增加分配或原生调用。POSIX 在 PIPE 已开启
时复用原有管道写分支。Windows 双向工厂有两次包装对象分配、两份 WSAStartup
引用、临时监听套接字和两个连通端点，关闭平衡各自引用。

```c
hx_os_file *first = NULL, *second = NULL;
hx_os_status status = hx_os_pipe_pair_socket(NULL, &first, &second, &error);
```

```cpp
auto channel = hx::DuplexPipe::pair();
channel.first.writeAll("hello", 5);
hx::DuplexPipe::shutdown(channel.first);
```

可执行示例见 [duplex_message.c](../examples/duplex_message.c)。
macOS、Win7、嵌入式原生环境仍待验证。


## 注册表、电源请求和字符串混淆（第 21 批）

常用入口 `hx_os_registry_open_user` 打开显式 HKCU 相对路径，
`hx_os_registry_read_string` 直接输出严格 UTF-8 的 `hx_string`；
`read_string_buffer` 共用解码实现，保留两段式容量协议。字符串读取接收
REG_SZ/REG_EXPAND_SZ 的字面值，不展开环境变量；原生值上限 1 MiB，容量变化
最多重试 8 次。损坏 UTF-16、内部 NUL、错误 DWORD 长度和类型不匹配明确失败，
失败保留输出。注册表路径用反斜杠、相对于指定根键，不按文件路径处理。
创建、写权限、WOW64 视图均须显式选择；独占创建保护已有键。原生创建可能
创建父键，不是事务。同一对象调用需串行，并排除并发关闭；关闭即使失败也
消耗对象。非 Windows 或关闭模块时返回不支持。

`hx_os_sleep_inhibit_create(NULL, "Exporting project", &request, &error)`
直接创建已激活请求；重复激活不增加引用，销毁只撤销当前对象持有的请求。
宿主同步后可以跨线程转移，单个对象的并发调用由宿主互斥。不创建后台线程。
Windows 用 Win7 已有的 PowerCreateRequest/SystemRequired 按句柄隔离消费者；
原草案 SetThreadExecutionState 会共享线程状态，因此调整为独立句柄。
性能请求使用 ExecutionRequired，Win8 起可用，Win7 返回不支持，不改变电源
计划、不保证固定频率或实时调度，系统和用户策略可能覆盖请求。
Apple 防睡眠使用 IOKit C 接口；性能活动使用 NSProcessInfo 公共接口，所需
Objective-C 限制在单个实现文件，异常不越过 C ABI。关闭 PERFORMANCE_REQUEST
即可去掉 Foundation/Objective-C，保留纯 C 的 IOKit 防睡眠。Linux 两项均按
既定决策返回不支持，不引入 DBus。释放消耗对象并报告原生清理错误。
C++11 的 Registry、SleepInhibit、PerformanceRequest 提供可移动同步 RAII；
显式 close 可观察清理错误，析构不会无限等待。

字符串混淆使用可逆 XOR，不提供加密。开发工具
`tools/obfuscate_string.py --name app_label --text "example"` 输出预编码 C99
数据，输入严格 UTF-8、最多 1 MiB；Python 不属于核心运行或构建依赖。
`hx_os_obfuscate_string(&app_label, &text, &error)` 使用输出字符串的 context，
失败保持输出，清零内部临时明文；结果明文由调用方管理生命周期。
缓冲入口也支持原始字节，允许完全原地解码或互不重叠缓冲，容量不足不写入。
C++11 可用 `constexpr auto text = hx::obfuscate<0x5a>("example")`。
编译器和调试信息仍可能保留字面量，不能据此保护秘密。
直接调用示例见 `examples/obfuscated_string.c`。

四个独立默认开启的开关为 REGISTRY、SLEEP_INHIBIT、PERFORMANCE_REQUEST、
OBFUSCATION。能力位表示编入适配，仍需处理运行系统和策略限制。Windows 在
注册表或原有随机数回退需要时链接 advapi32；Apple 静态消费正确传递 framework
及 Objective-C runtime 链接依赖。


## 日期、翻译表和位流（第 22 批）

`hx_os_wall_time_format_utc_string(milliseconds, NULL, &text, &error)`
直接输出拥有存储的 UTF-8 UTC 时间；缓冲入口提供两段式容量查询，容量不足
不产生部分输出。Unix 毫秒向下取整到秒，包括负值。共用公历实现覆盖
0001—9999 年，不受原生 time_t 年限限制。格式为严格 UTF-8，最多 65536 字节，
支持 %CCYY、%MM、%DD、%hh、%mm、%ss、%%；NULL 使用
%CCYY-%MM-%DDT%hh:%mm:%ssZ。非法格式、编码错误、日期越界均保留输出。
不表示闰秒，不修改 locale。缓冲格式化零分配；字符串入口使用自身 context
分配有界临时存储和结果。

`hx_os_wall_time_local_offset_minutes` 返回当前本地相对 UTC 向东的分钟偏移，
DST 为 0/1/-1（未知）。遵循宿主运行库时区规则，外部 TZ/时区修改须由宿主
同步；不提供历史命名时区查询。无法用整分钟表示的偏移返回不支持。

翻译表通过 `hx_os_text_lookup_create` 创建，add 复制 UTF-8 原文/译文，
get_string 直接给出拥有存储的结果。仅缺失键回退原文，空译文保持为空。
键区分大小写、不做 Unicode 规范化。默认最多 4096 项、键值连同 NUL 合计
1 MiB，可通过 options 设置其他有限上限。按需增长，有序数组二分查询，插入
为 O(n)；元数据另计。事务替换可能暂时保留新旧文本，最多两倍文本上限；
数组增长同样暂存新旧数组。支持并发只读，修改/销毁由调用方排除并发。
无目录锁、全局 locale 单例或后台服务；读取缓冲零分配，字符串使用输出的
分配器。C 组合示例见 `examples/localized_time.c`。

位流 reader/writer 在栈上，借用调用方缓冲区。初始化单位为字节，读写单位
为 0—64 位，所有 CPU 均为字节内和值内 MSB 优先。读越界 EOF、写容量不足
OUTPUT_FULL、值超出位宽 RANGE；失败不移动游标、不修改缓冲或输出。
只替换写入范围，writer 对齐补零，reader 对齐跳过；零位操作只允许零值。
检查字节乘八的溢出，避免移位 64 位。无分配、锁、间接调用、系统调用或汇编。

CALENDAR、TEXT_LOOKUP、BITSTREAM 独立默认开启；关闭时能力位清零、操作
返回不支持。C++11 便利层为 Calendar::utc、TextLookup::get、BitReader/
BitWriter；复用 C 实现，位游标继续借用缓冲，翻译表提供可移动同步 RAII。
