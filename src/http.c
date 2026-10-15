#include "internal.h"
#include "hx_os_http.h"
#include "hx_os_temp.h"
#include "file_utils_private.h"
#include "resolver_private.h"

#if HX_OS_HAVE_HTTP
#include <curl/curl.h>

struct hx_os_http {
	CURL *native;
	CURLM *multi;
	hx_string dns_servers;
	hx_os_atomic owner;
	hx_os_temp *pending_file;
};

typedef struct hx_os_http_transfer {
	hx_os_http_response result;
	const hx_os_http_options *options;
	hx_os_deadline deadline;
	hx_os_status status;
	hx_os_error *error;
	const hx_os_http_stream_options *stream;
	uint64_t downloaded, uploaded;
	uint64_t progress_downloaded, progress_uploaded;
} hx_os_http_transfer;

static hx_os_status hx_os_http_enter(hx_os_http *http, hx_os_error *error)
{
	int64_t expected = 0, thread = (int64_t)hx_os_thread_id();

	if (!http || !thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_atomic_compare_exchange(&http->owner, &expected, thread,
		    HX_OS_ACQ_REL, HX_OS_ACQUIRE))
		return hx_os_i_error(error,
			expected == thread ? HX_OS_DEADLOCK : HX_OS_BUSY, 0, 0);
	if (http->pending_file) {
		hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}
	return HX_OS_OK;
}

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
	size_t bytes;

	if (transfer->stream && transfer->stream->write) {
		if ((size && count > SIZE_MAX / size) ||
			(uint64_t)(size * count) >
				transfer->stream->max_download_bytes -
					transfer->downloaded) {
			transfer->status = hx_os_i_error(
				transfer->error, HX_OS_LIMIT, 0, 0);
			return 0;
		}
		bytes = size * count;
		if (transfer->status == HX_OS_OK)
			transfer->status =
				transfer->stream->write(transfer->stream->user,
					data, bytes, transfer->error);
		if (transfer->status != HX_OS_OK)
			return 0;
		transfer->downloaded += bytes;
		return bytes;
	}

	return hx_os_http_collect(transfer, &transfer->result.body,
		transfer->options->max_body_bytes, data, size, count);
}

static size_t hx_os_http_read(char *data, size_t size, size_t count, void *user)
{
	hx_os_http_transfer *transfer = user;
	uint64_t remaining =
		transfer->stream->upload_bytes - transfer->uploaded;
	size_t capacity, completed = 0;

	if (transfer->status != HX_OS_OK)
		return CURL_READFUNC_ABORT;
	if (size && count > SIZE_MAX / size) {
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_LIMIT, 0, 0);
		return CURL_READFUNC_ABORT;
	}
	capacity = size * count;
	if (remaining < capacity)
		capacity = (size_t)remaining;
	if (!capacity)
		return 0;
	transfer->status = transfer->stream->read(transfer->stream->user, data,
		capacity, &completed, transfer->error);
	if (completed > capacity)
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_INVALID, 0, 0);
	else if (transfer->status == HX_OS_EOF ||
		(transfer->status == HX_OS_OK && !completed))
		transfer->status = hx_os_i_error(
			transfer->error, HX_OS_TRUNCATED_INPUT, 0, 0);
	if (transfer->status != HX_OS_OK)
		return CURL_READFUNC_ABORT;
	transfer->uploaded += completed;
	return completed;
}

static size_t hx_os_http_headers(
	char *data, size_t size, size_t count, void *user)
{
	hx_os_http_transfer *transfer = user;

	return hx_os_http_collect(transfer, &transfer->result.headers,
		transfer->options->max_header_bytes, data, size, count);
}

static int hx_os_http_check(hx_os_http_transfer *transfer)
{
	if (hx_os_cancel_requested(transfer->options->cancel))
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_CANCELLED, 0, 0);
	else if (hx_os_deadline_expired(transfer->deadline))
		transfer->status =
			hx_os_i_error(transfer->error, HX_OS_TIMEOUT, 0, 0);
	else if (transfer->status == HX_OS_OK && transfer->stream &&
		transfer->stream->progress)
		transfer->status = transfer->stream->progress(
			transfer->stream->user, transfer->progress_downloaded,
			transfer->progress_uploaded, transfer->error);
	return transfer->status != HX_OS_OK;
}

static int hx_os_http_progress(void *user, curl_off_t download,
	curl_off_t downloaded, curl_off_t upload, curl_off_t uploaded)
{
	hx_os_http_transfer *transfer = user;

	(void)download;
	(void)upload;
	transfer->progress_downloaded = (uint64_t)downloaded;
	transfer->progress_uploaded = (uint64_t)uploaded;
	return hx_os_http_check(transfer);
}

// One transfer on the calling thread; the multi handle retains its cache.
static CURLcode hx_os_http_perform(
	hx_os_http *http, hx_os_http_transfer *transfer)
{
	CURLMcode multi_code;
	CURLcode code = CURLE_FAILED_INIT;
	CURLMsg *message;
	int running = 1, queued;

	if (!http->multi)
		return curl_easy_perform(http->native);
	multi_code = curl_multi_add_handle(http->multi, http->native);
	if (multi_code != CURLM_OK)
		return multi_code == CURLM_OUT_OF_MEMORY ? CURLE_OUT_OF_MEMORY
							 : CURLE_FAILED_INIT;
	while (multi_code == CURLM_OK && running) {
		uint32_t wait;

		if (hx_os_http_check(transfer)) {
			code = CURLE_ABORTED_BY_CALLBACK;
			break;
		}
		multi_code = curl_multi_perform(http->multi, &running);
		if (multi_code != CURLM_OK || !running)
			break;
		wait = hx_os_i_wait_ms(transfer->deadline);
		multi_code = curl_multi_poll(http->multi, NULL, 0,
			(int)(wait < 50 ? wait : 50), NULL);
	}
	while ((message = curl_multi_info_read(http->multi, &queued)) != NULL)
		if (message->msg == CURLMSG_DONE)
			code = message->data.result;
	if (multi_code != CURLM_OK)
		code = multi_code == CURLM_OUT_OF_MEMORY ? CURLE_OUT_OF_MEMORY
							 : CURLE_FAILED_INIT;
	// Remove outside callbacks, before borrowed buffers may be released.
	(void)curl_multi_remove_handle(http->multi, http->native);
	return code;
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
	http->pending_file = NULL;
	http->multi = NULL;
	hx_string_init(&http->dns_servers, context);
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELAXED);
	if (!http->native) {
		hx_string_destroy(&http->dns_servers);
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

hx_os_status hx_os_http_use_cancellable_dns(
	hx_os_http *http, const char *servers, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	hx_os_resolver_options options = HX_OS_RESOLVER_OPTIONS_INIT;
	hx_os_status status;
	CURLM *multi = NULL;
	size_t bytes;

	status = hx_os_http_enter(http, error);
	if (status != HX_OS_OK)
		return status;
	if (!curl_version_info(CURLVERSION_NOW)->ares) {
		status = hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		goto finish;
	}
	options.servers = servers;
	status = hx_os_i_resolver_options_check(&options, &bytes, error);
	if (status != HX_OS_OK)
		goto finish;
	multi = curl_multi_init();
	if (!multi) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto finish;
	}
	status = hx_string_assign_cstr(
		&http->dns_servers, servers ? servers : "", error);
	if (status == HX_OS_OK) {
		if (http->multi)
			(void)curl_multi_cleanup(http->multi);
		http->multi = multi;
		multi = NULL;
	}
finish:
	if (multi)
		(void)curl_multi_cleanup(multi);
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
	return status;
#else
	(void)http;
	(void)servers;
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

static hx_os_status hx_os_http_request_impl(hx_os_http *http,
	const char *method, const char *url, hx_os_bytes_view body,
	const char *const *headers, size_t header_count,
	const hx_os_http_options *options,
	const hx_os_http_stream_options *stream, hx_os_http_metrics *metrics,
	int owned, hx_os_http_response *out, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	const hx_os_http_options defaults = HX_OS_HTTP_OPTIONS_INIT;
	hx_os_http_transfer transfer;
	struct curl_slist *list = NULL;
	CURLcode code = CURLE_OK;
	hx_os_status status;
	long response_code = 0, timeout;
	int secure = 1;
	char *effective = NULL;
	size_t i, offset;

	if (!options)
		options = &defaults;
	if (!http || !method || !*method || !url || !out ||
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
	if (stream &&
		(stream->size < sizeof(*stream) || stream->version != 1 ||
			stream->upload_bytes > INT64_MAX ||
			(!stream->read && stream->upload_bytes) ||
			(stream->read && (options->redirects || body.size)) ||
			(stream->write && !stream->max_download_bytes) ||
			stream->low_speed_bytes_per_second > INT_MAX ||
			stream->low_speed_seconds > 86400 ||
			(!stream->low_speed_seconds !=
				!stream->low_speed_bytes_per_second)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!owned) {
		status = hx_os_http_enter(http, error);
		if (status != HX_OS_OK)
			return status;
	}
	if (metrics)
		memset(metrics, 0, sizeof(*metrics));
	hx_os_error_clear(error);
	memset(&transfer, 0, sizeof(transfer));
	hx_bytes_init(&transfer.result.body, out->body.context);
	hx_bytes_init(&transfer.result.headers, out->headers.context);
	hx_string_init(
		&transfer.result.effective_url, out->effective_url.context);
	transfer.options = options;
	transfer.stream = stream;
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
	if (http->multi && http->dns_servers.size)
		HX_OS_HTTP_SET(CURLOPT_DNS_SERVERS,
			hx_string_c_str(&http->dns_servers));
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
	if (stream && stream->read) {
		HX_OS_HTTP_SET(CURLOPT_UPLOAD, 1L);
		HX_OS_HTTP_SET(CURLOPT_INFILESIZE_LARGE,
			(curl_off_t)stream->upload_bytes);
		HX_OS_HTTP_SET(CURLOPT_READFUNCTION, hx_os_http_read);
		HX_OS_HTTP_SET(CURLOPT_READDATA, &transfer);
	}
	else if (body.size || !strcmp(method, "POST")) {
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
	if (stream && stream->low_speed_seconds) {
		HX_OS_HTTP_SET(CURLOPT_LOW_SPEED_LIMIT,
			(long)stream->low_speed_bytes_per_second);
		HX_OS_HTTP_SET(CURLOPT_LOW_SPEED_TIME,
			(long)stream->low_speed_seconds);
	}
#undef HX_OS_HTTP_SET
	if (code == CURLE_OK)
		code = hx_os_http_perform(http, &transfer);
	if (metrics) {
		curl_off_t downloaded = 0, uploaded = 0;

		(void)curl_easy_getinfo(
			http->native, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
		(void)curl_easy_getinfo(
			http->native, CURLINFO_SIZE_UPLOAD_T, &uploaded);
		metrics->downloaded_bytes = (uint64_t)downloaded;
		metrics->uploaded_bytes = (uint64_t)uploaded;
		(void)curl_easy_getinfo(http->native, CURLINFO_NAMELOOKUP_TIME,
			&metrics->dns_ms);
		(void)curl_easy_getinfo(http->native, CURLINFO_CONNECT_TIME,
			&metrics->connect_ms);
		(void)curl_easy_getinfo(http->native, CURLINFO_APPCONNECT_TIME,
			&metrics->tls_ms);
		(void)curl_easy_getinfo(http->native,
			CURLINFO_STARTTRANSFER_TIME, &metrics->first_byte_ms);
		(void)curl_easy_getinfo(
			http->native, CURLINFO_TOTAL_TIME, &metrics->total_ms);
		metrics->dns_ms *= 1000;
		metrics->connect_ms *= 1000;
		metrics->tls_ms *= 1000;
		metrics->first_byte_ms *= 1000;
		metrics->total_ms *= 1000;
	}
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
	if (!owned)
		hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
	if (error)
		error->status = status;
	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
#else
	(void)http;
	(void)method;
	(void)url;
	(void)body;
	(void)headers;
	(void)header_count;
	(void)options;
	(void)stream;
	(void)metrics;
	(void)owned;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_http_request(hx_os_http *http, const char *method,
	const char *url, hx_os_bytes_view body, const char *const *headers,
	size_t header_count, const hx_os_http_options *options,
	hx_os_http_response *out, hx_os_error *error)
{
	return hx_os_http_request_impl(http, method, url, body, headers,
		header_count, options, NULL, NULL, 0, out, error);
}

hx_os_status hx_os_http_request_stream(hx_os_http *http, const char *method,
	const char *url, const char *const *headers, size_t header_count,
	const hx_os_http_stream_options *options, hx_os_http_response *out,
	hx_os_http_metrics *metrics, hx_os_error *error)
{
	const hx_os_http_stream_options defaults =
		HX_OS_HTTP_STREAM_OPTIONS_INIT;

	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_http_request_impl(http, method, url,
		(hx_os_bytes_view){NULL, 0}, headers, header_count,
		&options->request, options, metrics, 0, out, error);
}

#if HX_OS_HAVE_HTTP
typedef struct hx_os_http_file_transfer {
	hx_os_file *file;
	const hx_os_http_stream_options *options;
} hx_os_http_file_transfer;

#if HX_OS_HAVE_TEMP
static hx_os_status HX_OS_CALL hx_os_http_file_write(
	void *user, const void *data, size_t bytes, hx_os_error *error)
{
	hx_os_http_file_transfer *transfer = user;
	size_t completed;

	return hx_os_file_write_all(
		transfer->file, data, bytes, &completed, error);
}
#endif

static hx_os_status HX_OS_CALL hx_os_http_file_read(void *user, void *data,
	size_t capacity, size_t *completed, hx_os_error *error)
{
	hx_os_http_file_transfer *transfer = user;

	return hx_os_file_read_some(
		transfer->file, data, capacity, completed, error);
}

static hx_os_status HX_OS_CALL hx_os_http_file_response(
	void *user, const void *data, size_t bytes, hx_os_error *error)
{
	hx_os_http_file_transfer *transfer = user;

	return transfer->options->write(
		transfer->options->user, data, bytes, error);
}

static hx_os_status HX_OS_CALL hx_os_http_file_progress(
	void *user, uint64_t downloaded, uint64_t uploaded, hx_os_error *error)
{
	hx_os_http_file_transfer *transfer = user;

	return transfer->options->progress(
		transfer->options->user, downloaded, uploaded, error);
}
#endif

hx_os_status hx_os_http_download_file(hx_os_http *http, const char *url,
	const char *destination, int replace,
	const hx_os_http_stream_options *options, hx_os_http_response *out,
	hx_os_error *error)
{
#if HX_OS_HAVE_HTTP && HX_OS_HAVE_TEMP
	hx_os_http_stream_options stream = HX_OS_HTTP_STREAM_OPTIONS_INIT;
	hx_os_http_file_transfer transfer;
	hx_string parent = HX_STRING_INIT;
	hx_string full_path = HX_STRING_INIT;
	hx_os_status status, cleanup;
	size_t length;

	if (!destination || !*destination || !out ||
		(replace != 0 && replace != 1) ||
		(options &&
			(options->size < sizeof(*options) ||
				options->version != 1 || options->read ||
				options->write)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_http_enter(http, error);
	if (status != HX_OS_OK)
		return status;
	hx_string_init(&parent, out->body.context);
	hx_string_init(&full_path, out->body.context);
	if (options)
		stream = *options;
	status = hx_os_i_path_full(destination, &full_path, error);
	if (status != HX_OS_OK) {
		hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
		hx_string_destroy(&full_path);
		return status;
	}
	destination = hx_string_c_str(&full_path);
	length = strlen(destination);
	while (length && destination[length - 1] != '/'
#if defined(_WIN32)
		&& destination[length - 1] != '\\'
#endif
	)
		--length;
	status = length ? hx_string_assign(&parent,
				  (hx_string_view){destination, length}, error)
			: hx_string_assign_cstr(&parent, ".", error);
	if (status == HX_OS_OK)
		status = hx_os_temp_create_file(out->body.context,
			hx_string_c_str(&parent), "hx-download",
			&http->pending_file, error);
	hx_string_destroy(&parent);
	if (status == HX_OS_OK) {
		transfer.file = hx_os_temp_file(http->pending_file);
		transfer.options = options;
		stream.write = hx_os_http_file_write;
		stream.user = &transfer;
		if (stream.progress)
			stream.progress = hx_os_http_file_progress;
		status = hx_os_http_request_impl(http, "GET", url,
			(hx_os_bytes_view){NULL, 0}, NULL, 0, &stream.request,
			&stream, NULL, 1, out, error);
		if (status == HX_OS_OK &&
			(out->status_code < 200 || out->status_code >= 300))
			status = hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (status == HX_OS_OK)
			status = hx_os_temp_publish(http->pending_file,
				destination, replace, error);
	}
	cleanup = hx_os_temp_close(
		&http->pending_file, status == HX_OS_OK ? error : NULL);
	if (status == HX_OS_OK)
		status = cleanup;
	hx_string_destroy(&full_path);
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
	return status;
#else
	(void)http;
	(void)url;
	(void)destination;
	(void)replace;
	(void)options;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_http_upload_file(hx_os_http *http, const char *method,
	const char *url, const char *path, const char *const *headers,
	size_t header_count, const hx_os_http_stream_options *options,
	hx_os_http_response *out, hx_os_error *error)
{
#if HX_OS_HAVE_HTTP
	hx_os_http_stream_options stream = HX_OS_HTTP_STREAM_OPTIONS_INIT;
	hx_os_http_file_transfer transfer = {0};
	hx_os_status status, closed;

	if (!out || !path ||
		(options &&
			(options->size < sizeof(*options) ||
				options->version != 1 || options->read ||
				options->request.redirects)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_http_enter(http, error);
	if (status != HX_OS_OK)
		return status;
	if (options)
		stream = *options;
	transfer.options = options;
	status = hx_os_file_open(out->body.context, path, HX_OS_FILE_READ,
		&transfer.file, error);
	if (status == HX_OS_OK)
		status = hx_os_file_size(
			transfer.file, &stream.upload_bytes, error);
	if (status == HX_OS_OK) {
		stream.read = hx_os_http_file_read;
		stream.user = &transfer;
		if (stream.write)
			stream.write = hx_os_http_file_response;
		if (stream.progress)
			stream.progress = hx_os_http_file_progress;
		status = hx_os_http_request_impl(http, method, url,
			(hx_os_bytes_view){NULL, 0}, headers, header_count,
			&stream.request, &stream, NULL, 1, out, error);
	}
	closed = hx_os_file_close(
		&transfer.file, status == HX_OS_OK ? error : NULL);
	if (status == HX_OS_OK)
		status = closed;
	hx_os_atomic_store(&http->owner, 0, HX_OS_RELEASE);
	return status;
#else
	(void)http;
	(void)method;
	(void)url;
	(void)path;
	(void)headers;
	(void)header_count;
	(void)options;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

const char *hx_os_http_pending_file(const hx_os_http *http)
{
#if HX_OS_HAVE_HTTP
	return http ? hx_os_temp_path(http->pending_file) : NULL;
#else
	(void)http;
	return NULL;
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
	if (value->pending_file) {
		hx_os_status status =
			hx_os_temp_close(&value->pending_file, error);

		if (status != HX_OS_OK)
			return status;
	}
	*http = NULL;
	curl_easy_cleanup(value->native);
	if (value->multi)
		(void)curl_multi_cleanup(value->multi);
	hx_string_destroy(&value->dns_servers);
	hx_os_memory_free(value);
	curl_global_cleanup();
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)http;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
