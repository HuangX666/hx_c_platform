#ifndef HX_C_PLATFORM_HTTP_H
#define HX_C_PLATFORM_HTTP_H

#include "hx_bytes.h"
#include "hx_os_coord.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_http hx_os_http;

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

/* Bounded synchronous HTTP/HTTPS with certificate + hostname verification.
 * create balances native curl initialization; not from DllMain. One request
 * per client at a time (concurrent/reentrant requests return BUSY/DEADLOCK).
 * Different clients can run concurrently. No library-created worker. The
 * supplied SDK uses blocking DNS, so deadline/cancel cannot interrupt DNS.
 * Exclude close from all callers; host coordinates native global cleanup.
 */
HX_OS_API int HX_OS_CALL hx_os_http_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_create(
	hx_os_context *context, hx_os_http **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_http_options_init(
	hx_os_http_options *options, size_t capacity);
HX_OS_API void HX_OS_CALL hx_os_http_response_init(
	hx_os_http_response *response, hx_os_context *context);
HX_OS_API void HX_OS_CALL hx_os_http_response_destroy(
	hx_os_http_response *response);
/* HTTP 4xx/5xx are successful exchanges; inspect status_code. Transfer failure
 * preserves the old response. All inputs are borrowed through the call.
 * Up to 64 headers (8 KiB each, no CR/LF), URL 16 KiB, upload 64 MiB,
 * response body max 64 MiB, headers max 1 MiB. Defaults: body 16 MiB,
 * headers 64 KiB, timeout 5 s, redirects disabled, proxy disabled. Native
 * codec/TLS allocations are additional. Redirects never downgrade HTTPS.
 * No credential-bearing URLs, automatic cookies or arbitrary file schemes.
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
