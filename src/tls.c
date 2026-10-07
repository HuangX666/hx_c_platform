#include "tls_private.h"
#include "file_utils_private.h"

#if HX_OS_HAVE_TLS
#if defined(_MSC_VER)
static __declspec(thread) hx_os_tls *hx_tls_active;
#else
static __thread hx_os_tls *hx_tls_active;
#endif

void hx_tls_i_guard(hx_os_tls *tls)
{
	hx_tls_active = tls;
}

int hx_tls_i_reentrant(hx_os_tls *tls)
{
	(void)tls;
	return hx_tls_active != NULL;
}

void hx_tls_i_lock(hx_os_tls *tls)
{
	(void)hx_os_mutex_lock(tls->mutex, NULL);
}

void hx_tls_i_unlock(hx_os_tls *tls)
{
	(void)hx_os_mutex_unlock(tls->mutex, NULL);
}

void hx_tls_i_wake(hx_os_tls *tls)
{
	hx_os_condition_notify_all(tls->condition);
	if (tls->loop)
		hx_os_i_loop_wake(tls->loop);
}

size_t hx_tls_i_ring_write(
	hx_tls_ring *ring, size_t capacity, const void *data, size_t bytes)
{
	size_t at = (ring->head + ring->size) % capacity;
	size_t first;

	if (bytes > capacity - ring->size)
		bytes = capacity - ring->size;
	first = bytes < capacity - at ? bytes : capacity - at;
	if (bytes) {
		memcpy(ring->data + at, data, first);
		memcpy(ring->data, (const unsigned char *)data + first,
			bytes - first);
	}
	ring->size += bytes;

	return bytes;
}

size_t hx_tls_i_ring_read(
	hx_tls_ring *ring, size_t capacity, void *data, size_t bytes)
{
	size_t first;

	if (bytes > ring->size)
		bytes = ring->size;
	first = bytes < capacity - ring->head ? bytes : capacity - ring->head;
	if (bytes) {
		memcpy(data, ring->data + ring->head, first);
		memcpy((unsigned char *)data + first, ring->data,
			bytes - first);
	}
	ring->head = (ring->head + bytes) % capacity;
	ring->size -= bytes;

	return bytes;
}

void hx_tls_i_dispose(hx_os_tls *tls)
{
	const hx_os_tls_options *options = &tls->options;

	hx_os_memory_free((void *)options->ca_file);
	hx_os_memory_free((void *)options->certificate_file);
	hx_os_memory_free((void *)options->private_key_file);
	if (options->key_password) {
		hx_os_secure_zero((void *)options->key_password,
			strlen(options->key_password));
		hx_os_memory_free((void *)options->key_password);
	}
	hx_os_memory_free((void *)options->server_name);
	hx_os_memory_free(tls->host);
	hx_os_memory_free(tls->peers);
	hx_os_condition_destroy(tls->condition);
	hx_os_mutex_destroy(tls->mutex);
	hx_os_memory_free(tls);
}

static hx_os_status hx_tls_path(
	const char *input, hx_string *out, hx_os_error *error)
{
#if defined(_WIN32)
	wchar_t *wide = hx_os_i_windows_path(out->context, input, error);
	hx_os_status status;

	if (!wide)
		return error->status;
	status = hx_string_assign_encoded(
		out, HX_OS_UTF16LE, wide, wcslen(wide) * sizeof(*wide), error);
	hx_os_memory_free(wide);

	return status;
#else
	hx_os_status status = hx_os_i_path_validate(input, error);

	if (status == HX_OS_OK && input[0] != '/') {
		status = hx_os_i_path_current(out, error);
		if (status == HX_OS_OK)
			status = hx_string_append_cstr(out, "/", error);
	}
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(out, input, error);

	return status;
#endif
}

static hx_os_status hx_tls_copy(hx_os_context *context, const char *input,
	int path, const char **out, hx_os_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;

	*out = NULL;
	if (!input)
		return HX_OS_OK;
	hx_string_init(&text, context);
	status = path ? hx_tls_path(input, &text, error)
		      : hx_string_assign_cstr(&text, input, error);
	if (status == HX_OS_OK) {
		char *copy = hx_os_i_alloc(context, text.size + 1, 0);

		if (!copy)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		else {
			memcpy(copy, hx_string_c_str(&text), text.size + 1);
			*out = copy;
		}
	}
	if (text.data)
		hx_os_secure_zero(text.data, text.size);
	hx_string_destroy(&text);

	return status;
}

static hx_os_status hx_tls_start(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *input, int server,
	hx_os_tls **out, hx_os_error *error)
{
	hx_os_tls_options options = HX_OS_TLS_OPTIONS_INIT;
	hx_os_tls *tls;
	hx_os_status status;
	hx_os_error local;
	const char *host_copy = NULL;
	size_t bytes;

	if (!error)
		error = &local;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (input) {
		if (input->size < sizeof(options) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}
	if (!host || !*host || strlen(host) > 1024 || (!server && !port) ||
		options.queue_capacity < HX_TLS_RECORD ||
		options.queue_capacity > 16 * 1024 * 1024 ||
		!options.max_peers || options.max_peers > 1024 ||
		!options.handshake_timeout_ms || (options.flags & ~7u) ||
		(options.certificate_file == NULL) !=
			(options.private_key_file == NULL) ||
		(server && !options.certificate_file) ||
		(!server &&
			(options.flags &
				HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE)) ||
		(server && options.server_name) ||
		(options.key_password && strlen(options.key_password) > 1024))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (bytes = 0; host[bytes]; ++bytes)
		if ((unsigned char)host[bytes] <= 32 ||
			(unsigned char)host[bytes] >= 127)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!server)
		options.max_peers = 1;
	/* Bound the complete first-party peer allocation, including records. */
	bytes = options.queue_capacity * 2 + HX_TLS_RECORD * 4 +
		sizeof(hx_tls_peer);
	if (options.max_peers > (256 * 1024 * 1024u) / bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	tls = hx_os_i_zero(context, sizeof(*tls));
	if (!tls)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	tls->context = context;
	tls->port = port;
	tls->server = server;
	tls->options = options;
	tls->options.ca_file = NULL;
	tls->options.certificate_file = NULL;
	tls->options.private_key_file = NULL;
	tls->options.key_password = NULL;
	tls->options.server_name = NULL;
	status = hx_tls_copy(context, host, 0, &host_copy, error);
	tls->host = (char *)host_copy;
	if (status == HX_OS_OK)
		status = hx_tls_copy(context, options.ca_file, 1,
			&tls->options.ca_file, error);
	if (status == HX_OS_OK)
		status = hx_tls_copy(context, options.certificate_file, 1,
			&tls->options.certificate_file, error);
	if (status == HX_OS_OK)
		status = hx_tls_copy(context, options.private_key_file, 1,
			&tls->options.private_key_file, error);
	if (status == HX_OS_OK)
		status = hx_tls_copy(context, options.key_password, 0,
			&tls->options.key_password, error);
	if (status == HX_OS_OK)
		status = hx_tls_copy(context, options.server_name, 0,
			&tls->options.server_name, error);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &tls->mutex, error);
	if (status == HX_OS_OK)
		status =
			hx_os_condition_create(context, &tls->condition, error);
	if (status == HX_OS_OK) {
		tls->peers = hx_os_i_zero(
			context, options.max_peers * sizeof(*tls->peers));
		if (!tls->peers)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (status == HX_OS_OK)
		status = hx_os_thread_create(
			context, hx_tls_i_worker, tls, &tls->worker, error);
	if (status == HX_OS_OK)
		*out = tls;
	else
		hx_tls_i_dispose(tls);

	return status;
}

hx_os_status hx_os_tls_start_connect(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error)
{
	return hx_tls_start(context, host, port, options, 0, out, error);
}

hx_os_status hx_os_tls_start_listen(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error)
{
	return hx_tls_start(context, host, port, options, 1, out, error);
}
#endif
