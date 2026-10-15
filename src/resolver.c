#include "net_private.h"
#include "resolver_private.h"
#include <stdio.h>

#if HX_OS_HAVE_CARES
#include <ares.h>
#if defined(_WIN32)
static SRWLOCK hx_os_resolver_global_mutex = SRWLOCK_INIT;
#else
#include <pthread.h>
static pthread_mutex_t hx_os_resolver_global_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
static size_t hx_os_resolver_global_users;

struct hx_os_resolver {
	hx_os_network *network;
	ares_channel_t *channel;
	hx_os_atomic owner;
	ares_fd_events_t sockets[64];
	size_t socket_count;
	int overflow;
};

typedef struct hx_os_resolve_query {
	hx_os_address_list *list;
	hx_os_status status;
	int done, native_code, timeouts;
} hx_os_resolve_query;

static int hx_os_resolver_lease(int acquire)
{
	int code = ARES_SUCCESS;

#if defined(_WIN32)
	AcquireSRWLockExclusive(&hx_os_resolver_global_mutex);
#else
	(void)pthread_mutex_lock(&hx_os_resolver_global_mutex);
#endif
	if (acquire) {
		if (!hx_os_resolver_global_users)
			code = ares_library_init(ARES_LIB_INIT_ALL);
		if (code == ARES_SUCCESS)
			++hx_os_resolver_global_users;
	}
	else if (!--hx_os_resolver_global_users)
		ares_library_cleanup();
#if defined(_WIN32)
	ReleaseSRWLockExclusive(&hx_os_resolver_global_mutex);
#else
	(void)pthread_mutex_unlock(&hx_os_resolver_global_mutex);
#endif

	return code;
}

static hx_os_status hx_os_resolver_error(int code, hx_os_error *error)
{
	hx_os_status status = code == ARES_SUCCESS ? HX_OS_OK
		: code == ARES_ENOMEM		   ? HX_OS_NO_MEMORY
		: code == ARES_ENOTFOUND || code == ARES_ENODATA
		? HX_OS_NOT_FOUND
		: code == ARES_ETIMEOUT	  ? HX_OS_TIMEOUT
		: code == ARES_ECANCELLED ? HX_OS_CANCELLED
		: code == ARES_ENOTIMP	  ? HX_OS_NOT_SUPPORTED
					  : HX_OS_IO_ERROR;

	return hx_os_i_error(error, status,
		code == ARES_SUCCESS ? HX_OS_ERROR_NONE : HX_OS_ERROR_CARES,
		code);
}

static void hx_os_resolver_socket(
	void *user, ares_socket_t socket, int readable, int writable)
{
	hx_os_resolver *resolver = user;
	size_t i;

	for (i = 0; i < resolver->socket_count; ++i)
		if (resolver->sockets[i].fd == socket)
			break;
	if (!readable && !writable) {
		if (i < resolver->socket_count)
			resolver->sockets[i] =
				resolver->sockets[--resolver->socket_count];
		return;
	}
	if (i == resolver->socket_count) {
		if (i == 64) {
			resolver->overflow = 1;
			return;
		}
		++resolver->socket_count;
	}
	resolver->sockets[i].fd = socket;
	resolver->sockets[i].events = (readable ? ARES_FD_EVENT_READ : 0u) |
		(writable ? ARES_FD_EVENT_WRITE : 0u);
}

static void hx_os_resolver_done(
	void *user, int code, int timeouts, struct ares_addrinfo *result)
{
	hx_os_resolve_query *query = user;
	struct ares_addrinfo_node *node;

	query->native_code = code;
	query->timeouts = timeouts;
	query->status = hx_os_resolver_error(code, NULL);
	for (node = result ? result->nodes : NULL;
		node && query->status == HX_OS_OK; node = node->ai_next) {
		hx_os_address address;
		size_t i;

		if (!hx_os_i_address_public(node->ai_addr, &address))
			continue;
		for (i = 0; i < query->list->count; ++i)
			if (!memcmp(query->list->addresses + i, &address,
				    sizeof(address)))
				break;
		if (i != query->list->count)
			continue;
		if (i == 256) {
			query->status = HX_OS_LIMIT;
			break;
		}
		query->list->addresses[query->list->count++] = address;
	}
	if (query->status == HX_OS_OK && !query->list->count)
		query->status = HX_OS_NOT_FOUND;
	if (result)
		ares_freeaddrinfo(result);
	query->done = 1;
}

static hx_os_status hx_os_resolver_poll(
	hx_os_resolver *resolver, hx_os_deadline deadline, hx_os_error *error)
{
	ares_fd_events_t events[64];
	struct timeval maximum, timeout, *wait;
	uint32_t ms = hx_os_i_wait_ms(deadline);
	size_t i, count = resolver->socket_count;
	int result, code;
#if defined(_WIN32)
	fd_set read_set, write_set, error_set;
#else
	struct pollfd descriptors[64];
#endif

	maximum.tv_sec = (long)(ms / 1000);
	maximum.tv_usec = (long)(ms % 1000) * 1000;
	wait = ares_timeout(resolver->channel, &maximum, &timeout);
	memcpy(events, resolver->sockets, count * sizeof(*events));
#if defined(_WIN32)
	FD_ZERO(&read_set);
	FD_ZERO(&write_set);
	FD_ZERO(&error_set);
	for (i = 0; i < count; ++i) {
		if (events[i].events & ARES_FD_EVENT_READ)
			FD_SET(events[i].fd, &read_set);
		if (events[i].events & ARES_FD_EVENT_WRITE)
			FD_SET(events[i].fd, &write_set);
		FD_SET(events[i].fd, &error_set);
	}
	if (count) {
		result = select(0, &read_set, &write_set, &error_set, wait);
		if (result == SOCKET_ERROR)
			return hx_os_i_socket_error(error, WSAGetLastError());
	}
	else
		Sleep((DWORD)(wait->tv_sec * 1000 +
			(wait->tv_usec + 999) / 1000));
	for (i = 0; i < count; ++i)
		events[i].events =
			(FD_ISSET(events[i].fd, &read_set) ||
						FD_ISSET(events[i].fd,
							&error_set)
					? ARES_FD_EVENT_READ
					: 0u) |
			(FD_ISSET(events[i].fd, &write_set)
					? ARES_FD_EVENT_WRITE
					: 0u);
#else
	for (i = 0; i < count; ++i) {
		descriptors[i].fd = events[i].fd;
		descriptors[i].events =
			(short)((events[i].events & ARES_FD_EVENT_READ ? POLLIN
								       : 0) |
				(events[i].events & ARES_FD_EVENT_WRITE
						? POLLOUT
						: 0));
		descriptors[i].revents = 0;
	}
	ms = (uint32_t)(wait->tv_sec * 1000 + (wait->tv_usec + 999) / 1000);
	result = poll(descriptors, (nfds_t)count, (int)ms);
	if (result < 0 && errno != EINTR)
		return hx_os_i_errno(error, errno);
	for (i = 0; i < count; ++i)
		events[i].events =
			(descriptors[i].revents &
						(POLLIN | POLLERR | POLLHUP |
							POLLNVAL)
					? ARES_FD_EVENT_READ
					: 0u) |
			(descriptors[i].revents & POLLOUT ? ARES_FD_EVENT_WRITE
							  : 0u);
#endif
	code = (int)ares_process_fds(
		resolver->channel, events, count, ARES_PROCESS_FLAG_NONE);

	return hx_os_resolver_error(code, error);
}
#endif

int hx_os_resolver_available(void)
{
	return HX_OS_HAVE_CARES;
}

hx_os_status hx_os_resolver_options_init(
	hx_os_resolver_options *options, size_t capacity)
{
	const hx_os_resolver_options defaults = HX_OS_RESOLVER_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_i_resolver_options_check(
	const hx_os_resolver_options *options, size_t *server_bytes,
	hx_os_error *error)
{
	size_t i = 0, servers = 1;

	if (options->size < sizeof(*options) || options->version != 1 ||
		options->timeout_ms < 250 || options->timeout_ms > 60000 ||
		!options->tries || options->tries > 8 ||
		(options->use_hosts_file != 0 && options->use_hosts_file != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (options->servers && !*options->servers)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (options->servers)
		for (i = 0; options->servers[i]; ++i) {
			if (i == 4096 ||
				(unsigned char)options->servers[i] > 126 ||
				(unsigned char)options->servers[i] < 33)
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
			if (options->servers[i] == ',' && ++servers > 16)
				return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		}
	*server_bytes = options->servers ? i + 1 : 0;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_resolver_options *hx_os_i_resolver_options_copy(hx_os_context *context,
	const hx_os_resolver_options *input, hx_os_error *error)
{
	const hx_os_resolver_options defaults = HX_OS_RESOLVER_OPTIONS_INIT;
	const hx_os_resolver_options *options = input ? input : &defaults;
	hx_os_resolver_options *copy;
	size_t bytes;

	if (!hx_os_resolver_available()) {
		(void)hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		return NULL;
	}
	if (hx_os_i_resolver_options_check(options, &bytes, error) != HX_OS_OK)
		return NULL;
	copy = hx_os_i_alloc(context, sizeof(*copy) + bytes, 0);
	if (!copy) {
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		return NULL;
	}
	*copy = *options;
	if (bytes) {
		copy->servers = (char *)(copy + 1);
		memcpy((char *)copy->servers, options->servers, bytes);
	}
	return copy;
}

hx_os_status hx_os_resolver_create(hx_os_context *context,
	const hx_os_resolver_options *input, hx_os_resolver **out,
	hx_os_error *error)
{
#if HX_OS_HAVE_CARES
	const hx_os_resolver_options defaults = HX_OS_RESOLVER_OPTIONS_INIT;
	const hx_os_resolver_options *options = input ? input : &defaults;
	hx_os_resolver *resolver;
	struct ares_options native;
	hx_os_status status;
	int code, initialized = 0;
	size_t server_bytes;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_i_resolver_options_check(options, &server_bytes, error);
	if (status != HX_OS_OK)
		return status;
	resolver = hx_os_i_zero(context, sizeof(*resolver));
	if (!resolver)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_network_create(context, &resolver->network, error);
	if (status == HX_OS_OK) {
		code = hx_os_resolver_lease(1);
		status = hx_os_resolver_error(code, error);
		initialized = code == ARES_SUCCESS;
	}
	memset(&native, 0, sizeof(native));
	native.timeout = (int)options->timeout_ms;
	native.tries = (int)options->tries;
	native.lookups = options->use_hosts_file ? "fb" : "b";
	native.sock_state_cb = hx_os_resolver_socket;
	native.sock_state_cb_data = resolver;
	native.qcache_max_ttl = 0;
	if (status == HX_OS_OK) {
		code = ares_init_options(&resolver->channel, &native,
			ARES_OPT_TIMEOUTMS | ARES_OPT_TRIES | ARES_OPT_LOOKUPS |
				ARES_OPT_SOCK_STATE_CB | ARES_OPT_QUERY_CACHE);
		status = hx_os_resolver_error(code, error);
	}
	if (status == HX_OS_OK && options->servers)
		status = hx_os_resolver_error(
			ares_set_servers_ports_csv(
				resolver->channel, options->servers),
			error);
	if (status != HX_OS_OK) {
		if (resolver->channel)
			ares_destroy(resolver->channel);
		if (initialized)
			(void)hx_os_resolver_lease(0);
		(void)hx_os_network_destroy(&resolver->network, NULL);
		hx_os_memory_free(resolver);
	}
	else {
		hx_os_atomic_store(&resolver->owner, 0, HX_OS_RELAXED);
		*out = resolver;
	}

	return status;
#else
	(void)context;
	(void)input;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_resolver_resolve_until(hx_os_resolver *resolver,
	const char *host, uint16_t port, int protocol, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, int (*cancelled)(void *), void *user,
	hx_os_address_list **out, hx_os_error *error)
{
#if HX_OS_HAVE_CARES
	struct ares_addrinfo_hints hints;
	hx_os_resolve_query query;
	hx_os_status status = HX_OS_OK;
	int64_t expected = 0, thread = (int64_t)hx_os_thread_id();
	char service[8];
	size_t i;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!resolver || !thread || !host || !*host || !port ||
		(protocol != HX_OS_TCP && protocol != HX_OS_UDP))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (i = 0; host[i]; ++i)
		if (i == 1024 || (unsigned char)host[i] < 33 ||
			(unsigned char)host[i] >= 127)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_os_atomic_compare_exchange(&resolver->owner, &expected, thread,
		    HX_OS_ACQ_REL, HX_OS_ACQUIRE))
		return hx_os_i_error(error,
			expected == thread ? HX_OS_DEADLOCK : HX_OS_BUSY, 0, 0);
	memset(&query, 0, sizeof(query));
	query.list =
		hx_os_i_zero(resolver->network->context, sizeof(*query.list));
	if (!query.list) {
		hx_os_atomic_store(&resolver->owner, 0, HX_OS_RELEASE);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = protocol == HX_OS_TCP ? SOCK_STREAM : SOCK_DGRAM;
	hints.ai_flags = ARES_AI_NUMERICSERV | ARES_AI_NOSORT;
	(void)snprintf(service, sizeof(service), "%u", (unsigned int)port);
	resolver->overflow = 0;
	if (hx_os_cancel_requested(cancel) || (cancelled && cancelled(user)))
		status = hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
	else if (hx_os_deadline_expired(deadline))
		status = hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	else
		ares_getaddrinfo(resolver->channel, host, service, &hints,
			hx_os_resolver_done, &query);
	while (status == HX_OS_OK && !query.done) {
		hx_os_deadline wait = deadline;

		if (resolver->overflow)
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		else if (hx_os_cancel_requested(cancel) ||
			(cancelled && cancelled(user)))
			status = hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
		else if (hx_os_deadline_expired(deadline))
			status = hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		if (status != HX_OS_OK)
			break;
		if (cancel || cancelled) {
			hx_os_deadline check = hx_os_deadline_after_ms(50);

			if (check < wait)
				wait = check;
		}
		status = hx_os_resolver_poll(resolver, wait, error);
	}
	if (status == HX_OS_OK && resolver->overflow)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (status == HX_OS_OK) {
		status = query.status;
		(void)hx_os_resolver_error(query.native_code, error);
		if (error) {
			error->status = status;
			error->cause_code = query.timeouts;
		}
	}
	// Cancellation callbacks finish before this stack frame retires.
	if (!query.done)
		ares_cancel(resolver->channel);
	if (status == HX_OS_OK)
		*out = query.list;
	else
		hx_os_memory_free(query.list);
	hx_os_atomic_store(&resolver->owner, 0, HX_OS_RELEASE);

	return status;
#else
	(void)resolver;
	(void)host;
	(void)port;
	(void)protocol;
	(void)deadline;
	(void)cancel;
	(void)cancelled;
	(void)user;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_resolver_resolve_until(hx_os_resolver *resolver,
	const char *host, uint16_t port, int protocol, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, hx_os_address_list **out,
	hx_os_error *error)
{
	return hx_os_i_resolver_resolve_until(resolver, host, port, protocol,
		deadline, cancel, NULL, NULL, out, error);
}

hx_os_status hx_os_resolver_destroy(hx_os_resolver **owner, hx_os_error *error)
{
#if HX_OS_HAVE_CARES
	hx_os_resolver *resolver;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	resolver = *owner;
	if (!resolver)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_os_atomic_load(&resolver->owner, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	ares_destroy(resolver->channel);
	(void)hx_os_resolver_lease(0);
	(void)hx_os_network_destroy(&resolver->network, NULL);
	hx_os_memory_free(resolver);
	*owner = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)owner;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
