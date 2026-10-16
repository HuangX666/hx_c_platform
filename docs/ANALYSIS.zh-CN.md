# 可选的 CPU 与内存分析

[English](ANALYSIS.md) · [区间计时](PROFILER.zh-CN.md) · [README](../README.zh-CN.md)

需要定位 CPU 热点、非法内存访问、泄漏或堆内存峰值时，使用本文。原有区间 profiler 保持不变。详细分析显式启动：打开调试符号不会自动采集、启动线程或安装工具。

## 1. 先选择要回答的问题

| 想知道什么 | 模式 | 主要结果 |
| --- | --- | --- |
| 哪些函数消耗 CPU？ | Linux／WSL 的 `perf` | `report.txt`、`perf.data` |
| 是否越界、使用未初始化数据或泄漏？ | `memcheck` | `report.txt` |
| 哪些调用执行了很多指令？ | `callgrind` | `report.txt`、`callgrind.out` |
| 堆内存峰值主要由哪里分配？ | `massif` | `report.txt`、`massif.out` |
| Windows 哪些线程／函数消耗 CPU？ | `wpr` | `trace.etl`；安装 xperf 时还有 `report.txt` |

在源码根目录使用 Python 3.9+。Python 只用于开发工具，业务运行时不依赖它。`python tools/hx_analyze.py check` 只列出工具位置。Linux 通常使用 `python3`。

## 2. 在已有构建目录中启用符号

沿用已经工作的编译器、SDK 和构建目录。示例使用现有 `build-shared-linux` 和 `build-shared-msvc`，请换成自己的目录。依赖先按[依赖指南](DEPENDENCIES.zh-CN.md)准备。Windows Release 分析需要匹配的 Release 依赖，不能混入 Debug 静态库，也不能通过关闭 CRT 冲突警告来完成链接。

```sh
cmake -S . -B build-shared-linux -DCMAKE_BUILD_TYPE=Release \
    -DHX_OS_DEVELOPER_ANALYSIS=ON -DHX_OS_BUILD_BENCHMARKS=ON \
    -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-linux --target \
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

Windows PowerShell：

```powershell
cmake -S . -B build-shared-msvc -DHX_OS_DEVELOPER_ANALYSIS=ON `
    -DHX_OS_BUILD_BENCHMARKS=ON -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-msvc --config Release --target `
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

`HX_OS_DEVELOPER_ANALYSIS` 默认 OFF。开启后，自有 GCC／Clang C/C++ 目标增加私有的 `-g -fno-omit-frame-pointer`；MSVC 增加 `/Zi` 并生成完整的链接 PDB，保留 Release 链接优化。不改变第三方目标或导出给消费者的编译选项。帧指针可能影响机器码，对照时应保持分析选项、优化和负载一致。Debug／Sanitizer 下的速度不是 Release 性能基线。

保留与报告匹配的程序、库和符号。Windows 共享库开启分析时会一并安装 PDB；静态库分析还需要保留原构建的编译器 PDB。独立业务应用和依赖需要提供各自的符号，本选项不会替它们配置。

## 3. Linux／WSL：查看 CPU 热点

```sh
python3 tools/hx_analyze.py perf -B build-shared-linux -- \
    bin/hx_os_benchmark 20000000
perf report -i build-shared-linux/hx-analysis/perf/perf.data
```

现有基准接受可选的迭代次数，范围 1～100,000,000，默认仍为 1,000,000。两千万次可比很短的运行获取更多样本。脚本以 199 Hz 采集用户态 CPU 时钟样本和 DWARF 调用栈，不要求硬件 PMU。`report.txt` 按库和函数汇总采样成本；百分比不是请求延迟，也不能只看排名就认定代码低效。

perf 先在原生 `/tmp` 临时目录采集，再将结果复制到已有构建目录并删除临时目录，避免 WSL DrvFS 拒绝从 perf 映射缓冲写出、报 `Bad address` 的问题，不新增构建目录。脚本关闭 build-ID 缓存写入，将采样数据限制为 64 MiB。符号缺失、调用栈不完整、丢失样本或达到上限都会限制结论。脚本不修改 perf 权限、不挂载 tracefs，也不会默默替换不支持的计数器。

## 4. Linux／WSL：检查内存与指令成本

`check` 显示没有 Valgrind 时，使用发行版包管理器安装。Ubuntu 可先检查 `apt-get -s install --no-install-recommends valgrind libc6-dbg`，再安装这些包。libc 符号有助于查看分配来源。perf 软件包应适合运行中的内核，WSL 内核名称可能不同于发行版软件包名称。

公开的统计示例会创建 profiler 和快照、打印结果并释放资源，不依赖维护者私有回归源码：

```sh
python3 tools/hx_analyze.py memcheck -B build-shared-linux -- \
    bin/hx_os_profile_statistics
python3 tools/hx_analyze.py callgrind -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
python3 tools/hx_analyze.py massif -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
```

Memcheck 检查非法访问、未初始化值使用和泄漏，并跟踪来源。退出码 86 表示检测到错误；确定泄漏、间接泄漏、可能泄漏都算失败。仍可达内存会显示，但不会自动认定为泄漏。其他非零退出也可能来自被测程序或工具自身，必须阅读报告。

Callgrind 默认统计指令数和调用关系，不是正常运行耗时或硬件缓存测量。Massif 记录堆快照及分配来源，不覆盖全部 RSS、映射页或协程栈。两者都不是内存正确性检查器。Valgrind 会显著放慢执行；超时需要单独排查，本身不等于内存缺陷。

Valgrind 使用普通带符号构建。脚本拒绝 `HX_OS_SANITIZER` 非空的目录；ASan／UBSan、TSan 仍分别在已有配置运行。内存池和有栈协程需要专项验证，已有 Sanitizer 纤程适配不等于通过 Valgrind 验收。不要用大范围忽略规则隐藏自有代码错误。

## 5. Windows：WPR 采集，WPA 查看

在管理员 PowerShell 中使用原生 Windows Python。WPR 负责采集，WPA 和可选的 xperf 来自 Windows Performance Toolkit。只有采集需要管理员权限，普通构建和使用本库不需要。

```powershell
python tools/hx_analyze.py wpr -B build-shared-msvc -- `
    bin/Release/hx_os_benchmark.exe 20000000
wpa build-shared-msvc/hx-analysis/wpr/trace.etl
```

配套 `hx_cpu.wprp` 使用 16 MiB 循环 CPU 采样缓冲。脚本保留已有 WPR 录制，使用独立实例名；即使被测程序失败，也会停止本次采集。临时文件放在选定输出目录。长时间录制可能覆盖较早样本；ETL 还包含元数据，最终大小可以超过环形缓冲大小。不启用堆跟踪，不修改应用注册表。

在 WPA 中添加 **CPU Usage (Sampled)**，按 `run.log` 中的程序和 PID 筛选，再按 **Process → Thread ID → Stack** 展开。将匹配的 EXE／DLL PDB 所在目录加入符号路径并加载。安装了 xperf 时，脚本还会使用程序旁的符号导出函数摘要，并将符号缓存留在本地目录。系统符号可能无法解析，不会自动访问符号服务器。ETL 包含其他宿主进程，应保持私有并先筛选，再解读百分比。xperf 与 perf 的百分比分母不同，不能直接比较两者数值。

## 6. 换成自己的任务，理解报告边界

`--` 后面是程序及其参数。路径相对 `-B`，工作目录也是它；也可以使用绝对路径。将基准程序替换为有限运行、有代表性的业务示例即可。

每种模式复用 `<build>/hx-analysis/<模式>`。再次运行需要 `--replace`，覆盖该模式的已知结果文件，保留其他模式。默认 `--timeout 120` 限制任务时间；超时或 Ctrl+C 会停止 Windows 下本次启动的进程树，或 Linux 下本次新建的进程组，并记录未完成状态 124。因此应使用有限开发程序，不要托管有未保存工作的生产应用，也不用于自行脱离进程组的守护服务。脚本不附加或终止已有业务进程。

| 文件 | 含义 |
| --- | --- |
| `result.json` | 命令、符号配置确认、经过时间、退出码 |
| `run.log` | 程序输出、PID、中断信息 |
| `report.txt` | 所选工具的文本报告，取决于工具是否可用 |
| `perf.data`、`callgrind.out`、`massif.out`、`trace.etl` | 对应查看器使用的原始数据 |

退出码 0 表示选定工具和任务按其规则完成，不代表全库正确。函数占比高、分配器保留缓存或 RSS 较大，本身不足以证明缺陷。CPU 采样、正常耗时测量、内存诊断应分别理解。重新构建之前保留与报告匹配的二进制和符号。

所有 `hx-analysis/` 目录均被忽略，回归、原始报告、日志和本机配置不提交。安装包的文档目录在 `tools/` 下附带脚本和配置，使用时指向业务工程已有构建目录并保留匹配符号。

## 7. 回到普通构建

```sh
cmake -S . -B build-shared-linux -DHX_OS_DEVELOPER_ANALYSIS=OFF
```

Windows 换成已有 MSVC 目录。重新构建受影响目标后，二进制不再使用额外分析选项。OFF 恢复先前构建行为，保留独立区间 profiler，也不删除本地报告。ON、OFF 都不会自动开始采集。

参考：[perf](https://perfwiki.github.io/main/tutorial/)、[Valgrind](https://valgrind.org/docs/manual/manual.html)、[WPR 配置](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/authoring-recording-profiles)、[WPR／WPA](https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr)。
