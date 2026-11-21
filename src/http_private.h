#ifndef HX_C_PLATFORM_HTTP_PRIVATE_H
#define HX_C_PLATFORM_HTTP_PRIVATE_H

#include "hx_os_http.h"

hx_os_status hx_os_i_http_request(hx_os_http *http,
	const char *method, const char *url, hx_os_bytes_view body,
	const char *const *headers, size_t header_count,
	const hx_os_http_options *options,
	const hx_os_http_stream_options *stream, hx_os_http_metrics *metrics,
	int owned, hx_os_http_response *out, hx_os_error *error);

#endif
