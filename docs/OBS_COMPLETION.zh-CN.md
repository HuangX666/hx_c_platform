# OBS 能力补齐进度

[English](OBS_COMPLETION.md)

本页保留**最初 24 批次的历史验收记录**。当时的基线为原 `main` 分支上的 `7cf52fc`；当时的分支名、测试数量和导出数量只描述那个阶段。当前开发使用 `dev`，项目已经发布到远端。后续扩展以[当前验证记录](VALIDATION.zh-CN.md)、[Android 运行说明](ANDROID.zh-CN.md)和[依赖构建](DEPENDENCIES.zh-CN.md)为准。

当前验收范围是 Windows、WSL Linux、Android，以及现有模拟器／QEMU 环境中的 ARMv7、AArch64。实体板卡结果单独记录；macOS 和旧版 Windows 运行验证暂缓。远端 CI 仅保留手动入口，本次未运行。

| 批次 | 能力 | 状态 |
| --- | --- | --- |
| 1 | 系统信息与进程 CPU 采样 | Windows/Linux 12/12；ASan 3/3；目标实测待补 |
| 2 | 动态库 | Windows/Linux 14/14；ASan 3/3；目标实测待补 |
| 3 | 文件与路径便利操作 | Windows/Linux 16/16；ASan 4/4；目标限制已记录 |
| 4 | 环境变量 | Windows/Linux 18/18；ASan 4/4；目标实测待补 |
| 5 | 特殊路径 | Windows/Linux 20/20；ASan 6/6；目标实测待补 |
| 6 | 线程命名、栈选项、信号量 | Windows/Linux 22/22；ASan 5/5；目标实测待补 |
| 7 | 同步与异步管道 | Windows/Linux 24/24；ASan 7/7；TSan 5/5；目标实测待补 |
| 8 | 套接字选项与定位文件 I/O | Windows/Linux 26/26；ASan 6/6；TSan 4/4；目标实测待补 |
| 9 | 扩展 stat、目录类型、BOM | Windows/Linux 28/28；ASan 5/5；目标实测待补 |
| 10 | 配置序列化与浮点转换 | Windows/Linux 30/30；ASan 4/4；TSan 4/4；目标实测待补 |
| 11 | 周期定时器 | Windows/Linux 32/32；ASan 6/6；TSan 6/6；目标实测待补 |
| 12 | 线程池工作线程身份查询 | Windows/Linux 32/32；ASan 3/3；TSan 3/3；目标实测待补 |
| 13 | GAI 与 Mach 错误文本 | Windows/Linux 33/33；ASan 3/3；TSan 3/3；Mach 目标待验证 |
| 14 | 随机、UUID、CRC32、安全清零 | Windows 35/35；Linux 37/37；ASan/TSan 7/7；目标待验证 |
| 15 | 按页内存与映射 | Windows 37/37；Linux 39/39；ASan/TSan 4/4；x86 本批运行通过 |
| 16 | 文件锁、时间戳、权限 | Windows 39/39；Linux 41/41；ASan/TSan 4/4；x86 本批通过 |
| 17 | 目录监听与外部描述符集成 | Windows 42/42；Linux 44/44；ASan/TSan 7/7；Apple 原生待验证 |
| 18 | 进程、输出捕获与系统工具 | Windows 46/46；Linux 48/48；ASan/TSan 7/7；x86 本批通过 |
| 19 | 优先级、亲和性、延迟任务 | Windows 48/48；Linux 50/50；ASan/TSan 7/7；x86 本批通过 |
| 20 | 本地套接字与双向管道 | Windows 51/51；Linux 53/53；ASan/TSan 8/8；x86 本批通过 |
| 21 | 注册表、电源与混淆 | Windows 57/57；Linux 59/59；ASan 8/8；x86 5/5；Apple 待验证 |
| 22 | 日期、翻译表、位流 | Windows 63/63；Linux 65/65；ASan 8/8；TSan 6/6；x86 7/7 |
| 23 | 信号、可变设置、区间 profiler | 已实现：Windows 72/72，Linux 74/74；profiler ASan/TSan 4/4；目标实测待补 |
| 24 | 原子与内存序 | Windows 74/74；Linux 76/76；ASan/TSan 10/10；x86 5/5 |

基线保留既有空白格式，包括第三方许可证的原始内容。构建产物、依赖缓存与安装输出不纳入 Git。

在最初 24 批次阶段，全部实现批次已完成。Windows／WSL 静态与共享安装消费、576 符号导出、旧头文件配新共享库的 ABI 验证通过。完整检查器、Clang、x86、CMake 3.16、 QEMU 部分运行和目标限制见[最终矩阵](VALIDATION.zh-CN.md)。这不代表 macOS／Win7／真实板卡验收完成，也不代表 hx_c_log 性能已达标。
