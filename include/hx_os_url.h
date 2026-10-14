#ifndef HX_C_PLATFORM_URL_H
#define HX_C_PLATFORM_URL_H

#include "hx_bytes.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_url hx_os_url;

enum {
	HX_OS_URL_FULL,
	HX_OS_URL_SCHEME,
	HX_OS_URL_HOST,
	HX_OS_URL_PORT,
	HX_OS_URL_PATH,
	HX_OS_URL_QUERY,
	HX_OS_URL_FRAGMENT
};

typedef hx_os_status(HX_OS_CALL *hx_os_query_fn)(void *user, hx_string_view key,
	hx_string_view value, hx_os_error *error);

/* Percent encoding uses RFC3986 unreserved bytes; decode preserves arbitrary
 * bytes and rejects malformed escapes. plus_as_space is explicitly 0/1.
 * encode: input up to 16 KiB, result up to 48 KiB.
 * decode: encoded input up to 48 KiB, result up to 16 KiB.
 * Outputs are transactional and independently owned.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_percent_encode(
	hx_os_bytes_view input, hx_string *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_percent_decode(hx_string_view input,
	int plus_as_space, hx_bytes *out, hx_os_error *error);

/* Absolute hierarchical URLs, at most 16 KiB UTF-8, parsed by libcurl URL
 * API. No HTTP transfer or DNS. Native IDNA availability applies. Parts
 * retain encoded spelling; query conveniences decode UTF-8, treating '+' as
 * space, and encode spaces as %20. Missing components/keys are NOT_FOUND.
 * Result owns the URL and a balanced native curl initialization lease;
 * host coordinates native global allocator/cleanup changes. Externally
 * serialize operations and destruction.
 */
HX_OS_API int HX_OS_CALL hx_os_url_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_parse(hx_os_context *context,
	const char *text, hx_os_url **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_part(const hx_os_url *url,
	unsigned part, hx_string *out, hx_os_error *error);
/* get returns the first matching duplicate. append preserves duplicates;
 * set replaces all occurrences of that key. Mutations preserve URL on error.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_query_get(const hx_os_url *url,
	hx_string_view key, hx_string *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_query_append(hx_os_url *url,
	hx_string_view key, hx_string_view value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_query_set(hx_os_url *url,
	hx_string_view key, hx_string_view value, hx_os_error *error);
/* Synchronous, at most 1024 pairs; callback views expire after return.
 * Callback must not mutate/destroy this URL. Failure keeps delivered prefix.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_url_query_each(const hx_os_url *url,
	hx_os_query_fn function, void *user, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_url_destroy(hx_os_url **url);

#ifdef __cplusplus
}
#endif
#endif
