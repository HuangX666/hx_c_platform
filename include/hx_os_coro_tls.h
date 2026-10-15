#ifndef HX_C_PLATFORM_CORO_TLS_H
#define HX_C_PLATFORM_CORO_TLS_H

#include "hx_os_coroutine.h"
#include "hx_os_tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief TLS waits suspend the current coroutine using the existing TLS worker.
 * \details No polling thread or periodic wakeup is added. Start the endpoint
 * with tls_start_* first. Stackless callers repeat the same operation and
 * persistent arguments after WOULD_BLOCK. Send progress is retained internally
 * until a terminal return; completed then reports accepted bytes even on
 * failure. Buffers and the TLS owner must remain alive through the operation.
 * Timeout/cancellation does not undo already accepted sends. The endpoint
 * close waits for registered callers; its own timeout preserves ownership.
 * Each send/receive direction still requires caller serialization per peer.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_tls_wait_ready(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_tls_send_until(hx_os_tls *tls,
	uint64_t peer, const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_tls_receive_until(hx_os_tls *tls,
	uint64_t peer, void *data, size_t capacity, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_tls_flush_until(hx_os_tls *tls,
	uint64_t peer, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
