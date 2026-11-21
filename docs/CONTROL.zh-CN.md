# 区间计时、重试延迟与限流

[English](CONTROL.md) · [README](../README.zh-CN.md)

当业务需要测量某个阶段的执行耗时、在网络请求失败后计算带抖动的指数退避重试延迟，或者使用令牌桶对发送频率进行突发限流时，可以使用 `<hx_os_control.h>` 提供的三个纯栈上轻量工具：秒表（`hx_os_stopwatch`）、指数退避（`hx_os_backoff`）和令牌桶（`hx_os_token_bucket`）。

### 快速运行示例

完整示例位于 [control.c](../examples/control.c)：启动秒表计时，通过 `hx_os_backoff_next()` 依次计算 3 次重试延迟直至额度耗尽，使用 `hx_os_token_bucket_try_take()` 消费初始突发额度并观察限流后返回的建议重试截止时间，最后通过 `hx_os_stopwatch_elapsed_ns()` 读取总耗时。

按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_control`（无需命令行参数或输入文件）：

```sh
cmake --build build --config Release --target hx_os_control
```

预期输出包含 3 次计算出的退避延迟、`Retry budget exhausted.`、被限流时的建议重试时间以及本次运行耗时。接入自己的工程时，可参考[最小工程配置](USAGE.zh-CN.md#c-project)直接使用。

## 1. 三种工具分别做什么

这三个结构体均可直接放置在栈上：它们**零堆内存分配、不创建后台线程，也不会暗中阻塞睡眠或替业务自动重发请求**。三者均属于始终编入的核心模块，按值复制即可得到独立的状态快照：

| 工具结构体 | 典型场景 | 使用的时钟源 |
| --- | --- | --- |
| `hx_os_stopwatch` | 测量单次操作耗时，或在多个阶段之间连续分段计时 | 高精度区间时钟（`hx_os_high_resolution_read()`） |
| `hx_os_backoff` | 计算下一次有次数上限的指数退避延迟（含可配置抖动） | 纯确定性计算，不读取时钟，输出毫秒数 |
| `hx_os_token_bucket` | 允许短时突发流量，并限制长期平均令牌消耗速率 | 单调截止时间时钟（`hx_os_deadline`） |

> [!NOTE]
> **调用约定**
> 使用前请先调用对应的初始化/启动函数；若多个线程共享同一个状态对象，请由调用方在外部加锁。请勿直接修改结构体内部字段，也不要将结构体成员地址（如 `&watch.started_ns` 或 `&bucket.updated_ns`）作为输出参数传入。

## 2. Stopwatch 秒表

```c
hx_os_stopwatch watch;
uint64_t elapsed;
hx_os_status status = hx_os_stopwatch_start(&watch, NULL);

if (status == HX_OS_OK) {
	// Perform the operation whose duration is needed.
	status = hx_os_stopwatch_elapsed_ns(&watch, &elapsed, NULL);
}
```

- **读取与分段重启**：
  - `hx_os_stopwatch_elapsed_ns()` 只读取从起点到当前的纳秒耗时，不重置起点。
  - `hx_os_stopwatch_restart()` 返回上一阶段的耗时，并在同一次时钟采样点上无缝开始下一阶段计时。
  - `hx_os_stopwatch_start()` 可随时重新开始计时（零初始化的 `hx_os_stopwatch` 必须先调用 `start()` 才能调用 `elapsed_ns()` 或 `restart()`）。
- **时钟语义**：返回值单位为纳秒（`ns`），但实际精度取决于硬件高精度计时器分辨率（极短间隔的连续两次采样可能返回相同值）。秒表时间戳仅供计算相对耗时，**不是**单调截止时间（`hx_os_deadline`），切勿将其传给 `hx_os_sleep_until()` 或与令牌桶时间戳混用。

## 3. 有次数上限的退避延迟

使用 `hx_os_backoff_init(&backoff, &options, NULL)` 初始化退避状态。若 `options` 传 `NULL`（或使用 `HX_OS_BACKOFF_OPTIONS_INIT` 默认值），默认配置为：初始延迟 `100 ms`、最大名义延迟 `30,000 ms`、最多重试 `8` 次、抖动比例 `20%`、随机种子 `1`（要求 `initial_ms > 0`、`max_ms >= initial_ms`、抖动比例在 `0`～`100` 之间；`max_retries = 0` 合法，表示不允许重试，首次调用即返回 `HX_OS_EOF`）：

```c
hx_os_backoff backoff;
hx_os_backoff_options options = HX_OS_BACKOFF_OPTIONS_INIT;
uint64_t delay_ms;

options.seed = 17;
options.max_retries = 3;
hx_os_status status = hx_os_backoff_init(&backoff, &options, NULL);

if (status == HX_OS_OK)
	status = hx_os_backoff_next(&backoff, &delay_ms, NULL);
```

- **指数翻倍与次数耗尽**：每次调用 `hx_os_backoff_next()` 成功都会消耗 1 次重试额度，名义延迟按 2 倍递增并在 `max_ms` 处饱和（不会发生整数溢出）。在关闭抖动（`jitter_percent = 0`）时，默认配置依次输出 `100`、`200`、`400`、`800`、`1600`、`3200`、`6400`、`12800 ms`，第 9 次调用返回 `HX_OS_EOF`。
- **抖动计算与客户端去同步**：设当前名义延迟为 $n$、抖动百分比为 $p$、偏移量 $d = \lfloor n \times p / 100 \rfloor$，则实际返回的延迟落在 $[n - d, \min(\text{max\_ms}, n + d)]$ 区间内。内部使用轻量确定性伪随机数生成器（种子 `0` 与 `1` 等价）：**相同种子的实例会产生完全相同的退避序列**。为了避免多台客户端在故障恢复时同时发起重试惊群，请为不同客户端传入不同的 `seed`。
- **重置**：调用 `hx_os_backoff_reset()` 可将实例恢复到初始延迟、初始重试额度与初始随机序列。

## 4. 不暗中等待的令牌桶

通过 `hx_os_token_bucket_init(&bucket, capacity, refill_per_second, NULL)` 初始化令牌桶（初始状态为满桶）。每次调用 `hx_os_token_bucket_try_take()` 时，要么一次性完整扣除请求的令牌数并返回 `HX_OS_OK`，要么一个也不扣并返回 `HX_OS_WOULD_BLOCK`（若单次请求数量超过桶总容量 `capacity`，则直接返回 `HX_OS_LIMIT`）：

```c
hx_os_token_bucket bucket;
hx_os_deadline retry_at;
hx_os_status status = hx_os_token_bucket_init(&bucket, 20, 5, NULL);

if (status == HX_OS_OK)
	status = hx_os_token_bucket_try_take(&bucket, 1, &retry_at, NULL);

if (status == HX_OS_WOULD_BLOCK) {
	// Defer work until retry_at, or report backpressure to the caller.
}
```

- **容量与补充速率**：上面代码中容量为 `20`、速率为 `5`，代表允许初始瞬间突发消耗 `20` 个令牌，随后按每秒 `5` 个令牌的速率连续补充（满桶时不会超额累积）。内部完全采用无溢出的整数运算并保留不足 1 个令牌的时间余数，无浮点舍入误差。
- **建议重试截止时间（`retry_at`）**：
  - 当返回 `HX_OS_OK` 时，`retry_at` 被设为 `HX_OS_DEADLINE_NOW`。
  - 当令牌不足返回 `HX_OS_WOULD_BLOCK` 时，函数会更新桶内补充进度，并根据当前缺口计算出最早攒够所需令牌的单调时钟截止时间写入 `retry_at`（极远时间在 `UINT64_MAX - 1` 处饱和，绝不会返回 `HX_OS_DEADLINE_INFINITE`）。
  - 注意：`HX_OS_WOULD_BLOCK` **不会预留**未来的令牌；如果多个调用方共享同一个桶，到达 `retry_at` 后再次调用 `try_take()` 仍需检查返回状态。

## 5. C++11 与可运行示例

包含 `<hx/Control.hpp>` 即可使用零堆分配、无异常的 C++11 封装：
- `hx::Stopwatch::start()`：返回 `Result<Stopwatch>`；成员方法 `elapsedNs()` 和 `restart()` 返回 `Result<uint64_t>`。
- `hx::Backoff::create(options = nullptr)`：返回 `Result<Backoff>`；`next()` 返回下一次毫秒延迟或 `HX_OS_EOF`，`reset()` 重置序列。
- `hx::TokenBucket::create(capacity, rate)`：返回 `Result<TokenBucket>`；`tryTake(amount = 1)` 返回 `Result<hx_os_deadline>`。当限流触发导致状态为 `HX_OS_WOULD_BLOCK` 时，`hasValue()` 依然为 `true`，可直接通过 `value()` 读取建议重试截止时间 `retry_at`。

启用示例后构建 `hx_os_control`，即可运行完整的 [C 示例](../examples/control.c) 验证上述三项工具的完整交互。
