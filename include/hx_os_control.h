#ifndef HX_C_PLATFORM_CONTROL_H
#define HX_C_PLATFORM_CONTROL_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Allocation-free value objects with fixed layouts.
 *
 * \details Fields are private state. Initialize before use. Copies are
 * independent snapshots, not synchronized owners. Externally serialize
 * mutations of one object. Outputs must not overlap state objects. Always built
 * in core.
 */
typedef struct hx_os_stopwatch {
	uint64_t started_ns;
	int started;
} hx_os_stopwatch;

#define HX_OS_STOPWATCH_INIT {0, 0}

/**
 * \brief Uses the high-resolution interval clock, not the deadline clock.
 *
 * \details start initializes/restarts; elapsed observes without mutation;
 * restart reports elapsed and starts the next interval using the same single
 * clock sample. Failure, including a backwards clock reading, preserves state
 * and output. Default-zero objects must be started before elapsed/restart. No
 * sleep, suspend compensation or timer-resolution changes are performed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_stopwatch_start(
	hx_os_stopwatch *watch, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stopwatch_elapsed_ns(
	const hx_os_stopwatch *watch, uint64_t *elapsed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_stopwatch_restart(
	hx_os_stopwatch *watch, uint64_t *elapsed, hx_os_error *error);

typedef struct hx_os_backoff_options {
	uint64_t initial_ms;
	uint64_t max_ms;
	uint64_t seed;
	uint32_t max_retries;
	uint32_t jitter_percent;
} hx_os_backoff_options;

#define HX_OS_BACKOFF_OPTIONS_INIT {100, 30000, 1, 8, 20}

typedef struct hx_os_backoff {
	hx_os_backoff_options options;
	uint64_t nominal_ms;
	uint64_t random_state;
	uint32_t used;
} hx_os_backoff;

/**
 * \brief NULL options selects the initializer above.
 *
 * \details initial_ms>0, max_ms>=initial_ms, jitter_percent<=100; max_retries=0
 * permits no retries. seed=0 maps to 1. This is a noncryptographic
 * deterministic generator: use distinct seeds for independent clients. No
 * random global state, time reads or threads.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_backoff_init(hx_os_backoff *backoff,
	const hx_os_backoff_options *options, hx_os_error *error);

/**
 * \brief Each successful next consumes one retry and returns a delay in
 * milliseconds.
 *
 * \details Nominal delays double, saturating at max_ms. Let
 * d=floor(nominal*p/100): jitter lies in [nominal-d, min(max_ms, nominal+d)].
 * At p=100 it may be zero. The modulo-mapped pseudorandom samples are not
 * promised perfectly uniform. EOF after max_retries preserves output/state.
 * Failure also preserves them. This calculates delays only; it neither sleeps
 * nor retries a business call. reset restores the initial delay, retry count
 * and original seed sequence.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_backoff_next(hx_os_backoff *backoff,
	uint64_t *delay_ms, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_backoff_reset(
	hx_os_backoff *backoff, hx_os_error *error);

typedef struct hx_os_token_bucket {
	uint64_t capacity;
	uint64_t tokens;
	uint64_t updated_ns;
	uint32_t fraction;
	uint32_t rate_per_second;
} hx_os_token_bucket;

/**
 * \brief Initial bucket is full.
 *
 * \details Both arguments must be nonzero. Refill uses integer tokens/second
 * and exact fractional carry under the library monotonic clock. Excess credit
 * is discarded at capacity; this permits bursts up to capacity. Failure leaves
 * the previous object unchanged. This does not use stopwatch timestamps; the
 * two clock domains must never be mixed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_token_bucket_init(
	hx_os_token_bucket *bucket, uint64_t capacity, uint32_t rate_per_second,
	hx_os_error *error);

/**
 * \brief All-or-nothing take.
 *
 * \details OK sets required retry_at=NOW. Amount zero is a no-op. Insufficient
 * tokens return WOULD_BLOCK and a monotonic retry deadline after refilling
 * state; no tokens are consumed/reserved. Other users can take them first.
 * Extremely distant deadlines saturate at UINT64_MAX-1, never INFINITE. Amount
 * above capacity returns LIMIT. Other errors, including clock failures or
 * backwards readings, preserve state and retry_at. No sleep/internal lock.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_token_bucket_try_take(
	hx_os_token_bucket *bucket, uint64_t amount, hx_os_deadline *retry_at,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
