# Interval timing, retry delays and rate limits

[简体中文](CONTROL.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Choose backoff, a token bucket or a stopwatch for retry delays, send-rate limits or elapsed-time measurement.

Open [control.c](../examples/control.c). stopwatch_start → backoff_next computes three delays → token_bucket_try_take requests capacity → elapsed_ns measures the interval.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_control`. This program needs no arguments or input files.

**Expected result:** It prints three delays, `Retry budget exhausted.`, a retry deadline when rate-limited, and elapsed time. Timing varies; the helpers do not retry or send requests for you.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. What each tool does

`<hx_os_control.h>` contains three small stack values. They allocate no memory, create no threads and perform no hidden sleep or business retry. All are always available in the core; they do not depend on optional wide-math functionality. Copying one creates an independent snapshot. Initialize before use and externally serialize mutations of a shared object. Output variables must not overlap the state object. Do not modify fields or use fields as output parameters (for example, `&watch.started_ns` or `&bucket.updated_ns`).

| Tool | Common use | Clock |
| --- | --- | --- |
| `hx_os_stopwatch` | Measure one operation or restart between stages | Existing high-resolution interval clock |
| `hx_os_backoff` | Calculate the next bounded retry delay | No clock reads; returns milliseconds |
| `hx_os_token_bucket` | Allow a burst, then limit the average token rate | Existing monotonic deadline clock |

## 2. Stopwatch

```c
hx_os_stopwatch watch;
uint64_t elapsed;
hx_os_status status = hx_os_stopwatch_start(&watch, NULL);

if (status == HX_OS_OK) {
	// Perform the operation whose duration is needed.
	status = hx_os_stopwatch_elapsed_ns(&watch, &elapsed, NULL);
}
```

`elapsed_ns()` observes without resetting the start. `restart()` returns the completed interval and starts the next interval from the same clock reading. `start()` also permits restarting directly without obtaining a previous duration. A zero-initialized stopwatch must be started before elapsed/restart operations.

Failures, including clock errors or a backwards reading, preserve state and output; native clock diagnostics are retained. The returned nanosecond unit does not imply nanosecond hardware resolution, and consecutive samples may be equal. Suspend behavior follows `hx_os_high_resolution_read()` on each platform. A stopwatch timestamp is not a deadline: never combine it with `hx_os_sleep_until()` or monotonic token-bucket timestamps.

## 3. Backoff with a finite retry count

Passing NULL options selects an initial 100 ms delay, a 30,000 ms nominal maximum, eight retries, 20% jitter and seed 1. `HX_OS_BACKOFF_OPTIONS_INIT` provides the same values for customization. `initial_ms` must be nonzero, `max_ms` must be at least `initial_ms`, and jitter must be 0…100 percent. Zero retries is valid and immediately exhausted.

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

Each successful `next()` consumes one retry allowance. Nominal delays double without integer overflow and saturate at `max_ms`. With jitter disabled, the defaults produce 100, 200, 400, 800, 1600, 3200, 6400 and 12800 ms, then `HX_OS_EOF`. The initial business attempt is not one of these retries. No business operation is invoked and no thread sleeps when advancing this state.

For nominal delay `n` and jitter percentage `p`, let `d=floor(n*p/100)`. The returned delay is in `[n-d, min(max_ms,n+d)]`; actual jittered delays need not increase, and 100% jitter can produce zero. The small deterministic generator is noncryptographic and modulo mapping is not perfectly uniform. Seed zero maps to one. Different instances with the same seed produce the same sequence, so assign different seeds to independent clients if synchronized retries must be avoided. The default seed is not automatically randomized.

`reset()` restores the original nominal delay, retry allowance and random sequence. Exhaustion or failure leaves state and the output delay unchanged. The caller decides whether an error is retryable, whether to cancel, whether a whole operation has a deadline, and whether to sleep. A retry count alone does not bound time spent inside a blocking third-party call.

## 4. Token bucket without hidden waiting

Create a bucket with a capacity and a positive integer tokens-per-second rate. It starts full. `try_take()` consumes the entire requested amount or consumes none; an amount above capacity returns `HX_OS_LIMIT`. A zero amount succeeds without reading the clock or changing state.

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

Capacity 20 and rate 5 allow an initial burst of 20 tokens, then replenish five per second. Tokens can represent messages, bytes or another unit selected by the application; use the same unit for capacity, rate and amount. This is a burst-tolerant limiter, not a guarantee of a strict number of events in every sliding one-second window.

Refill uses elapsed monotonic time, integer arithmetic and carried fractional tokens. Time at full capacity does not accumulate credit beyond capacity. No floating-point rounding or multiplication overflow is used. `HX_OS_OK` sets `retry_at` to NOW; `HX_OS_WOULD_BLOCK` updates refill state and returns the earliest calculated monotonic retry time for the current deficit, without reserving future tokens. Other callers may consume available tokens before that time, so a later attempt must check again.

Extremely distant retry times saturate at `UINT64_MAX-1`, the largest finite deadline. This is a bounded suggestion, not a promise that the requested tokens are available at that representable limit. The bucket never returns INFINITE as a retry suggestion. Clock failures or backwards readings preserve state and `retry_at`; invalid arguments and requests above capacity also preserve them. Each positive request reads the clock once. External locking is required when multiple threads share a bucket.

## 5. C++11 and a runnable example

Include `<hx/Control.hpp>` for these direct entry points:

- `hx::Stopwatch::start()` returns `Result<Stopwatch>`; `elapsedNs()` and `restart()` return `Result<uint64_t>`.
- `hx::Backoff::create(options=nullptr)` returns `Result<Backoff>`; `next()` returns the delay or EOF, and `reset()` restarts the same sequence.
- `hx::TokenBucket::create(capacity, rate)` returns `Result<TokenBucket>`; `tryTake(amount=1)` returns `Result<hx_os_deadline>`. When its status is `HX_OS_WOULD_BLOCK`, `hasValue()` is true and `value()` contains the suggested retry deadline. Other errors do not carry a deadline.

The wrappers add no allocations or exceptions. Default-constructed objects need initialization through these factories before useful operation; they do not silently start clocks or create a policy.

Build `hx_os_control` with examples enabled. The complete [C example](../examples/control.c) prints three calculated retry delays, reports exhaustion, demonstrates a two-token burst and observes elapsed time. It does not perform network requests or sleep; retry and scheduling policy remain visible to the developer. Timing values naturally vary between runs and platforms.
