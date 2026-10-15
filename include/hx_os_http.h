#ifndef HX_C_PLATFORM_HTTP_H
#define HX_C_PLATFORM_HTTP_H

#include "hx_bytes.h"
#include "hx_os_coord.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_http hx_os_http;

typedef hx_os_status(HX_OS_CALL *hx_os_http_read_fn)(void *user, void *data,
	size_t capacity, size_t *completed, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_http_write_fn)(
	void *user, const void *data, size_t bytes, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_http_progress_fn)(
	void *user, uint64_t downloaded, uint64_t uploaded, hx_os_error *error);

typedef struct hx_os_http_options {
	uint32_t size, version;
	size_t max_body_bytes, max_header_bytes;
	hx_os_deadline deadline;
	hx_os_cancel_token *cancel;
	const char *ca_file, *proxy_url;
	uint32_t timeout_ms, redirects;
} hx_os_http_options;

#define HX_OS_HTTP_OPTIONS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_http_options), 1, 16 * 1024 * 1024, 65536,        \
			HX_OS_DEADLINE_INFINITE, NULL, NULL, NULL, 5000, 0     \
	}

typedef struct hx_os_http_response {
	hx_bytes body, headers;
	hx_string effective_url;
	unsigned status_code;
} hx_os_http_response;

#define HX_OS_HTTP_RESPONSE_INIT                                               \
	{                                                                      \
		HX_BYTES_INIT, HX_BYTES_INIT, HX_STRING_INIT, 0                \
	}

/**
 * \brief Streaming callbacks execute synchronously on the requesting thread.
 *
 * \details read supplies exactly upload_bytes over successive calls; premature
 * EOF fails. write accepts a whole borrowed chunk or fails; earlier chunks
 * cannot be rolled back. Neither callback may retain buffers or reenter the
 * client. NULL write collects the response using request.max_body_bytes.
 * Streaming downloads use max_download_bytes, independent of that memory
 * limit. No automatic upload replay/rewind; redirects with read are rejected.
 * Progress may cancel by returning an error. Native DNS/callbacks can block.
 */
typedef struct hx_os_http_stream_options {
	uint32_t size, version;
	hx_os_http_options request;
	hx_os_http_read_fn read;
	hx_os_http_write_fn write;
	hx_os_http_progress_fn progress;
	void *user;
	uint64_t upload_bytes, max_download_bytes;
	uint32_t low_speed_bytes_per_second, low_speed_seconds;
} hx_os_http_stream_options;

#define HX_OS_HTTP_STREAM_OPTIONS_INIT                                         \
	{                                                                      \
		sizeof(hx_os_http_stream_options), 1, HX_OS_HTTP_OPTIONS_INIT, \
			NULL, NULL, NULL, NULL, 0, UINT64_C(1073741824), 0, 0  \
	}

typedef struct hx_os_http_metrics {
	uint64_t downloaded_bytes, uploaded_bytes;
	double dns_ms, connect_ms, tls_ms, first_byte_ms, total_ms;
} hx_os_http_metrics;

/**
 * \brief Same response/error contract as request; metrics describe this call.
 * \details Timing values are cumulative from transfer start, in milliseconds.
 * A streaming response has an empty body. Transfer failure preserves out,
 * but callback side effects and diagnostic metrics are not rolled back.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_request_stream(hx_os_http *http,
	const char *method, const char *url, const char *const *headers,
	size_t header_count, const hx_os_http_stream_options *options,
	hx_os_http_response *out, hx_os_http_metrics *metrics,
	hx_os_error *error);

/**
 * \brief Download to a same-directory temporary file; publish only HTTP 2xx.
 * \details replace=0 preserves an existing destination. Failure removes the
 * temporary file; if cleanup fails the client retains it, and close retries.
 * No implicit disk sync or resume. The destination parent must be trusted.
 * Further requests return BUSY while file cleanup is pending. options may
 * configure limits/progress, but read/write callbacks must be NULL.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_download_file(hx_os_http *http,
	const char *url, const char *destination, int replace,
	const hx_os_http_stream_options *options, hx_os_http_response *out,
	hx_os_error *error);

/**
 * \brief Upload a regular file without collecting it in memory.
 * \details The file must remain unchanged. read must be NULL. The known file
 * length is used; redirects are rejected. Other streaming options apply.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_upload_file(hx_os_http *http,
	const char *method, const char *url, const char *path,
	const char *const *headers, size_t header_count,
	const hx_os_http_stream_options *options, hx_os_http_response *out,
	hx_os_error *error);

HX_OS_API const char *HX_OS_CALL hx_os_http_pending_file(
	const hx_os_http *http);

/**
 * \brief Bounded synchronous HTTP/HTTPS with certificate + hostname
 * verification.
 *
 * \details create balances native curl initialization; not from DllMain. One
 * request per client at a time (concurrent/reentrant requests return
 * BUSY/DEADLOCK). Different clients can run concurrently. No library-created
 * worker. The default SDK uses blocking system DNS. An opt-in c-ares SDK
 * permits use_cancellable_dns below. Exclude close from all callers; the host
 * coordinates native global cleanup.
 */
HX_OS_API int HX_OS_CALL hx_os_http_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_create(
	hx_os_context *context, hx_os_http **out, hx_os_error *error);
/**
 * \brief Require cancellable DNS for subsequent requests on this client.
 * \details Requires a c-ares-enabled libcurl SDK; otherwise NOT_SUPPORTED.
 * NULL servers uses its platform DNS configuration. Optional numeric server
 * CSV (for example "127.0.0.1:5353") is copied; limit 4096 bytes/16 servers.
 * Configure while idle. Success resets this client's transfer/DNS cache.
 * All request variants share it. Cancel/deadline are checked within 50 ms of
 * DNS/network waits, subject to scheduling; user callbacks/files can still
 * block. This is not a hard deadline. System DNS compatibility is not implied.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_use_cancellable_dns(
	hx_os_http *http, const char *servers, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_options_init(
	hx_os_http_options *options, size_t capacity);
HX_OS_API void HX_OS_CALL hx_os_http_response_init(
	hx_os_http_response *response, hx_os_context *context);
HX_OS_API void HX_OS_CALL hx_os_http_response_destroy(
	hx_os_http_response *response);
/**
 * \brief HTTP 4xx/5xx are successful exchanges; inspect status_code.
 *
 * \details Transfer failure preserves the old response. All inputs are borrowed
 * through the call. Up to 64 headers (8 KiB each, no CR/LF), URL 16 KiB, upload
 * 64 MiB, response body max 64 MiB, headers max 1 MiB. Defaults: body 16 MiB,
 * headers 64 KiB, timeout 5 s, redirects disabled, proxy disabled. Native
 * codec/TLS allocations are additional. Redirects never downgrade HTTPS. No
 * credential-bearing URLs, automatic cookies or arbitrary file schemes.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_request(hx_os_http *http,
	const char *method, const char *url, hx_os_bytes_view body,
	const char *const *headers, size_t header_count,
	const hx_os_http_options *options, hx_os_http_response *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_get(const char *url,
	const hx_os_http_options *options, hx_os_http_response *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_post_json(hx_os_http *http,
	const char *url, hx_string_view json, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_response_text(
	const hx_os_http_response *response, hx_string *out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_close(
	hx_os_http **http, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
