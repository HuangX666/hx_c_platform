#include "internal.h"
#include "hx_os_url.h"

#if HX_OS_HAVE_URL || HX_OS_HAVE_HTTP
#include <curl/curl.h>
#endif
#if HX_OS_HAVE_URL

struct hx_os_url {
	CURLU *native;
	hx_os_context *context;
};
#endif

static int hx_os_url_hex(unsigned char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

hx_os_status hx_os_percent_encode(
	hx_os_bytes_view input, hx_string *out, hx_os_error *error)
{
	static const char hex[] = "0123456789ABCDEF";
	hx_string result;
	hx_os_status status;
	size_t i;

	if (!out || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (input.size > 16384)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	hx_string_init(&result, out->context);
	status = hx_string_reserve(&result, input.size * 3, error);
	for (i = 0; status == HX_OS_OK && i < input.size; ++i) {
		unsigned char ch = ((const unsigned char *)input.data)[i];
		char encoded[3] = {'%', hex[ch >> 4], hex[ch & 15]};
		int plain = (ch >= 'a' && ch <= 'z') ||
			(ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
			ch == '-' || ch == '_' || ch == '.' || ch == '~';

		status = hx_string_append(&result,
			(hx_string_view){plain ? (const char *)&ch : encoded,
				plain ? 1u : 3u},
			error);
	}
	if (status == HX_OS_OK)
		hx_string_move(out, &result);
	hx_string_destroy(&result);
	return status;
}

hx_os_status hx_os_percent_decode(hx_string_view input, int plus_as_space,
	hx_bytes *out, hx_os_error *error)
{
	hx_bytes result;
	hx_os_status status;
	size_t i;

	if (!out || (!input.data && input.size) ||
		(plus_as_space != 0 && plus_as_space != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (input.size > 49152)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	hx_bytes_init(&result, out->context);
	status = hx_bytes_reserve(
		&result, input.size > 16384 ? 16384 : input.size, error);
	for (i = 0; status == HX_OS_OK && i < input.size; ++i) {
		unsigned char ch = (unsigned char)input.data[i];

		if (ch == '%') {
			int high, low;

			if (input.size - i < 3 ||
				(high = hx_os_url_hex(
					 (unsigned char)input.data[i + 1])) <
					0 ||
				(low = hx_os_url_hex(
					 (unsigned char)input.data[i + 2])) <
					0) {
				status = hx_os_i_error(
					error, HX_OS_SYNTAX, 0, 0);
				if (error)
					error->cause_code = (int64_t)i;
				break;
			}
			ch = (unsigned char)((high << 4) | low);
			i += 2;
		}
		else if (ch == '+' && plus_as_space)
			ch = ' ';
		if (result.size == 16384) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
		result.data[result.size++] = ch;
	}
	if (status == HX_OS_OK)
		hx_bytes_move(out, &result);
	hx_bytes_destroy(&result);
	return status;
}

int hx_os_url_available(void)
{
	return HX_OS_HAVE_URL;
}

hx_os_status hx_os_url_parse(hx_os_context *context, const char *text,
	hx_os_url **out, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	hx_os_url *url;
	CURLUcode code;
	CURLcode initialized;
	size_t bytes, offset;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = strlen(text);
	if (bytes > 16384)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (hx_os_text_validate_utf8(text, bytes, &offset) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (initialized != CURLE_OK)
		return hx_os_i_error(error,
			initialized == CURLE_OUT_OF_MEMORY ? HX_OS_NO_MEMORY
							   : HX_OS_IO_ERROR,
			HX_OS_ERROR_CURL, initialized);
	if (!(curl_version_info(CURLVERSION_NOW)->features &
		    CURL_VERSION_THREADSAFE)) {
		curl_global_cleanup();
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}
	url = hx_os_i_zero(context, sizeof(*url));
	if (!url) {
		curl_global_cleanup();
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	url->context = context;
	url->native = curl_url();
	code = url->native ? curl_url_set(url->native, CURLUPART_URL, text,
				     CURLU_NON_SUPPORT_SCHEME)
			   : CURLUE_OUT_OF_MEMORY;
	if (code != CURLUE_OK) {
		hx_os_url_destroy(&url);
		return hx_os_i_error(error,
			code == CURLUE_OUT_OF_MEMORY ? HX_OS_NO_MEMORY
						     : HX_OS_SYNTAX,
			HX_OS_ERROR_CURL_URL, code);
	}
	*out = url;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)text;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_url_part(
	const hx_os_url *url, unsigned part, hx_string *out, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	static const CURLUPart parts[] = {CURLUPART_URL, CURLUPART_SCHEME,
		CURLUPART_HOST, CURLUPART_PORT, CURLUPART_PATH, CURLUPART_QUERY,
		CURLUPART_FRAGMENT};
	char *text = NULL;
	CURLUcode code;
	hx_os_status status;

	if (!url || !out || part > HX_OS_URL_FRAGMENT)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	code = curl_url_get(url->native, parts[part], &text, 0);
	if (code != CURLUE_OK)
		return hx_os_i_error(error,
			code == CURLUE_OUT_OF_MEMORY ? HX_OS_NO_MEMORY
						     : HX_OS_NOT_FOUND,
			HX_OS_ERROR_CURL_URL, code);
	status = hx_string_assign_cstr(out, text, error);
	curl_free(text);
	return status;
#else
	(void)url;
	(void)part;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

#if HX_OS_HAVE_URL
static hx_os_status hx_os_url_decode_text(
	hx_string_view input, hx_string *out, hx_os_error *error)
{
	hx_bytes bytes;
	hx_os_status status;

	hx_bytes_init(&bytes, out->context);
	status = hx_os_percent_decode(input, 1, &bytes, error);
	if (status == HX_OS_OK)
		status = hx_string_assign(out,
			(hx_string_view){(const char *)bytes.data, bytes.size},
			error);
	hx_bytes_destroy(&bytes);
	return status;
}

static hx_os_status hx_os_url_walk(const hx_os_url *url,
	hx_os_query_fn function, void *user, hx_os_error *error)
{
	hx_string query, key, value;
	hx_os_status status;
	size_t begin = 0, pairs = 0;

	if (!url || !function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&query, url->context);
	hx_string_init(&key, url->context);
	hx_string_init(&value, url->context);
	status = hx_os_url_part(url, HX_OS_URL_QUERY, &query, error);
	if (status == HX_OS_NOT_FOUND)
		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
	while (status == HX_OS_OK && begin < query.size) {
		size_t end = begin, equal;

		if (++pairs > 1024) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
		while (end < query.size && query.data[end] != '&')
			++end;
		equal = begin;
		while (equal < end && query.data[equal] != '=')
			++equal;
		status = hx_os_url_decode_text(
			(hx_string_view){query.data + begin, equal - begin},
			&key, error);
		if (status == HX_OS_OK)
			status = hx_os_url_decode_text(
				(hx_string_view){query.data +
						(equal < end ? equal + 1 : end),
					equal < end ? end - equal - 1 : 0},
				&value, error);
		if (status == HX_OS_OK)
			status = function(user, hx_string_view_of(&key),
				hx_string_view_of(&value), error);
		begin = end + 1;
	}
	hx_string_destroy(&value);
	hx_string_destroy(&key);
	hx_string_destroy(&query);
	return status;
}

typedef struct hx_os_url_find {
	hx_string_view key;
	hx_string *out;
	int found;
} hx_os_url_find;

static hx_os_status hx_os_url_find_pair(void *user, hx_string_view key,
	hx_string_view value, hx_os_error *error)
{
	hx_os_url_find *find = user;

	if (!find->found && key.size == find->key.size &&
		(!key.size || !memcmp(key.data, find->key.data, key.size))) {
		hx_os_status status = hx_string_assign(find->out, value, error);

		if (status != HX_OS_OK)
			return status;
		find->found = 1;
	}
	return HX_OS_OK;
}

static hx_os_status hx_os_url_add_pair(hx_string *query, hx_string_view key,
	hx_string_view value, hx_os_error *error)
{
	hx_string encoded;
	hx_os_status status;

	hx_string_init(&encoded, query->context);
	status = hx_os_percent_encode(
		(hx_os_bytes_view){key.data, key.size}, &encoded, error);
	if (status == HX_OS_OK && query->size)
		status = hx_string_append_cstr(query, "&", error);
	if (status == HX_OS_OK)
		status = hx_string_append(
			query, hx_string_view_of(&encoded), error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(query, "=", error);
	if (status == HX_OS_OK)
		status = hx_os_percent_encode(
			(hx_os_bytes_view){value.data, value.size}, &encoded,
			error);
	if (status == HX_OS_OK)
		status = hx_string_append(
			query, hx_string_view_of(&encoded), error);
	hx_string_destroy(&encoded);
	if (status == HX_OS_OK && query->size > 16384)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	return status;
}

typedef struct hx_os_url_filter {
	hx_string_view key;
	hx_string *query;
} hx_os_url_filter;

static hx_os_status hx_os_url_keep_pair(void *user, hx_string_view key,
	hx_string_view value, hx_os_error *error)
{
	hx_os_url_filter *filter = user;

	if (key.size == filter->key.size &&
		(!key.size || !memcmp(key.data, filter->key.data, key.size)))
		return HX_OS_OK;
	return hx_os_url_add_pair(filter->query, key, value, error);
}

static hx_os_status hx_os_url_query_change(hx_os_url *url, hx_string_view key,
	hx_string_view value, int replace, hx_os_error *error)
{
	hx_string query;
	hx_os_url_filter filter = {key, &query};
	hx_os_status status;
	CURLU *copy = NULL;
	CURLUcode code;
	size_t offset;

	if ((!key.data && key.size) || (!value.data && value.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_text_validate_utf8(key.data, key.size, &offset) != HX_OS_OK ||
		hx_os_text_validate_utf8(value.data, value.size, &offset) !=
			HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	if (!url)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&query, url->context);
	status = replace
		? hx_os_url_walk(url, hx_os_url_keep_pair, &filter, error)
		: hx_os_url_part(url, HX_OS_URL_QUERY, &query, error);
	if (status == HX_OS_NOT_FOUND)
		status = HX_OS_OK;
	if (status == HX_OS_OK)
		status = hx_os_url_add_pair(&query, key, value, error);
	if (status == HX_OS_OK) {
		copy = curl_url_dup(url->native);
		code = copy ? curl_url_set(copy, CURLUPART_QUERY,
				      hx_string_c_str(&query), 0)
			    : CURLUE_OUT_OF_MEMORY;
		if (code != CURLUE_OK)
			status = hx_os_i_error(error,
				code == CURLUE_OUT_OF_MEMORY ? HX_OS_NO_MEMORY
							     : HX_OS_SYNTAX,
				HX_OS_ERROR_CURL_URL, code);
	}
	if (status == HX_OS_OK) {
		char *full = NULL;

		code = curl_url_get(copy, CURLUPART_URL, &full, 0);
		if (code != CURLUE_OK)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY,
				HX_OS_ERROR_CURL_URL, code);
		else if (strlen(full) > 16384)
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		curl_free(full);
	}
	if (status == HX_OS_OK) {
		curl_url_cleanup(url->native);
		url->native = copy;
		copy = NULL;
	}
	curl_url_cleanup(copy);
	hx_string_destroy(&query);
	return status;
}
#endif

hx_os_status hx_os_url_query_get(const hx_os_url *url, hx_string_view key,
	hx_string *out, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	hx_string result;
	hx_os_url_find find = {key, &result, 0};
	hx_os_status status;

	if (!out || (!key.data && key.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&result, out->context);
	status = hx_os_url_walk(url, hx_os_url_find_pair, &find, error);
	if (status == HX_OS_OK && !find.found)
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (status == HX_OS_OK)
		hx_string_move(out, &result);
	hx_string_destroy(&result);
	return status;
#else
	(void)url;
	(void)key;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_url_query_append(hx_os_url *url, hx_string_view key,
	hx_string_view value, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	return hx_os_url_query_change(url, key, value, 0, error);
#else
	(void)url;
	(void)key;
	(void)value;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_url_query_set(hx_os_url *url, hx_string_view key,
	hx_string_view value, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	return hx_os_url_query_change(url, key, value, 1, error);
#else
	(void)url;
	(void)key;
	(void)value;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_url_query_each(const hx_os_url *url, hx_os_query_fn function,
	void *user, hx_os_error *error)
{
#if HX_OS_HAVE_URL
	return hx_os_url_walk(url, function, user, error);
#else
	(void)url;
	(void)function;
	(void)user;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_url_destroy(hx_os_url **url)
{
#if HX_OS_HAVE_URL
	hx_os_url *value;

	if (!url || !*url)
		return;
	value = *url;
	*url = NULL;
	curl_url_cleanup(value->native);
	hx_os_memory_free(value);
	curl_global_cleanup();
#else
	(void)url;
#endif
}

const char *hx_os_i_transfer_message(uint32_t domain, int64_t code)
{
#if HX_OS_HAVE_URL || HX_OS_HAVE_HTTP
	if (domain == HX_OS_ERROR_CURL && code >= 0 && code < CURL_LAST)
		return curl_easy_strerror((CURLcode)code);
	if (domain == HX_OS_ERROR_CURL_URL && code >= 0 && code < CURLUE_LAST)
		return curl_url_strerror((CURLUcode)code);
#else
	(void)domain;
	(void)code;
#endif
	return "transfer failure";
}
