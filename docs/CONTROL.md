# Interval timing, retry delays and rate limits

[简体中文](CONTROL.zh-CN.md) · [README](../README.md)

Use the lightweight stack-allocated helpers in `<hx_os_control.h>` when you need to measure elapsed execution time (`hx_os_stopwatch`), calculate bounded exponential-backoff delays with jitter (`hx_os_backoff`), or rate-limit requests with burst tolerance (`hx_os_token_bucket`).

### Quick Start Example

Open [control.c](../examples/control.c) for a complete runnable program: it starts a stopwatch, computes three retry delays with `hx_os_backoff_next()` until the retry budget is exhausted, consumes burst capacity from a token bucket with `hx_os_token_bucket_try_take()`, inspects the suggested retry deadline when rate-limited, and reads the total elapsed time via `hx_os_stopwatch_elapsed_ns()`.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_control` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_control
```

Expected output: three backoff delays, `Retry budget exhausted.`, a suggested retry deadline when rate-limited, and the elapsed interval duration. You can also copy [control.c](../examples/control.c) as your starting `main.c` in the [minimal project](USAGE.md#c-project) setup.

## 1. What each tool does

All three types are small stack values that **allocate no heap memory, spawn no threads, and never sleep or retry operations behind your back**. They are always available in the core library, and copying a struct creates an independent state snapshot:

| Tool | Common use | Clock source |
| --- | --- | --- |
| `hx_os_stopwatch` | Measure a single operation or consecutive pipeline stages | High-resolution interval clock (`hx_os_high_resolution_read()`) |
| `hx_os_backoff` | Compute the next bounded exponential retry delay with optional jitter | Pure arithmetic (no clock reads; outputs milliseconds) |
| `hx_os_token_bucket` | Allow a short burst and then cap the average token rate | Monotonic deadline clock (`hx_os_deadline`) |

> [!NOTE]
> **Initialization and thread-safety contract**
> Initialize each struct before use, and serialize access externally if a single instance is shared across threads. Never mutate struct fields directly or pass the address of an internal field (such as `&watch.started_ns` or `&bucket.updated_ns`) as an output pointer.

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

- **Observing and restarting intervals**:
  - `hx_os_stopwatch_elapsed_ns()` reads the elapsed nanoseconds without resetting the start timestamp.
  - `hx_os_stopwatch_restart()` returns the completed interval duration and starts the next interval from the exact same clock sample.
  - `hx_os_stopwatch_start()` starts or restarts a stopwatch at any time (a zero-initialized `hx_os_stopwatch` must be started before calling `elapsed_ns()` or `restart()`).
- **Clock semantics**: Elapsed time is reported in nanoseconds (`ns`), though actual precision depends on the platform's hardware counter (back-to-back calls may return identical readings). A stopwatch reading is an interval timestamp, **not** an `hx_os_deadline`—never pass it to `hx_os_sleep_until()` or mix it with token-bucket deadlines.

## 3. Backoff with a finite retry count

Initialize a backoff state with `hx_os_backoff_init(&backoff, &options, NULL)`. Passing `NULL` options (or starting from `HX_OS_BACKOFF_OPTIONS_INIT`) selects the defaults: `100 ms` initial delay, `30,000 ms` nominal maximum delay, `8` retries, `20%` jitter, and seed `1` (`initial_ms` must be nonzero, `max_ms >= initial_ms`, and `jitter_percent` in `0..100`; `max_retries = 0` is valid and returns `HX_OS_EOF` on the first call):

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

- **Exponential doubling and exhaustion**: Each successful `hx_os_backoff_next()` call consumes one retry allowance. Nominal delays double at each step and saturate at `max_ms` without integer overflow. With jitter set to `0`, the default configuration produces `100`, `200`, `400`, `800`, `1600`, `3200`, `6400`, and `12800 ms`, followed by `HX_OS_EOF`.
- **Jitter formula and client desynchronization**: For nominal delay $n$ and jitter percentage $p$, let $d = \lfloor n \times p / 100 \rfloor$; the returned delay lies in $[n - d, \min(\text{max\_ms}, n + d)]$. Jitter uses a small deterministic PRNG (seed `0` maps to `1`): **instances initialized with the same seed produce the exact same sequence**. Assign distinct seeds to independent clients when you want to prevent synchronized retry storms.
- **Resetting**: `hx_os_backoff_reset()` restores the initial delay, full retry count, and initial PRNG sequence.

## 4. Token bucket without hidden waiting

Initialize a bucket with `hx_os_token_bucket_init(&bucket, capacity, refill_per_second, NULL)`. The bucket starts full. `hx_os_token_bucket_try_take()` either consumes the entire requested token count (`HX_OS_OK`) or consumes nothing (`HX_OS_WOULD_BLOCK`); requesting an amount larger than `capacity` returns `HX_OS_LIMIT` immediately:

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

- **Burst capacity and refill rate**: A bucket with capacity `20` and rate `5` permits an initial burst of `20` tokens and replenishes `5` tokens per second thereafter (never accumulating above `capacity`). Refill arithmetic uses exact integer math and carries sub-token time remainders forward without floating-point drift.
- **Suggested retry deadline (`retry_at`)**:
  - On `HX_OS_OK`, `retry_at` is set to `HX_OS_DEADLINE_NOW`.
  - On `HX_OS_WOULD_BLOCK`, the bucket updates its refill state and writes the earliest monotonic deadline at which the current deficit will be replenished into `retry_at` (saturating at `UINT64_MAX - 1` and never returning `HX_OS_DEADLINE_INFINITE`).
  - Note that `HX_OS_WOULD_BLOCK` does **not** reserve future tokens; if another caller takes tokens in the meantime, a subsequent attempt at `retry_at` will recompute the new deficit.

## 5. C++11 and a runnable example

Include `<hx/Control.hpp>` for the zero-allocation, exception-free C++11 wrappers:
- `hx::Stopwatch::start()` returns `Result<Stopwatch>`; `elapsedNs()` and `restart()` return `Result<uint64_t>`.
- `hx::Backoff::create(options = nullptr)` returns `Result<Backoff>`; `next()` returns the delay in milliseconds or `HX_OS_EOF`, and `reset()` restarts the sequence.
- `hx::TokenBucket::create(capacity, rate)` returns `Result<TokenBucket>`; `tryTake(amount = 1)` returns `Result<hx_os_deadline>`. When rate-limited (`status() == HX_OS_WOULD_BLOCK`), `hasValue()` is `true` and `value()` holds the suggested `retry_at` deadline.

Build and run `hx_os_control` with examples enabled to see the complete [C example](../examples/control.c) in action.
