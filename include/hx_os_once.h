#ifndef HX_C_PLATFORM_ONCE_H
#define HX_C_PLATFORM_ONCE_H

#include "hx_os_coord.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_once hx_os_once;
typedef hx_os_status(HX_OS_CALL *hx_os_once_fn)(void *user, hx_os_error *error);

/**
 * \brief One successful synchronous initialization, independently owned.
 *
 * \details Callbacks run without the internal lock, must rollback their own
 * partial state on failure, and cannot throw/longjmp. Failure permits a later
 * caller to retry. All callers use semantically equivalent initializers and
 * borrowed state. Success publishes the initialized data to callers returning
 * OK. Self-reentry returns DEADLOCK. No global initializer or hidden worker.
 */
HX_OS_API int HX_OS_CALL hx_os_once_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_once_create(
	hx_os_context *context, hx_os_once **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_once_run(hx_os_once *once,
	hx_os_once_fn function, void *user, hx_os_error *error);

/**
 * \brief Run initialization once, or wait for another caller to finish it.
 * \param[in] once Shared once object, retained through the call.
 * \param[in] function Initializer used if initialization is still needed.
 * \param[in] user Borrowed state passed to the initializer.
 * \param[in] deadline Monotonic deadline for contention waiting.
 * \param[in] cancel Optional cancellation token for waiting.
 * \param[out] error Optional wait or initializer error details.
 * \returns OK after successful initialization; otherwise the wait/callback
 * failure. A failed initializer may be retried by a later caller.
 * \note Deadline/cancellation cannot interrupt an initializer already running.
 *
 * \details An uninitialized object can run a callback even with deadline=NOW.
 * Completed state wins over cancellation. Initial internal mutex acquisition
 * and native reacquisition are not hard deadlines. Timeout preserves the
 * object; the other initializer may still finish.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_once_run_until(hx_os_once *once,
	hx_os_once_fn function, void *user, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, hx_os_error *error);

/**
 * \brief Exclude new/active callers before destruction.
 *
 * \details Observed active calls return BUSY and retain the owner. No reset of
 * published initialization.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_once_destroy(
	hx_os_once **once, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
