#ifndef HX_C_PLATFORM_WAIT_H
#define HX_C_PLATFORM_WAIT_H

#include "hx_os_base.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * \brief Explicit hybrid wait using the library monotonic deadline.
 *
 * \details Sleeps until the final spin window, then uses cpu_relax and clock
 * reads. Default 200 us; ex budget is 0..10 ms (0 delegates to ordinary sleep).
 * NOW returns OK, INFINITE is INVALID. No global timer-resolution/scheduler
 * changes, hidden thread, allocation or hard-real-time guarantee. Scheduler
 * delays may exceed the deadline; a late wake can skip the spin window
 * altogether. Windows bounds spin time independently with QPC; if its coarse
 * deadline clock has not advanced within the budget, waiting resumes ordinary
 * sleep.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_until_precise(
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sleep_until_precise_ex(
	hx_os_deadline deadline, uint64_t spin_ns, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_repeat_fn)(
	void *user, hx_os_error *error);

/**
 * \brief Synchronous periodic work on the calling thread; no loop, worker or
 * heap.
 *
 * \details Positive interval in milliseconds and positive tick limit. First
 * call is after one interval; missed periods coalesce. OK continues; EOF stops
 * with overall OK; other callback errors propagate. User is borrowed until
 * return. The callback receives a non-NULL, cleared error record. until
 * additionally bounds waiting and reports callbacks started. Callback execution
 * is not interruptible and may exceed the deadline. No per-call spin: use
 * precise waiting explicitly when the CPU tradeoff is intended.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_repeat(uint64_t interval_ms,
	size_t ticks, hx_os_repeat_fn function, void *user, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_repeat_until(uint64_t interval_ms,
	size_t ticks, hx_os_repeat_fn function, void *user,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
