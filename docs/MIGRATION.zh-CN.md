# `hx_c_log` 底层平台层迁移设计指南

[English](MIGRATION.md) · [返回首页](../README.zh-CN.md)

本文记录将兄弟项目 `hx_c_log` 的私有平台适配层迁移至 `hx_c_platform` 基础库的设计映射与分阶段集成准则。

---

## 1. 背景与解耦目标

`hx_c_log` 早期的私有平台头文件直接包含了 `hx_log.h`，在底层文件/线程/网络代码中直接耦合了日志 Sink 配置、崩溃处理选项、日志器内存分配器与线程退出 TLS 清理逻辑。

`hx_c_platform` 将这些通用系统能力抽离为独立、零日志耦合的基础库：
- 采用独立的不透明句柄、单调截止时间（`hx_os_deadline`）、显式内存序原子操作、UTF-8 路径自动转 UTF-16、部分写入进度报告（Partial Write Progress）以及原生线程退出 TLS 析构。
- 提供独立的 `hx_os_status` 状态码、`hx_os_error` 原生错误诊断以及按上下文隔离的 `hx_os_context` 分配器。

---

## 2. 薄适配层接口映射表

在 `hx_c_log` 内部通过一层薄适配（Seam Adapter）对接 `hx_c_platform` 时，原私有接口与基础库接口的对应关系如下：

| 原 `hx_c_log` 私有平台接口 | `hx_c_platform` 对应接口与适配职责 |
| --- | --- |
| `hx_atomic_load/acquire/store/release/add/cas` | 使用带显式内存序的 `hx_os_atomic_*` 系列接口 |
| `hx_atomic_ptr_*` / `hx_cpu_relax` | 使用指针原子操作接口与 `hx_os_cpu_relax()` |
| `hx_mutex_*` | 使用不透明互斥锁 `hx_os_mutex`（按原需求保留递归锁选项） |
| `hx_event_create/notify/wait` | 使用保留单次通知的自动复位事件 `hx_os_event` 并转换截止时间 |
| `hx_thread_start/join/is_current` | 使用 `hx_os_thread` 包装入口函数，分离 `join` 与 `destroy` 步骤 |
| `hx_mono_ms` / `hx_now_ms` / `hx_sleep_ms` | 映射为单调纳秒时钟、墙上毫秒时钟与 `hx_os_sleep_until()` |
| `hx_file_open` / `open_read` / `read_some` | 使用 `hx_os_file` 标志位，分离 `hx_os_status` 返回值与实际读取字节数 |
| `hx_file_write_progress` | 使用 `hx_os_file_write_all()` 并接收累计写入的 `size_t` 字节进度 |
| `hx_file_flush(durable)` | 先冲刷日志器自身的用户态缓冲，再按需调用 `hx_os_file_sync()` |
| `hx_file_size` / `move` / `remove` / `stat` | 使用强类型文件信息结果与统一 `hx_os_error` 错误结构 |
| `hx_make_directories` / `directory_scan` | 调用基础库目录创建与枚举接口，日志轮转扫描策略仍留在 `hx_c_log` 层 |
| `hx_native_path` / `hx_wide_utf8` | 直接使用基础库 UTF-8 路径接口或显式编码转换接口 |
| `hx_tls_get/set` | 由 `hx_c_log` 持有 `hx_os_tls_key` 并注册日志线程缓冲区的析构回调 |
| `hx_alloc` / `hx_free` | 为日志器实例创建专属 `hx_os_context` 以桥接自定义分配器 |
| `hx_network_*` | 在基础库 Socket / Event Loop 之上保留日志 Sink 既有分帧、重连与背压策略 |
| `hx_platform_capabilities` | 仅映射当前基础库二进制实际启用的模块能力位 |

---

## 3. 迁移时的关键语义差异与注意事项

在逐项替换底层调用时，需特别注意以下语义边界：

1. **事件超时与信号消费顺序**：
   旧版事件 `wait` 在发现截止时间已过时会直接返回超时而不消费已就绪信号；`hx_c_platform` 的事件在使用 `HX_OS_DEADLINE_NOW` 或到期检查时，若信号已就绪则会优先消费信号并返回成功。适配层需根据日志器原有契约保持可观测行为一致。
2. **线程 `join` 与对象销毁分离**：
   `hx_os_thread_join_until()` 成功后，还需显式调用 `hx_os_thread_destroy()` 释放线程句柄内存。
3. **32 位平台的长度截断检查**：
   在 32 位目标平台上传递 `uint64_t` 长度到接收 `size_t` 的接口前，必须先检查是否超过 `SIZE_MAX` 或分段处理，严禁静默截断。
4. **切勿混用不同时钟源的截止时间**：
   构造截止时间与等待调用必须同步迁移到 `hx_os_deadline`（单调纳秒），切勿将旧版毫秒时间戳直接乘以 `1,000,000` 当作 `hx_os_deadline` 传入。
5. **保持日志器专属能力在 `hx_c_log` 层**：
   日期字符串缓存、与 locale 无关的高速数值格式化、控制台/系统日志（Syslog/EventLog）Sink、日志专用有序异步 MPSC 队列、崩溃信号处理器安装以及日志历史回溯环（Backtrace Ring）导出仍由 `hx_c_log` 自身维护（不要用通用线程池替换日志器的单消费者有序输出队列）。Windows WER 注册表配置可通过基础库提供的 [WER LocalDumps 接口](LOCAL_DUMPS.zh-CN.md) 显式按需调用。

---

## 4. 分阶段推进与验证步骤

建议按以下可独立回退的阶段逐步完成迁移：

1. **冻结基线**：固定 `hx_c_log` 现有公共头文件 ABI、回归测试结果与基准性能数据（Windows 与 WSL2）。
2. **引入可选适配目标**：在 CMake 中引入 `hx_c_platform` 依赖与私有适配层目标，保持 `hx_log.h` 公共 ABI 完全不变。
3. **分批替换原语**：先替换状态码、时钟与路径转换；再替换文件读写、同步原语、线程生命周期与 TLS；最后迁移网络传输底层。每完成一步均运行完整回归测试。
4. **核对生命周期与关闭预算**：保留 `hx_c_log` 默认的 2 秒关闭等待预算，并将其转换为 `hx_os_deadline` 传给基础库关闭接口；超时后继续保留资源与 `user` 数据以支持重试。
5. **性能与打包回归验证**：验证单生产者与多生产者（8 线程）含最终落盘冲刷的吞吐量与尾延迟分位数不回退，并确认静态库（`find_dependency(hx_c_platform)`）与动态库安装在消费端工程中链接正常。
