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
