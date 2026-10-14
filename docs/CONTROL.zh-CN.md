# 区间计时、重试延迟与限流

[English](CONTROL.md) · [README](../README.zh-CN.md)

## 1. 三种工具分别做什么

`<hx_os_control.h>` 提供三个可以放在栈上的小对象。它们不分配内存、不创建线程，也不会暗中休眠或重试业务操作。全部属于始终可用的核心，不依赖可选宽位运算模块。复制对象得到独立快照；使用前先初始化，共享对象的修改由调用者串行化。输出变量不得与状态对象重叠。不要直接修改成员，也不要把 `&watch.started_ns`、`&bucket.updated_ns` 等成员地址当作输出参数。

| 工具 | 常见用途 | 时钟 |
| --- | --- | --- |
| `hx_os_stopwatch` | 测量一次操作，或分阶段重新计时 | 已有高精度区间时钟 |
| `hx_os_backoff` | 计算下一次有次数限制的重试延迟 | 不读取时钟，返回毫秒时长 |
| `hx_os_token_bucket` | 允许短时突发，再限制平均令牌速率 | 已有单调截止时间时钟 |

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

`elapsed_ns()` 只查询，不重置起点。`restart()` 返回上一段耗时，并用同一次时钟读数开始下一段。`start()` 也可以直接重新开始，不需要读取上一段时长。零初始化的对象必须先 start，才能查询耗时或 restart。

时钟失败或读数倒退时，保留对象和输出原值，并保留原生时钟诊断。返回单位是纳秒，不代表硬件真的有一纳秒分辨率，相邻读数可以相同。系统挂起是否计入时间遵循各平台 `hx_os_high_resolution_read()` 的原生行为。秒表时间不是截止时间，不能拿来与 `hx_os_sleep_until()` 或令牌桶的单调时间混用。

## 3. 有次数上限的退避延迟

options 为 NULL 时，默认初始延迟 100 ms、名义最大延迟 30,000 ms、八次重试、20% 抖动、种子 1。`HX_OS_BACKOFF_OPTIONS_INIT` 提供同样的默认值，方便局部修改。`initial_ms` 必须非零，`max_ms` 不小于它，抖动比例允许 0…100%。重试次数为零合法，表示立即耗尽。

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

每次 next 成功，会消耗一次重试额度。名义延迟翻倍增长，在 `max_ms` 饱和，不发生整数回绕。关闭抖动后，默认值依次产生 100、200、400、800、1600、3200、6400、12800 ms，然后返回 `HX_OS_EOF`。第一次业务调用不属于这八次重试。推进这个状态时，不会调用业务函数，也不会让线程睡眠。

名义延迟为 `n`、抖动比例为 `p`，令 `d=floor(n*p/100)`，实际结果位于 `[n-d, min(max_ms,n+d)]`。带抖动的实际延迟不保证逐次增大，100% 抖动可能产生零。内部是确定性的非密码学伪随机生成器，取模映射不承诺完全均匀。种子零按一处理。相同种子的对象会产生相同序列，因此需要避免多个客户端同时重试时，应给它们设置不同种子；默认种子不会自动随机化。

`reset()` 恢复初始延迟、重试次数和原随机序列。耗尽或失败都保留状态和输出原值。错误能不能重试、是否取消、整个操作是否还有总截止时间、何时睡眠，都由调用者决定。仅限制重试次数，不能限制第三方阻塞函数内部一次调用的耗时。

## 4. 不暗中等待的令牌桶

创建时指定容量和正整数“每秒补充令牌数”，初始为满桶。`try_take()` 要么完整取走指定数量，要么一个也不取；请求大于容量时返回 `HX_OS_LIMIT`。请求零个令牌立即成功，不读取时钟，也不改变对象。

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

容量 20、速率 5，表示一开始允许消耗 20 个令牌，此后每秒补充五个。令牌可以代表消息、字节或业务自行选择的单位；容量、速率、单次申请量必须使用相同单位。这是允许短时突发的限流方式，不保证每一个滑动的一秒窗口内都严格只有固定次数。

补充按实际经过的单调时间计算，使用整数并保留不足一个令牌的余数。满桶期间不会额外积攒超过容量的额度。没有浮点精度损失或乘法回绕。`HX_OS_OK` 把 `retry_at` 设为 NOW；`HX_OS_WOULD_BLOCK` 更新补充状态，返回按当前欠缺量计算的最早单调重试时间，但不会预约将来的令牌。其他调用者可能先取走令牌，所以到时重试仍然需要再次检查结果。

无法表示的极远重试时间饱和到 `UINT64_MAX-1`，即最大的有限截止时间。这是有界建议，不保证在这个表示范围上限处一定已有足够令牌。建议时间不会返回 INFINITE。时钟失败或倒退时保留状态及 `retry_at`；参数错误和申请超过容量时也保留原值。每个正数请求只读取一次时钟。多个线程共享同一桶时，需要外部加锁。

## 5. C++11 与可运行示例

包含 `<hx/Control.hpp>` 后，可以使用：

- `hx::Stopwatch::start()` 返回 `Result<Stopwatch>`；`elapsedNs()`、`restart()` 返回 `Result<uint64_t>`。
- `hx::Backoff::create(options=nullptr)` 返回 `Result<Backoff>`；`next()` 返回延迟或 EOF，`reset()` 从原序列重新开始。
- `hx::TokenBucket::create(capacity, rate)` 返回 `Result<TokenBucket>`；`tryTake(amount=1)` 返回 `Result<hx_os_deadline>`。状态为 `HX_OS_WOULD_BLOCK` 时，`hasValue()` 为真，`value()` 中仍然保留建议重试时间；其他失败不携带截止时间。

包装层不增加分配，也不使用异常。默认构造对象不会暗中开始计时或建立策略，应使用这些工厂入口取得初始化对象后再操作。

开启 examples 后，构建 `hx_os_control`。完整 [C 示例](../examples/control.c) 打印三个计算出的重试延迟，报告次数耗尽，演示两个令牌的初始突发，并读取整个示例区间耗时。它不会发起网络请求或睡眠，重试与调度策略始终由开发者明确编写。时间数值会随运行环境变化。
