#include "dial_private.h"
#include "hx_os_net_extra.h"

hx_os_status hx_os_tcp_options_init(hx_os_tcp_options *options, size_t capacity)
{
	const hx_os_tcp_options defaults = HX_OS_TCP_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_dial_options_init(
	hx_os_dial_options *options, size_t capacity)
{
	const hx_os_dial_options defaults = HX_OS_DIAL_OPTIONS_INIT;

	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;
	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_i_tcp_options_validate(
	const hx_os_tcp_options *options, hx_os_error *error)
{
	if (!options || options->size < sizeof(*options) ||
		options->version != 1 || options->nodelay < -1 ||
		options->nodelay > 1 || options->keepalive < -1 ||
		options->keepalive > 1 || options->dual_stack < -1 ||
		options->dual_stack > 1 ||
		(options->keepalive_idle_seconds == 0) !=
			(options->keepalive_interval_seconds == 0) ||
		(options->keepalive_probe_count &&
			!options->keepalive_idle_seconds) ||
		(options->keepalive_idle_seconds && options->keepalive != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (options->send_buffer_bytes > INT_MAX ||
		options->receive_buffer_bytes > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_apply_tcp_options(hx_os_socket *socket,
	const hx_os_tcp_options *options, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	hx_os_status status = hx_os_i_tcp_options_validate(options, error);

	if (status != HX_OS_OK)
		return status;
	if (!socket || socket->protocol != HX_OS_TCP)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (socket->loop)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (options->nodelay != -1)
		status = hx_os_socket_set_nodelay(
			socket, options->nodelay, error);
	if (status == HX_OS_OK && options->keepalive != -1) {
		if (options->keepalive_idle_seconds)
			status = hx_os_socket_configure_keepalive(socket,
				options->keepalive_idle_seconds,
				options->keepalive_interval_seconds,
				options->keepalive_probe_count, error);
		else
			status = hx_os_socket_set_keepalive(
				socket, options->keepalive, error);
	}
	if (status == HX_OS_OK && options->dual_stack != -1 &&
		socket->family == HX_OS_IPV6)
		status = hx_os_socket_set_dual_stack(
			socket, options->dual_stack, error);
	if (status == HX_OS_OK &&
		(options->send_buffer_bytes || options->receive_buffer_bytes))
		status = hx_os_socket_set_buffers(socket,
			options->send_buffer_bytes,
			options->receive_buffer_bytes, error);

	return status;
#else
	(void)socket;
	(void)options;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

#if HX_OS_ENABLE_NETWORK
static hx_os_status hx_os_dial_wait(hx_os_socket *const sockets[2],
	hx_os_deadline deadline, int ready[2], hx_os_error *error)
{
	int result;
	size_t i;
#if defined(_WIN32)
	fd_set write_set, error_set;
	struct timeval interval;
	uint32_t ms = hx_os_i_wait_ms(deadline);

	FD_ZERO(&write_set);
	FD_ZERO(&error_set);
	for (i = 0; i < 2; ++i)
		if (sockets[i]) {
			FD_SET(sockets[i]->native, &write_set);
			FD_SET(sockets[i]->native, &error_set);
		}
	interval.tv_sec = (long)(ms / 1000);
	interval.tv_usec = (long)(ms % 1000) * 1000;
	result = select(0, NULL, &write_set, &error_set, &interval);
	if (result == SOCKET_ERROR)
		return hx_os_i_socket_error(error, WSAGetLastError());
	for (i = 0; i < 2; ++i)
		ready[i] = sockets[i] &&
			(FD_ISSET(sockets[i]->native, &write_set) ||
				FD_ISSET(sockets[i]->native, &error_set));
#else
	struct pollfd descriptors[2];

	for (i = 0; i < 2; ++i) {
		descriptors[i].fd = sockets[i] ? sockets[i]->native : -1;
		descriptors[i].events = POLLOUT;
		descriptors[i].revents = 0;
	}
	result = poll(descriptors, 2, (int)hx_os_i_wait_ms(deadline));
	if (result < 0) {
		if (errno == EINTR)
			return HX_OS_OK;
		return hx_os_i_errno(error, errno);
	}
	for (i = 0; i < 2; ++i)
		ready[i] = descriptors[i].revents != 0;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

// Reserve time for later candidates instead of letting two black holes
// consume the entire deadline. The last candidates keep the full remainder.
static hx_os_deadline hx_os_dial_window(
	hx_os_deadline deadline, size_t remaining)
{
	uint64_t now = hx_os_monotonic_ns();
	uint64_t span = UINT64_C(2000000000);

	if (deadline != HX_OS_DEADLINE_INFINITE) {
		uint64_t share = deadline > now
			? (deadline - now) / ((remaining + 1) / 2)
			: 0;

		if (share < span)
			span = share;
	}
	return span < UINT64_MAX - now ? now + span : UINT64_MAX - 1;
}

static void hx_os_dial_order(
	const hx_os_address *addresses, size_t count, size_t order[256])
{
	size_t cursor[2] = {0, 0}, used;
	int first = addresses[0].family;
	int turn = 0;

	for (used = 0; used < count; ++used) {
		size_t i;
		int pass;

		for (pass = 0; pass < 2; ++pass) {
			for (i = cursor[turn]; i < count; ++i)
				if ((addresses[i].family != first) == turn)
					break;
			cursor[turn] = i + 1;
			if (i < count) {
				order[used] = i;
				turn ^= 1;
				break;
			}
			turn ^= 1;
		}
	}
}
#endif

hx_os_status hx_os_i_tcp_dial_addresses_until(hx_os_network *network,
	const hx_os_address *addresses, size_t count,
	const hx_os_dial_options *input, hx_os_deadline deadline,
	int (*cancelled)(void *), void *user, hx_os_socket **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	const hx_os_dial_options defaults = HX_OS_DIAL_OPTIONS_INIT;
	const hx_os_dial_options *options = input ? input : &defaults;
	hx_os_socket *sockets[2] = {NULL, NULL};
	hx_os_status status;
	hx_os_error last;
	hx_os_deadline next_start = HX_OS_DEADLINE_NOW;
	hx_os_deadline expires[2] = {0, 0};
	size_t order[256], next = 0, i;
	int winner = -1;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!network || !addresses || !count || count > 256 ||
		options->size < sizeof(*options) || options->version != 1 ||
		options->attempt_delay_ms > 60000)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_tcp_options_validate(&options->socket, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; i < count; ++i)
		if ((addresses[i].family != HX_OS_IPV4 &&
			    addresses[i].family != HX_OS_IPV6) ||
			!addresses[i].port)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_dial_order(addresses, count, order);
	(void)hx_os_i_error(&last, HX_OS_NOT_FOUND, 0, 0);
	for (;;) {
		hx_os_deadline wait = deadline;
		int ready[2] = {0, 0};

		if (hx_os_cancel_requested(options->cancel) ||
			(cancelled && cancelled(user))) {
			status = hx_os_i_error(&last, HX_OS_CANCELLED, 0, 0);
			break;
		}
		if (hx_os_deadline_expired(deadline)) {
			status = hx_os_i_error(&last, HX_OS_TIMEOUT, 0, 0);
			break;
		}
		for (i = 0; i < 2 && next < count; ++i) {
			const hx_os_address *address;

			if (sockets[i] || !hx_os_deadline_expired(next_start))
				continue;
			address = addresses + order[next++];
			status = hx_os_socket_create(network, address->family,
				HX_OS_TCP, &sockets[i], &last);
			if (status == HX_OS_OK) {
				status = hx_os_socket_apply_tcp_options(
					sockets[i], &options->socket, &last);
				if (status != HX_OS_OK)
					goto finished;
				status = hx_os_socket_connect(
					sockets[i], address, &last);
			}
			if (status == HX_OS_OK) {
				winner = (int)i;
				goto finished;
			}
			if (status != HX_OS_WOULD_BLOCK) {
				hx_os_i_socket_discard(&sockets[i]);
				next_start = HX_OS_DEADLINE_NOW;
			}
			else {
				expires[i] = hx_os_dial_window(deadline,
					count - next + 1 +
						(sockets[1 - i] != NULL));
				next_start = hx_os_deadline_after_ms(
					options->attempt_delay_ms);
				if (next < count && expires[i] < next_start)
					next_start = expires[i];
			}
		}
		if (!sockets[0] && !sockets[1]) {
			if (next == count) {
				status = last.status;
				break;
			}
			continue;
		}
		if (next < count && (!sockets[0] || !sockets[1]) &&
			next_start < wait)
			wait = next_start;
		if (next < count)
			for (i = 0; i < 2; ++i)
				if (sockets[i] && expires[i] < wait)
					wait = expires[i];
		if (options->cancel || cancelled) {
			hx_os_deadline check = hx_os_deadline_after_ms(50);

			if (check < wait)
				wait = check;
		}
		status = hx_os_dial_wait(sockets, wait, ready, &last);
		if (status != HX_OS_OK)
			break;
		for (i = 0; i < 2; ++i) {
			if (!sockets[i] || !ready[i])
				continue;
			status = hx_os_i_socket_connected(sockets[i], &last);
			if (status == HX_OS_OK) {
				winner = (int)i;
				goto finished;
			}
			if (status != HX_OS_WOULD_BLOCK) {
				hx_os_i_socket_discard(&sockets[i]);
				next_start = HX_OS_DEADLINE_NOW;
			}
		}
		if (next < count)
			for (i = 0; i < 2; ++i)
				if (sockets[i] &&
					hx_os_deadline_expired(expires[i])) {
					hx_os_i_socket_discard(&sockets[i]);
					next_start = HX_OS_DEADLINE_NOW;
				}
	}
finished:
	if (winner >= 0) {
		*out = sockets[winner];
		sockets[winner] = NULL;
		status = hx_os_i_error(&last, HX_OS_OK, 0, 0);
	}
	for (i = 0; i < 2; ++i)
		hx_os_i_socket_discard(&sockets[i]);
	if (error)
		*error = last;

	return status;
#else
	(void)network;
	(void)addresses;
	(void)count;
	(void)input;
	(void)deadline;
	(void)cancelled;
	(void)user;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_tcp_dial_addresses_until(hx_os_network *network,
	const hx_os_address *addresses, size_t count,
	const hx_os_dial_options *options, hx_os_deadline deadline,
	hx_os_socket **out, hx_os_error *error)
{
	return hx_os_i_tcp_dial_addresses_until(network, addresses, count,
		options, deadline, NULL, NULL, out, error);
}

hx_os_status hx_os_tcp_dial_until(hx_os_network *network, const char *host,
	uint16_t port, const hx_os_dial_options *options,
	hx_os_deadline deadline, hx_os_socket **out, hx_os_error *error)
{
	hx_os_address_list *list = NULL;
	hx_os_address addresses[256];
	hx_os_status status;
	size_t i, count;

	if (!out || !port)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (options &&
		(options->size < sizeof(*options) || options->version != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (options && hx_os_cancel_requested(options->cancel))
		return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
	if (hx_os_deadline_expired(deadline))
		return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	status = options && options->resolver
		? hx_os_resolver_resolve_until(options->resolver, host, port,
			  HX_OS_TCP, deadline, options->cancel, &list, error)
		: hx_os_resolve(network, host, port, HX_OS_TCP, &list, error);
	if (status != HX_OS_OK)
		return status;
	count = hx_os_address_list_count(list);
	for (i = 0; i < count && i < 256; ++i)
		addresses[i] = *hx_os_address_list_get(list, i);
	status = hx_os_tcp_dial_addresses_until(
		network, addresses, count, options, deadline, out, error);
	hx_os_address_list_destroy(list);

	return status;
}
