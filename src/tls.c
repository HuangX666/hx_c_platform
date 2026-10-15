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
	if (tls->closing)
		hx_tls_i_notify(tls);
	if (tls->loop)
		hx_os_i_loop_wake(tls->loop);
}

void hx_tls_i_notify(hx_os_tls *tls)
{
	++tls->change_sequence;
	hx_os_condition_notify_all(tls->condition);
	hx_tls_i_coro_notify(tls);
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
	hx_os_memory_free(tls->crl_file);
	hx_os_memory_free(tls->resolver_servers);
	hx_os_memory_free(tls->host);
	hx_os_memory_free(tls->peers);
	hx_os_condition_destroy(tls->condition);
	hx_os_mutex_destroy(tls->mutex);
	hx_os_tls_profile_release(tls->profile);
	hx_os_memory_free(tls);
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
	status = path ? hx_os_i_path_full(input, &text, error)
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

static hx_os_status hx_tls_extended(
	hx_os_tls *tls, const hx_os_tls_options_ex *options, hx_os_error *error)
{
	hx_os_status status;
	const char *crl = NULL;
	size_t i;

	if (!options)
		return HX_OS_OK;
	if (options->size < sizeof(*options) || options->version != 1 ||
		(tls->server && options->resolver) ||
		(options->enable_session_tickets != 0 &&
			options->enable_session_tickets != 1) ||
		(!tls->server && options->enable_session_tickets) ||
		options->attempt_delay_ms > 60000 || options->alpn_count > 16 ||
		(!options->alpn && options->alpn_count) ||
		(options->require_alpn != 0 && options->require_alpn != 1) ||
		(options->require_alpn && !options->alpn_count) ||
		options->crl_mode > HX_OS_TLS_CRL_CHAIN ||
		(options->crl_mode == 0) != (options->crl_file == NULL) ||
		(tls->server &&
			!(tls->options.flags &
				HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE) &&
			(options->certificate_sha256 || options->crl_mode)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_tcp_options_validate(&options->socket, error);
	if (status != HX_OS_OK)
		return status;
	tls->extended = 1;
	tls->tcp_options = options->socket;
	if (tls->tcp_options.nodelay < 0 &&
		(tls->options.flags & HX_OS_TLS_NODELAY))
		tls->tcp_options.nodelay = 1;
	tls->attempt_delay_ms = options->attempt_delay_ms;
	tls->require_alpn = options->require_alpn;
	tls->session_tickets = options->enable_session_tickets;
	tls->crl_mode = options->crl_mode;
	tls->pinned = options->certificate_sha256 != NULL;
	if (tls->pinned)
		memcpy(tls->certificate_sha256, options->certificate_sha256,
			32);
	for (i = 0; i < options->alpn_count; ++i) {
		size_t length = 0;
		const char *name = options->alpn[i];

		if (!name)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		while (name[length]) {
			if ((unsigned char)name[length] < 33 ||
				(unsigned char)name[length] > 126 ||
				length == 255)
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
			++length;
		}
		if (!length)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		tls->alpn[tls->alpn_bytes++] = (unsigned char)length;
		memcpy(tls->alpn + tls->alpn_bytes, name, length);
		tls->alpn_bytes += length;
	}
	status = hx_tls_copy(tls->context, options->crl_file, 1, &crl, error);
	tls->crl_file = (char *)crl;
	if (status == HX_OS_OK && options->resolver) {
		const hx_os_resolver_options *resolver = options->resolver;
		const char *servers = NULL;

		if (resolver->size < sizeof(*resolver) ||
			resolver->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		tls->resolver_options = *resolver;
		status = hx_tls_copy(
			tls->context, resolver->servers, 0, &servers, error);
		tls->resolver_servers = (char *)servers;
		tls->resolver_options.servers = servers;
		tls->resolve_async = 1;
	}

	return status;
}

static hx_os_status hx_tls_start(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *input,
	const hx_os_tls_options_ex *extended, int server,
	hx_os_tls_profile *profile, int start, hx_os_tls **out,
	hx_os_error *error)
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
	// Bound the complete first-party peer allocation, including records.
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
	tls->profile = profile;
	hx_os_tls_profile_retain(profile);
	tls->options = options;
	tls->options.ca_file = NULL;
	tls->options.certificate_file = NULL;
	tls->options.private_key_file = NULL;
	tls->options.key_password = NULL;
	tls->options.server_name = NULL;
	status = hx_tls_extended(tls, extended, error);
	if (status == HX_OS_OK)
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
	if (status == HX_OS_OK && start)
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
	return hx_tls_start(
		context, host, port, options, NULL, 0, NULL, 1, out, error);
}

hx_os_status hx_os_tls_start_listen(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error)
{
	return hx_tls_start(
		context, host, port, options, NULL, 1, NULL, 1, out, error);
}

hx_os_status hx_os_tls_start_connect_ex(hx_os_context *context,
	const char *host, uint16_t port, const hx_os_tls_options_ex *input,
	hx_os_tls **out, hx_os_error *error)
{
	const hx_os_tls_options_ex defaults = HX_OS_TLS_OPTIONS_EX_INIT;
	const hx_os_tls_options_ex *options = input ? input : &defaults;

	if (out)
		*out = NULL;
	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_tls_start(context, host, port, &options->base, options, 0,
		NULL, 1, out, error);
}

hx_os_status hx_os_tls_start_listen_ex(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options_ex *input, hx_os_tls **out,
	hx_os_error *error)
{
	const hx_os_tls_options_ex defaults = HX_OS_TLS_OPTIONS_EX_INIT;
	const hx_os_tls_options_ex *options = input ? input : &defaults;

	if (out)
		*out = NULL;
	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_tls_start(context, host, port, &options->base, options, 1,
		NULL, 1, out, error);
}

hx_os_status hx_tls_i_configuration(hx_os_context *context, const char *host,
	uint16_t port, const hx_os_tls_options_ex *options, hx_os_tls **out,
	hx_os_error *error)
{
	return hx_tls_start(context, host, port, &options->base, options, 0,
		NULL, 0, out, error);
}

hx_os_status hx_tls_i_profile_start(
	hx_os_tls_profile *profile, hx_os_tls **out, hx_os_error *error)
{
	hx_os_tls_options_ex options = HX_OS_TLS_OPTIONS_EX_INIT;
	hx_os_tls *config = profile->configuration;
	char names[4096];
	const char *protocols[16];
	size_t offset = 0, used = 0;

	options.base = config->options;
	options.socket = config->tcp_options;
	options.attempt_delay_ms = config->attempt_delay_ms;
	options.require_alpn = config->require_alpn;
	options.crl_mode = config->crl_mode;
	options.crl_file = config->crl_file;
	options.certificate_sha256 =
		config->pinned ? config->certificate_sha256 : NULL;
	options.resolver =
		config->resolve_async ? &config->resolver_options : NULL;
	while (offset < config->alpn_bytes) {
		size_t length = config->alpn[offset++];

		protocols[options.alpn_count++] = names + used;
		memcpy(names + used, config->alpn + offset, length);
		names[used + length] = 0;
		used += length + 1;
		offset += length;
	}
	options.alpn = protocols;
	return hx_tls_start(config->context, config->host, config->port,
		&options.base, &options, 0, profile, 1, out, error);
}
#endif
