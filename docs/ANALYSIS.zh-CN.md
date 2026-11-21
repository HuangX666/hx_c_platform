# 可选的 CPU 与内存分析

[English](ANALYSIS.md) · [区间计时](PROFILER.zh-CN.md) · [README](../README.zh-CN.md)

当你在开发或调优阶段需要定位**哪些函数占用了最多 CPU 时间**、检查是否存在**内存越界/未初始化读取/内存泄漏**，或者分析**堆内存峰值是由哪条调用链分配的**时，可以使用本页介绍的外部开发分析工具流（`tools/hx_analyze.py`）。它与代码内手动插桩的[区间计时器（Profiler）](PROFILER.zh-CN.md)形成互补：普通构建默认关闭分析符号，且开启编译期符号开关绝不会在运行时自动启动后台采集线程或安装外部工具。

如果你需要针对部署在客户 Windows 机器上的正式版程序收集闪退现场（`.dmp`），请使用独立的 [WER LocalDumps 配置接口](LOCAL_DUMPS.zh-CN.md)——开发期性能分析开关或 WPR 录制不会自动启用崩溃转储，反之崩溃转储也不能替代开发期的内存越界检查与 CPU 采样。

## 1. 先选择要回答的问题

根据你想排查的问题，选择对应的 `hx_analyze.py` 分析模式：

| 想排查什么问题 | 分析模式 | 适用平台与底层工具 | 生成的核心结果文件 |
| --- | --- | --- | --- |
| 哪些函数消耗了最多用户态 CPU 时间？ | `perf` | Linux / WSL（`perf`） | `report.txt`、`perf.data` |
| 是否存在内存越界、使用未初始化值或内存泄漏？ | `memcheck` | Linux / WSL（Valgrind Memcheck） | `report.txt` |
| 哪些调用路径执行的 CPU 指令数最多？ | `callgrind` | Linux / WSL（Valgrind Callgrind） | `report.txt`、`callgrind.out` |
| 进程堆内存峰值主要来自哪些分配调用栈？ | `massif` | Linux / WSL（Valgrind Massif） | `report.txt`、`massif.out` |
| Windows 下哪些线程与函数消耗了最多 CPU？ | `wpr` | Windows（WPR 录制 + WPA / 可选 `xperf`） | `trace.etl`（安装 `xperf` 时额外生成 `report.txt`） |

在仓库根目录下使用 Python 3.9+ 运行辅助脚本（Python 仅作为开发期包装脚本使用，库运行时完全不依赖 Python）。你可以先运行以下命令检查当前机器上已安装了哪些分析工具（Linux 下通常使用 `python3`）：

```sh
python3 tools/hx_analyze.py check
```

## 2. 在已有构建目录中启用符号

为了在采样报告中看到清晰的函数名与源码行号，只需在现有的构建目录上开启 `-DHX_OS_DEVELOPER_ANALYSIS=ON` 并重新编译目标程序（以下命令以 `build-shared-linux` 和 `build-shared-msvc` 为例，请替换为你实际使用的构建目录；第三方依赖请先按[依赖指南](DEPENDENCIES.zh-CN.md)准备完毕）：

```sh
# Linux / macOS：
cmake -S . -B build-shared-linux -DCMAKE_BUILD_TYPE=Release \
    -DHX_OS_DEVELOPER_ANALYSIS=ON -DHX_OS_BUILD_BENCHMARKS=ON \
    -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-linux --target \
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

```powershell
# Windows PowerShell（Visual Studio 生成器）：
cmake -S . -B build-shared-msvc -DHX_OS_DEVELOPER_ANALYSIS=ON `
    -DHX_OS_BUILD_BENCHMARKS=ON -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build-shared-msvc --config Release --target `
    hx_os_benchmark hx_os_profiler_benchmark hx_os_profile_statistics
```

> [!IMPORTANT]
> **`HX_OS_DEVELOPER_ANALYSIS` 编译选项的作用范围**
> - `HX_OS_DEVELOPER_ANALYSIS` 默认为 `OFF`。开启后，它会为本仓库自有的 GCC/Clang C/C++ 目标添加私有的 `-g -fno-omit-frame-pointer`，为 MSVC 目标添加 `/Zi` 并生成完整的链接器 `.pdb` 符号文件（同时保留 `Release` 优化级别）。
> - 它**不会**修改第三方依赖目标的编译标志，也不会通过 CMake 导出目标污染下游消费者的编译选项（Windows 下执行 `Release` 分析时，必须链接匹配的 `Release` 依赖库，切勿混入 `Debug` 静态库或强行屏蔽 CRT 冲突警告）。
> - 由于保留帧指针（Frame Pointer）会略微影响寄存器分配与生成的机器码，做横向性能对比时请确保前后的分析开关、优化级别和负载参数完全一致。

## 3. Linux／WSL：查看 CPU 热点（`perf`）

使用 `perf` 模式运行基准测试程序并收集调用栈热点：

```sh
python3 tools/hx_analyze.py perf -B build-shared-linux -- \
    bin/hx_os_benchmark 20000000
perf report -i build-shared-linux/hx-analysis/perf/perf.data
```

- **调整迭代次数以获取充足样本**：`hx_os_benchmark` 支持传入可选的迭代次数参数（范围 `1 ～ 100,000,000`，默认 `1,000,000`）。传入 `20000000`（两千万次）可以让程序运行足够长的时间，从而在 `199 Hz` 的用户态 CPU 时钟采样率与 DWARF 调用栈展开下收集到具有统计意义的样本（无需硬件 PMU 计数器支持）。
- **WSL 文件系统兼容处理**：为了规避 WSL 挂载盘（DrvFS）不支持 `perf` 内存映射缓冲直接写出（会报 `Bad address` 错误）的问题，`hx_analyze.py` 会先在 Linux 原生 `/tmp` 临时目录中录制数据，录制完成后再自动将结果拷回 `<build>/hx-analysis/perf/` 并清理临时目录（采样数据大小上限设为 `64 MiB`）。
- **正确解读百分比**：生成的 `report.txt` 按动态库与函数名汇总采样占比。请注意：采样百分比反映的是“该函数在 CPU 采样点中出现的相对频率”，不等于单次请求的端到端延迟；一个高频调用的核心函数占比高属于正常现象，不能仅凭排名靠前就断定代码低效。

## 4. Linux／WSL：检查内存与指令成本（Valgrind）

如果 `hx_analyze.py check` 提示未安装 Valgrind，可通过系统包管理器安装（例如在 Ubuntu 上安装 `valgrind` 与 `libc6-dbg`，带调试符号的 `libc` 有助于精准定位底层分配栈）。

你可以直接针对公开示例 `hx_os_profile_statistics` 和 `hx_os_profiler_benchmark` 运行以下三种分析模式：

```sh
python3 tools/hx_analyze.py memcheck -B build-shared-linux -- \
    bin/hx_os_profile_statistics
python3 tools/hx_analyze.py callgrind -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
python3 tools/hx_analyze.py massif -B build-shared-linux -- \
    bin/hx_os_profiler_benchmark
```

- **`memcheck`（内存正确性与泄漏检查）**：检查非法内存读写、使用未初始化变量以及内存泄漏（开启未初始化值来源追踪）。若检测到任何非法访问，或发现确定泄漏（Definitely Lost）、间接泄漏（Indirectly Lost）、可能泄漏（Possibly Lost），Valgrind 会以退出码 `86` 报错退出（进程退出时仍可达的 `still reachable` 内存会在报告中列出，但不计为自动失败）。
- **`callgrind`（指令数与调用图分析）**：统计函数间调用次数与执行的 CPU 指令数量，用于分析算法复杂度与调用频次（注意：指令数统计不是真实墙钟耗时，也不反映硬件缓存未命中开销）。
- **`massif`（堆内存峰值分析）**：定期对分配器堆内存拍快照，找出进程堆内存峰值（Heap Peak）是由哪条代码路径分配的（注意：它只统计堆分配，不包含匿名 `mmap` 页、线程栈或进程全部 RSS）。
- **不要与 Sanitizer 混开**：运行 Valgrind 时，请使用开启了 `HX_OS_DEVELOPER_ANALYSIS=ON` 的普通构建目录；若构建目录同时开启了 `HX_OS_SANITIZER`（如 ASan/UBSan/TSan），`hx_analyze.py` 会明确拒绝运行，以免 Valgrind 与编译器 Sanitizer 运行时发生冲突。

## 5. Windows：WPR 采集，WPA 查看

在 Windows 上，使用 Windows Performance Toolkit（包含在 Windows ADK 中）提供的 `wpr`（录制）与 `wpa`（图形化分析器）。由于 ETW 内核级 CPU 采样需要管理员权限，请在**管理员 PowerShell** 中使用原生 Windows Python 运行录制命令（日常编译和使用本库不需要管理员权限）：

```powershell
python tools/hx_analyze.py wpr -B build-shared-msvc -- `
    bin/Release/hx_os_benchmark.exe 20000000
wpa build-shared-msvc/hx-analysis/wpr/trace.etl
```

- **独立环形缓冲会话**：脚本使用配套的 `tools/hx_cpu.wprp` 配置文件，开启一个带唯一会话名的 `16 MiB` 循环内存 CPU 采样缓冲；即便被测程序中途报错退出，脚本也会确保停止并清理该录制会话，不会干扰机器上其他已有的 WPR 会话。
- **在 WPA 中加载符号并筛选进程**：
  1. 打开 `trace.etl` 后，将 **CPU Usage (Sampled)** 图表拖入分析视图。
  2. 查看同目录下 `run.log` 中记录的被测进程名与 PID，在 WPA 中按该进程筛选（因为系统级 ETL 录制会包含机器上同时运行的其他进程，必须先筛选出目标进程再看百分比），并按 **Process → Thread ID → Stack** 展开调用栈。
  3. 在 WPA 菜单中将包含匹配 `.exe` / `.dll` `.pdb` 的构建目录加入符号路径并加载符号（如果机器上同时安装了 `xperf`，脚本还会自动利用本地符号导出文本摘要 `report.txt`；注意 `xperf` 与 Linux `perf` 的百分比计算分母不同，请勿跨平台直接横向比较两者的百分比数值）。

## 6. 换成自己的任务，理解报告边界

在命令行中，`--` 后面的部分就是要执行的被测程序及其命令行参数（相对路径以 `-B` 指定的构建目录为基准，工作目录同样为该构建目录）。你可以将上面的基准程序替换为任何能在有限时间内运行完毕的业务示例程序：

- **输出目录与 `--replace` 覆盖保护**：每种模式的结果保存在 `<build>/hx-analysis/<mode>/` 下。为防止误删上一次的分析报告，重复运行同一模式时必须显式加上 `--replace` 参数（它只会更新当前模式的输出文件，不会触碰其他模式的目录）。
- **超时保护（`--timeout`）**：默认超时时间为 `120 秒`（可通过 `--timeout <秒>` 调整，由于 Valgrind 会显著放慢程序运行速度，跑较重负载时可适当调大）。若超时或按下 `Ctrl+C`，脚本会终止本次启动的进程树（Windows）或新建进程组（Linux），并在结果中记录未完成退出码 `124`。请仅用它运行有限耗时的开发测试程序，不要用它启动带有未保存数据的生产服务或会自行脱离进程组的守护进程。

每次运行会在 `<build>/hx-analysis/<mode>/` 下生成以下文件：

| 文件名 | 内容说明 |
| --- | --- |
| `result.json` | 本次运行的完整命令行、符号配置校验状态、运行耗时与退出码 |
| `run.log` | 被测程序的标准输出/错误日志、进程 PID 以及超时/中断信息 |
| `report.txt` | 分析工具生成的文本摘要报告（在工具支持时生成） |
| `perf.data` / `callgrind.out` / `massif.out` / `trace.etl` | 供对应交互式查看器（`perf report`、`kcachegrind`、`ms_print`、`wpa`）打开的原始采样数据 |

## 7. 回到普通构建

完成性能与内存分析后，可将 `HX_OS_DEVELOPER_ANALYSIS` 重新设回 `OFF` 并重新编译受影响的目标，即可恢复不带额外帧指针与分析符号的常规构建（不会删除本地已生成的 `hx-analysis/` 报告目录）：

```sh
cmake -S . -B build-shared-linux -DHX_OS_DEVELOPER_ANALYSIS=OFF
```

外部工具官方文档参考：[Linux `perf` Tutorial](https://perfwiki.github.io/main/tutorial/)、[Valgrind User Manual](https://valgrind.org/docs/manual/manual.html)、[Authoring WPR Recording Profiles](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/authoring-recording-profiles) 以及 [WPR / WPA Performance Analysis](https://learn.microsoft.com/en-us/troubleshoot/windows-server/support-tools/support-tools-xperf-wpa-wpr)。
