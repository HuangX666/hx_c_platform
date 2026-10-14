#ifndef HX_C_PLATFORM_LOOP_FD_H
#define HX_C_PLATFORM_LOOP_FD_H

#include "hx_os_loop.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_loop_watch hx_os_loop_watch;
typedef void(HX_OS_CALL *hx_os_loop_fd_fn)(
	hx_os_loop_watch *watch, int events, void *user);
typedef void(HX_OS_CALL *hx_os_loop_fd_cleanup_fn)(void *user);

/**
 * \brief POSIX readiness extension (Windows: NOT_SUPPORTED).
 *
 * \details events is READ/WRITE from HX_OS_WAIT_*. Owns a CLOEXEC duplicate,
 * preserving the original fd's lifetime and shared open-file flags/offset. No
 * implicit O_NONBLOCK change. Use nonblocking I/O in callbacks; readiness can
 * race with other consumers. One persistent loop request slot; callback only on
 * the poller, outside locks. Hangup/errors also signal readiness: I/O
 * determines EOF/error. Acceptance transfers optional cleanup; rejection calls
 * no user callback. Callback/cleanup data is borrowed through retirement,
 * cleanup once.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_attach_fd(hx_os_loop *loop,
	int descriptor, int events, hx_os_loop_fd_fn function, void *user,
	hx_os_loop_watch **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_attach_fd_with_cleanup(
	hx_os_loop *loop, int descriptor, int events, hx_os_loop_fd_fn function,
	hx_os_loop_fd_cleanup_fn cleanup, void *user, hx_os_loop_watch **out,
	hx_os_error *error);

/**
 * \brief Owner-only borrowed duplicate, never close it.
 *
 * \details -1 after retirement/invalid.
 */
HX_OS_API int HX_OS_CALL hx_os_loop_watch_descriptor(
	const hx_os_loop_watch *watch);

/**
 * \brief Thread-safe stop request; exclude concurrent destruction.
 */
HX_OS_API void HX_OS_CALL hx_os_loop_watch_stop(hx_os_loop_watch *watch);

/**
 * \brief Wait includes callback/cleanup return, never pumps; owner wait can
 * DEADLOCK.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_watch_wait_until(
	hx_os_loop_watch *watch, hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief Owner-only stop/pump/unregister/close.
 *
 * \details Timeout or unregister failure retains the object for retry. After
 * unregistration, native close errors consume it. Self-close is DEADLOCK. Close
 * every watch before destroying its loop.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_watch_close_until(
	hx_os_loop_watch **watch, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_detach_fd(
	hx_os_loop_watch **watch, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
