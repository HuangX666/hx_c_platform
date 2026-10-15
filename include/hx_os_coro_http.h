#ifndef HX_C_PLATFORM_CORO_HTTP_H
#define HX_C_PLATFORM_CORO_HTTP_H

#include "hx_os_coroutine.h"
#include "hx_os_http.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_coro_http hx_os_coro_http;

/**
 * \brief Explicit bounded HTTP I/O pool, separate from coroutine workers.
 * \details Zero values select 2 threads, 16 requests and 64 MiB of reserved
 * input/response capacity. Limits: 64 threads, 1024 requests. Native curl/TLS
 * allocations and container growth peaks are additional. This is an admission
 * budget, not an RSS cap. Creation starts the pool, not individual calls.
 * The service retains runtime until close. No process-global service is used.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_http_create(
	hx_os_coro_runtime *runtime, size_t threads, size_t max_requests,
	size_t max_pending_bytes, hx_os_coro_http **out, hx_os_error *error);

/**
 * \brief Copy a request, then suspend until its bounded I/O worker completes.
 * \details Stackless callers repeat this call with persistent out after
 * WOULD_BLOCK. Inputs are copied before suspension; out changes only on
 * success. Responses own service-context allocations. options.cancel must be
 * NULL: use coroutine cancellation, or bind an external token to the coroutine.
 * Timeout/cancellation can return before blocked native DNS retires; the job
 * owns all buffers until then and service close reports TIMEOUT for live work.
 * No automatic retries. 4xx/5xx remain completed HTTP exchanges.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_http_request(hx_os_coro_http *http,
	const char *method, const char *url, hx_os_bytes_view body,
	const char *const *headers, size_t header_count,
	const hx_os_http_options *options, hx_os_http_response *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_http_get(hx_os_coro_http *http,
	const char *url, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error);

/**
 * \brief Native owner thread only; reject new requests and cancel queued work.
 * \details Cancel all jobs and join existing workers within one deadline.
 * TIMEOUT retains *http, including native work, for retry. Close before final
 * runtime destruction; do not call from a coroutine on this runtime.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_coro_http_close_until(
	hx_os_coro_http **http, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
