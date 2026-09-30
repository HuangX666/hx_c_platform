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
