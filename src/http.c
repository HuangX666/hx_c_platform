#include "internal.h"
#include "hx_os_http.h"

#if HX_OS_HAVE_HTTP
#include <curl/curl.h>

struct hx_os_http {
	CURL *native;
	hx_os_atomic owner;
};

typedef struct hx_os_http_transfer {
	hx_os_http_response result;
	const hx_os_http_options *options;
	hx_os_deadline deadline;
	hx_os_status status;
	hx_os_error *error;
} hx_os_http_transfer;

static size_t hx_os_http_collect(hx_os_http_transfer *transfer, hx_bytes *out,
	size_t maximum, const void *data, size_t size, size_t count)
{
	size_t bytes;

	if (transfer->status != HX_OS_OK)
		return 0;
	if (size && count > SIZE_MAX / size) {
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_LIMIT, 0, 0);
		return 0;
	}
	bytes = size * count;
	if (bytes > maximum - out->size)
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_LIMIT, 0, 0);
	else
		transfer->status = hx_bytes_append(
			out, (hx_os_bytes_view){data, bytes}, transfer->error);
	return transfer->status == HX_OS_OK ? bytes : 0;
}

static size_t hx_os_http_body(char *data, size_t size, size_t count, void *user)
{
	hx_os_http_transfer *transfer = user;

	return hx_os_http_collect(transfer, &transfer->result.body,
		transfer->options->max_body_bytes, data, size, count);
}

static size_t hx_os_http_headers(
	char *data, size_t size, size_t count, void *user)
{
	hx_os_http_transfer *transfer = user;

	return hx_os_http_collect(transfer, &transfer->result.headers,
		transfer->options->max_header_bytes, data, size, count);
}

static int hx_os_http_progress(void *user, curl_off_t download,
	curl_off_t downloaded, curl_off_t upload, curl_off_t uploaded)
{
	hx_os_http_transfer *transfer = user;

	(void)download;
	(void)downloaded;
	(void)upload;
	(void)uploaded;
	if (hx_os_cancel_requested(transfer->options->cancel))
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_CANCELLED, 0, 0);
	else if (hx_os_deadline_expired(transfer->deadline))
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_TIMEOUT, 0, 0);
	return transfer->status != HX_OS_OK;
}

static hx_os_status hx_os_http_fail(CURLcode code, hx_os_error *error)
{
	hx_os_status status = code == CURLE_OUT_OF_MEMORY ? HX_OS_NO_MEMORY
		: code == CURLE_OPERATION_TIMEDOUT	  ? HX_OS_TIMEOUT
		: code == CURLE_PEER_FAILED_VERIFICATION ||
			code == CURLE_SSL_CACERT_BADFILE
		? HX_OS_AUTH_FAILED
		: code == CURLE_UNSUPPORTED_PROTOCOL ? HX_OS_NOT_SUPPORTED
		: code == CURLE_URL_MALFORMAT	     ? HX_OS_SYNTAX
						     : HX_OS_IO_ERROR;

	return hx_os_i_error(error, status, HX_OS_ERROR_CURL, code);
}
#endif

int hx_os_http_available(void)
{
	return HX_OS_HAVE_HTTP;
}

hx_os_status hx_os_http_create(
	hx_os_context *context, hx_os_http **out, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	hx_os_http *http;
	CURLcode code;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	code = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (code != CURLE_OK)
		return hx_os_http_fail(code, error);
	if (!(curl_version_info(CURLVERSION_NOW)->features &
		    CURL_VERSION_THREADSAFE)) {
		curl_global_cleanup();
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	http = hx_os_i_alloc(context, sizeof(*http), 8);
	if (!http) {
		curl_global_cleanup();
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	http->native = curl_easy_init();
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELAXED);
	if (!http->native) {
		hx_os_memory_free(http);
		curl_global_cleanup();
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	*out = http;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_http_options_init(
	hx_os_http_options *options, size_t capacity)
{
	const hx_os_http_options defaults = HX_OS_HTTP_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;
	return HX_OS_OK;
}

void hx_os_http_response_init(
	hx_os_http_response *response, hx_os_context *context)
{
	if (!response)
		return;
	hx_bytes_init(&response->body, context);
	hx_bytes_init(&response->headers, context);
	hx_string_init(&response->effective_url, context);
	response->status_code = 0;
}

void hx_os_http_response_destroy(hx_os_http_response *response)
{
	if (!response)
		return;
	hx_bytes_destroy(&response->body);
	hx_bytes_destroy(&response->headers);
	hx_string_destroy(&response->effective_url);
	response->status_code = 0;
}

hx_os_status hx_os_http_request(hx_os_http *http, const char *method,
	const char *url, hx_os_bytes_view body, const char *const *headers,
	size_t header_count, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	const hx_os_http_options defaults = HX_OS_HTTP_OPTIONS_INIT;
	hx_os_http_transfer transfer;
	struct curl_slist *list = NULL;
	CURLcode code = CURLE_OK;
	hx_os_status status;
	int64_t expected = 0, thread = (int64_t)hx_os_thread_id();
	long response_code = 0, timeout;
	int secure = 1;
	char *effective = NULL;
	size_t i, offset;

	if (!options)
		options = &defaults;
	if (!thread || !http || !method || !*method || !url || !out ||
		(!body.data && body.size) || (!headers && header_count) ||
		options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (header_count > 64 || strlen(url) > 16384 ||
		body.size > HX_BYTES_MAX_SIZE || !options->max_body_bytes ||
		options->max_body_bytes > HX_BYTES_MAX_SIZE ||
		!options->max_header_bytes ||
		options->max_header_bytes > 1024 * 1024 ||
		options->timeout_ms > 86400000 || options->redirects > 16)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (hx_os_text_validate_utf8(url, strlen(url), &offset) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	for (i = 0; method[i]; ++i)
		if (i >= 32 || method[i] < 'A' || method[i] > 'Z')
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (i = 0; i < header_count; ++i)
		if (!headers[i] || strlen(headers[i]) > 8192 ||
			strchr(headers[i], '\r') || strchr(headers[i], '\n') ||
			!strchr(headers[i], ':'))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_atomic_compare_exchange(&http->owner, &expected, thread,
		    HX_OS_ACQ_REL, HX_OS_ACQUIRE))
		return hx_os_i_error(error,
			expected == thread ? HX_OS_DEADLOCK : HX_OS_BUSY, 0, 0);
	memset(&transfer, 0, sizeof(transfer));
	hx_bytes_init(&transfer.result.body, out->body.context);
	hx_bytes_init(&transfer.result.headers, out->headers.context);
	hx_string_init(
		&transfer.result.effective_url, out->effective_url.context);
	transfer.options = options;
	transfer.error = error;
	transfer.deadline = hx_os_deadline_after_ms(options->timeout_ms);
	if (options->deadline < transfer.deadline)
		transfer.deadline = options->deadline;
	if (hx_os_http_progress(&transfer, 0, 0, 0, 0)) {
		status = transfer.status;
		goto finish;
	}
	for (i = 0; i < header_count; ++i) {
		struct curl_slist *next = curl_slist_append(list, headers[i]);

		if (!next) {
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
			goto finish;
		}
		list = next;
	}
	curl_easy_reset(http->native);
	timeout = (long)hx_os_i_wait_ms(transfer.deadline);
	if (timeout < 1)
		timeout = 1;
#define HX_OS_HTTP_SET(name, value)                                            \
	do {                                                                   \
		if (code == CURLE_OK)                                          \
			code = curl_easy_setopt(http->native, name, value);    \
	} while (0)
	for (i = 0; i < 6; ++i)
		if (!url[i] ||
			((unsigned char)url[i] | 32u) !=
				(unsigned char)"https:"[i]) {
			secure = 0;
			break;
		}
	HX_OS_HTTP_SET(CURLOPT_URL, url);
	HX_OS_HTTP_SET(CURLOPT_PROTOCOLS_STR, "http,https");
	HX_OS_HTTP_SET(
		CURLOPT_REDIR_PROTOCOLS_STR, secure ? "https" : "http,https");
	HX_OS_HTTP_SET(CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
	HX_OS_HTTP_SET(
		CURLOPT_PROXY, options->proxy_url ? options->proxy_url : "");
	HX_OS_HTTP_SET(CURLOPT_NOSIGNAL, 1L);
	HX_OS_HTTP_SET(CURLOPT_SSL_VERIFYPEER, 1L);
	HX_OS_HTTP_SET(CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef _WIN32
	if (!options->ca_file)
		HX_OS_HTTP_SET(CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
	if (options->ca_file)
		HX_OS_HTTP_SET(CURLOPT_CAINFO, options->ca_file);
	HX_OS_HTTP_SET(CURLOPT_FOLLOWLOCATION, options->redirects ? 1L : 0L);
	HX_OS_HTTP_SET(CURLOPT_MAXREDIRS, (long)options->redirects);
	HX_OS_HTTP_SET(CURLOPT_TIMEOUT_MS, timeout);
	HX_OS_HTTP_SET(
		CURLOPT_CONNECTTIMEOUT_MS, timeout < 2000 ? timeout : 2000L);
	HX_OS_HTTP_SET(CURLOPT_HTTPHEADER, list);
	if (body.size || !strcmp(method, "POST")) {
		HX_OS_HTTP_SET(CURLOPT_POSTFIELDS, body.data ? body.data : "");
		HX_OS_HTTP_SET(
			CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body.size);
	}
	HX_OS_HTTP_SET(CURLOPT_CUSTOMREQUEST, method);
	HX_OS_HTTP_SET(CURLOPT_NOBODY, !strcmp(method, "HEAD") ? 1L : 0L);
	HX_OS_HTTP_SET(CURLOPT_WRITEFUNCTION, hx_os_http_body);
	HX_OS_HTTP_SET(CURLOPT_WRITEDATA, &transfer);
	HX_OS_HTTP_SET(CURLOPT_HEADERFUNCTION, hx_os_http_headers);
	HX_OS_HTTP_SET(CURLOPT_HEADERDATA, &transfer);
	HX_OS_HTTP_SET(CURLOPT_NOPROGRESS, 0L);
	HX_OS_HTTP_SET(CURLOPT_XFERINFOFUNCTION, hx_os_http_progress);
	HX_OS_HTTP_SET(CURLOPT_XFERINFODATA, &transfer);
#undef HX_OS_HTTP_SET
	if (code == CURLE_OK)
		code = curl_easy_perform(http->native);
	if (transfer.status != HX_OS_OK)
		status = transfer.status;
	else if (code != CURLE_OK)
		status = hx_os_http_fail(code, error);
	else {
		code = curl_easy_getinfo(
			http->native, CURLINFO_RESPONSE_CODE, &response_code);
		if (code == CURLE_OK)
			code = curl_easy_getinfo(http->native,
				CURLINFO_EFFECTIVE_URL, &effective);
		status = code == CURLE_OK
			? hx_string_assign_cstr(&transfer.result.effective_url,
				  effective ? effective : url, error)
			: hx_os_http_fail(code, error);
		if (status == HX_OS_OK) {
			curl_easy_reset(http->native);
			hx_os_http_response_destroy(out);
			*out = transfer.result;
			memset(&transfer.result, 0, sizeof(transfer.result));
			out->status_code = (unsigned)response_code;
		}
	}
finish:
	// Reset before releasing borrowed request buffers and callback state.
	curl_easy_reset(http->native);
	curl_slist_free_all(list);
	hx_os_http_response_destroy(&transfer.result);
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)http;
	(void)method;
	(void)url;
	(void)body;
	(void)headers;
	(void)header_count;
	(void)options;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_http_get(const char *url, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error)
{
	hx_os_http *http = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_http_create(out->body.context, &http, error);
	if (status == HX_OS_OK)
		status = hx_os_http_request(http, "GET", url,
			(hx_os_bytes_view){NULL, 0}, NULL, 0, options, out,
			error);
	(void)hx_os_http_close(&http, NULL);
	return status;
}

hx_os_status hx_os_http_post_json(hx_os_http *http, const char *url,
	hx_string_view json, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error)
{
	const char *headers[] = {"Content-Type: application/json"};
	size_t offset;

	if ((!json.data && json.size) ||
		hx_os_text_validate_utf8(json.data, json.size, &offset) !=
			HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	return hx_os_http_request(http, "POST", url,
		(hx_os_bytes_view){json.data, json.size}, headers, 1, options,
		out, error);
}

hx_os_status hx_os_http_response_text(
	const hx_os_http_response *response, hx_string *out, hx_os_error *error)
{
	if (!response || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_string_assign(out,
		(hx_string_view){
			(const char *)response->body.data, response->body.size},
		error);
}

hx_os_status hx_os_http_close(hx_os_http **http, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	hx_os_http *value;

	if (!http)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *http;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_os_atomic_load(&value->owner, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	*http = NULL;
	curl_easy_cleanup(value->native);
	hx_os_memory_free(value);
	curl_global_cleanup();
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)http;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
