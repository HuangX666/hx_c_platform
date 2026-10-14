#ifndef HX_C_PLATFORM_TIMER_H
#define HX_C_PLATFORM_TIMER_H

#include "hx_os_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_timer hx_os_timer;
typedef void(HX_OS_CALL *hx_os_timer_fn)(
	hx_os_timer *timer, const hx_os_error *error, void *user);
typedef void(HX_OS_CALL *hx_os_timer_cleanup_fn)(void *user);

/**
 * \brief Periodic monotonic timer, first tick one interval after creation.
 *
 * \details A positive interval is nanoseconds, not a resolution guarantee.
 * Missed periods coalesce; scheduling stays on the original time axis, without
 * a catch-up burst. One request slot remains reserved while active. No per-tick
 * allocation or background thread: the creator drives the supplied loop.
 * Callback runs outside locks on the poller, with borrowed error storage: OK is
 * a tick; a terminal clock/range error is reported once and stops it. Stop
 * inside a callback is allowed. Cancellation produces no extra tick.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_timer_create(hx_os_loop *loop,
	uint64_t interval_ns, hx_os_timer_fn function, void *user,
	hx_os_timer **out, hx_os_error *error);

/**
 * \brief Same factory with optional cleanup after the final callback returns.
 *
 * \details Acceptance transfers cleanup responsibility; rejection calls neither
 * function nor cleanup. Cleanup executes once on the poller, outside locks,
 * before retirement is observable. It must not throw or wait for itself.
 * Without cleanup, user remains borrowed through retirement.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_timer_create_with_cleanup(
	hx_os_loop *loop, uint64_t interval_ns, hx_os_timer_fn function,
	hx_os_timer_cleanup_fn cleanup, void *user, hx_os_timer **out,
	hx_os_error *error);

/**
 * \brief Thread-safe request; caller excludes concurrent object destruction.
 */
HX_OS_API void HX_OS_CALL hx_os_timer_stop(hx_os_timer *timer);

/**
 * \brief Does not pump the loop.
 *
 * \details Includes callback/cleanup return. An unfinished owner-thread wait
 * returns DEADLOCK; other threads may wait with a deadline. Normal cancellation
 * is OK; native terminal errors retain their details.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_timer_wait_until(
	hx_os_timer *timer, hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief Owner-only stop/pump/close.
 *
 * \details Timeout retains *timer for retry. Callback reentry cannot wait for
 * itself. Once retired, consumes the object even when reporting its terminal
 * error. Close every timer before its loop. Exclude concurrent close and all
 * operations that could race destruction.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_timer_close_until(
	hx_os_timer **timer, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
