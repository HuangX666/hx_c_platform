#include "ipc_private.h"
#include "hx_os_net_extra.h"
#include <stdio.h>
#if defined(_WIN32)
#include <mstcpip.h>
#endif

hx_os_status hx_ipc_options_init(hx_ipc_options *options, size_t capacity)
{
	if (!options || capacity < sizeof(*options))
		return HX_OS_INVALID;

	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = 1;
	options->max_peers = 16;
	options->max_messages = 128;
	options->max_pending_requests = 128;
	options->max_message_bytes = 65536;
	options->max_pending_bytes = 4 * 1024 * 1024;
	options->max_kcp_bytes = 8 * 1024 * 1024;
	options->handshake_timeout_ms = 5000;
	options->idle_timeout_ms = 15000;
	options->reconnect_attempts = 8;
	options->reconnect_delay_ms = 250;

	return HX_OS_OK;
}

void hx_ipc_message_release(hx_ipc_message *message)
{
	hx_os_memory_free(message);
}

hx_os_status hx_ipc_listen(
	const char *address, hx_ipc **out, hx_os_error *error)
{
	return hx_ipc_listen_with_options(NULL, address, NULL, out, error);
}

hx_os_status hx_ipc_connect(
	const char *address, hx_ipc **out, hx_os_error *error)
{
	return hx_ipc_connect_until(
		NULL, address, NULL, hx_os_deadline_after_ms(5000), out, error);
}

hx_os_status hx_ipc_send_text(
	hx_ipc *endpoint, const char *text, hx_os_error *error)
{
	return hx_ipc_send_text_until(
		endpoint, 0, 0, text, hx_os_deadline_after_ms(5000), error);
}

hx_os_status hx_ipc_request_text(hx_ipc *endpoint, const char *text,
	hx_ipc_message **reply, hx_os_error *error)
{
	return hx_ipc_request_text_until(endpoint, 0, 0, text,
		hx_os_deadline_after_ms(5000), reply, error);
}

hx_os_status hx_ipc_reply_text(hx_ipc *endpoint, const hx_ipc_message *request,
	const char *text, hx_os_error *error)
{
	return hx_ipc_reply_text_until(
		endpoint, request, text, hx_os_deadline_after_ms(5000), error);
}

#if HX_OS_HAVE_IPC
hx_ipc_connection *hx_ipc_i_find(hx_ipc *endpoint, uint64_t id)
{
	size_t index;

	if (!id && !endpoint->server)
		return endpoint->peers;

	for (index = 0; index < endpoint->options.max_peers; ++index)
		if (endpoint->peers[index].id == id &&
			endpoint->peers[index].state)
			return &endpoint->peers[index];

	return NULL;
}

void hx_ipc_i_complete(hx_ipc *endpoint, uint64_t peer, uint64_t id,
	hx_os_status status, hx_ipc_message *reply)
{
	hx_ipc_waiter *waiter;
	int taken = 0;

	hx_ipc_i_lock(endpoint);
	if (reply)
		endpoint->statistics.pending_bytes -= reply->bytes;

	for (waiter = endpoint->waiters; waiter; waiter = waiter->next) {
		if (waiter->peer != peer || waiter->id != id || waiter->done)
			continue;

		if (status == HX_OS_OK && !reply &&
			waiter->kind == HX_IPC_I_REQUEST)
			break;
		if (reply && waiter->kind != HX_IPC_I_REQUEST)
			break;

		waiter->done = 1;
		waiter->reply = reply;
		(void)hx_os_i_error(&waiter->error, status, 0, 0);
		taken = 1;
		hx_os_condition_notify_all(endpoint->condition);
		break;
	}

	hx_ipc_i_unlock(endpoint);
	if (!taken)
		hx_ipc_message_release(reply);
}

void hx_ipc_i_received_free(
	hx_ipc *endpoint, hx_ipc_received *message, int counted)
{
	if (!message)
		return;

	hx_ipc_i_lock(endpoint);
	endpoint->statistics.pending_bytes -= message->message.bytes;
	endpoint->inbox_count -= (size_t)counted;
	hx_ipc_i_unlock(endpoint);

	hx_os_memory_free(message);
}

void hx_ipc_i_frame_free(hx_ipc *endpoint, hx_ipc_frame *frame)
{
	if (!frame)
		return;

	hx_ipc_i_lock(endpoint);
	if (!frame->control) {
		endpoint->statistics.pending_bytes -= frame->bytes;
		--endpoint->outbox_count;
	}
	else {
		hx_ipc_connection *peer = hx_ipc_i_find(endpoint, frame->peer);
		endpoint->control_bytes -= frame->bytes;
		if (peer && peer->controls) {
			--peer->controls;
			peer->control_bytes -= frame->bytes;
		}
	}

	hx_ipc_i_unlock(endpoint);

	hx_os_memory_free(frame);
}

hx_os_status hx_ipc_i_queue(hx_ipc_connection *peer,
	const hx_ipc_header *header, const void *data, hx_os_deadline deadline,
	int control, hx_os_error *error)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_ipc_frame *frame;
	size_t bytes = HX_IPC_I_HEADER + (size_t)header->bytes;
	hx_os_status status = HX_OS_OK;
	uint64_t identity;

	hx_ipc_i_lock(endpoint);
	identity = peer->id;
	if ((endpoint->closing && !control) || hx_ipc_i_aborting(endpoint) ||
		peer->state >= 4 || !peer->state || header->source != identity)
		status = HX_OS_CLOSED;
	else if (control) {
		if (peer->controls >= endpoint->options.max_pending_requests +
					endpoint->options.max_messages + 8 ||
			bytes > 65536 - peer->control_bytes ||
			bytes > endpoint->options.max_peers * 65536 -
					endpoint->control_bytes)
			status = HX_OS_LIMIT;
		else {
			++peer->controls;
			peer->control_bytes += bytes;
			endpoint->control_bytes += bytes;
		}
	}
	else if (endpoint->outbox_count >= endpoint->options.max_messages ||
		bytes > endpoint->options.max_pending_bytes -
				endpoint->statistics.pending_bytes)
		status = HX_OS_LIMIT;
	else {
		++endpoint->outbox_count;
		endpoint->statistics.pending_bytes += bytes;
	}

	hx_ipc_i_unlock(endpoint);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	frame = hx_os_i_zero(endpoint->context, sizeof(*frame) + bytes);
	if (!frame) {
		hx_ipc_i_lock(endpoint);
		if (control) {
			endpoint->control_bytes -= bytes;
			if (peer->id == identity && peer->controls) {
				--peer->controls;
				peer->control_bytes -= bytes;
			}
		}
		else {
			--endpoint->outbox_count;
			endpoint->statistics.pending_bytes -= bytes;
		}

		hx_ipc_i_unlock(endpoint);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	frame->bytes = bytes;
	frame->control = control;
	frame->kind = header->kind;
	frame->peer = identity;
	frame->id = header->id;
	frame->deadline = deadline;
	hx_ipc_i_encode(frame->wire, header);
	if (header->bytes)
		memcpy(frame->wire + HX_IPC_I_HEADER, data, header->bytes);

	hx_ipc_i_lock(endpoint);
	if ((endpoint->closing && !control) || hx_ipc_i_aborting(endpoint) ||
		peer->id != identity || peer->state >= 4)
		status = HX_OS_CLOSED;
	else {
		if (control) {
			// Control frames can pass queued data, never bytes
			// already handed to the transport. Keep control order
			// stable.
			hx_ipc_frame **cursor = &endpoint->outbox;

			while (*cursor && (*cursor)->control)
				cursor = &(*cursor)->next;

			frame->next = *cursor;
			*cursor = frame;
			if (!frame->next)
				endpoint->outbox_last = frame;
		}
		else {
			if (endpoint->outbox_last)
				endpoint->outbox_last->next = frame;
			else
				endpoint->outbox = frame;

			endpoint->outbox_last = frame;
		}

		if (endpoint->loop)
			hx_os_i_loop_wake(endpoint->loop);
	}

	hx_ipc_i_unlock(endpoint);
	if (status != HX_OS_OK)
		hx_ipc_i_frame_free(endpoint, frame);

	return hx_os_i_error(error, status, 0, 0);
}

void hx_ipc_i_event(hx_ipc_connection *peer, int kind)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_ipc_message *event;

	hx_ipc_i_lock(endpoint);
	if (endpoint->event_count == endpoint->event_capacity) {
		++endpoint->statistics.lost_events;
		endpoint->overflow = 1;
	}
	else {
		size_t index = (endpoint->event_read + endpoint->event_count) %
			endpoint->event_capacity;

		event = &endpoint->events[index];
		memset(event, 0, sizeof(*event));
		event->peer = peer->id;
		event->kind = kind;
		event->error = peer->error;
		++endpoint->event_count;
	}

	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
}

static hx_os_status hx_ipc_enter(hx_ipc *endpoint, hx_os_error *error)
{
	if (!endpoint)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_ipc_i_lock(endpoint);
	if (endpoint->closing) {
		hx_os_error detail = endpoint->error;

		if (!detail.status)
			detail.status = HX_OS_CLOSED;

		hx_ipc_i_unlock(endpoint);
		if (error)
			*error = detail;

		return detail.status;
	}

	++endpoint->users;
	hx_ipc_i_unlock(endpoint);

	return HX_OS_OK;
}

static void hx_ipc_leave(hx_ipc *endpoint)
{
	hx_ipc_i_lock(endpoint);
	--endpoint->users;
	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
}

static hx_os_status hx_ipc_parse_ip(
	hx_ipc *endpoint, const char *address, hx_os_error *error)
{
	char host[64];

	const char *begin = strstr(address, "://") + 3;

	const char *port;
	size_t length;
	unsigned long number = 0;
	hx_os_status status;

	if (endpoint->remote_kcp)
		return hx_os_endpoint_parse(begin, &endpoint->address, error);

	if (*begin == '[') {
		const char *end = strchr(++begin, ']');
		if (!end || end[1] != ':')
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		length = (size_t)(end - begin);
		port = end + 2;
	}
	else {
		port = strchr(begin, ':');
		if (!port)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		length = (size_t)(port++ - begin);
	}

	if (!length || length >= sizeof(host) || !*port)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	memcpy(host, begin, length);
	host[length] = 0;

	while (*port) {
		if (*port < '0' || *port > '9')
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		number = number * 10 + (unsigned long)(*port++ - '0');
		if (number > 65535)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	if (!endpoint->server && !number)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_address_parse(
		host, (uint16_t)number, &endpoint->address, error);
	if (status != HX_OS_OK)
		return status;

	if (strcmp(host, "127.0.0.1") && strcmp(host, "::1"))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);

	return HX_OS_OK;
}

static hx_os_status hx_ipc_prepare(
	hx_ipc *endpoint, const char *address, hx_os_error *error)
{
	hx_os_status status;
	hx_os_loop_options loop;
	hx_os_address local;

	if (!strncmp(address, "tcp://", 6))
		endpoint->transport = HX_IPC_I_TCP;
	else if (!strncmp(address, "udp://", 6))
		endpoint->transport = HX_IPC_I_UDP;
	else if (!strncmp(address, "kcp://", 6))
		endpoint->transport = HX_IPC_I_KCP;
	else
		endpoint->transport = HX_IPC_I_LOCAL;

	if (endpoint->transport == HX_IPC_I_UDP ||
		endpoint->transport == HX_IPC_I_KCP) {
		endpoint->datagram =
			hx_os_i_alloc(endpoint->context, HX_IPC_I_PACKET, 0);
		if (!endpoint->datagram)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	if (endpoint->transport == HX_IPC_I_KCP) {
		endpoint->kcp_message = hx_os_i_alloc(endpoint->context,
			endpoint->options.max_message_bytes + HX_IPC_I_HEADER,
			0);
		if (!endpoint->kcp_message)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	if (endpoint->transport == HX_IPC_I_KCP &&
		(!(hx_os_capabilities() & HX_OS_CAP_KCP) ||
			endpoint->options.max_message_bytes + HX_IPC_I_HEADER >
				(size_t)(1200 - 24) * 127 ||
			endpoint->options.max_kcp_bytes < 2 * 1024 * 1024))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_os_network_create(
		endpoint->context, &endpoint->network, error);
	if (status != HX_OS_OK)
		return status;

	hx_os_loop_options_init(&loop);
	loop.max_requests = 4 * endpoint->options.max_peers + 8;
	loop.max_pending_bytes = endpoint->options.max_pending_bytes +
		HX_IPC_I_PACKET + endpoint->options.max_peers * 4096;
	loop.dns_threads = 0;

	status = hx_os_loop_create(
		endpoint->network, &loop, &endpoint->loop, error);
	if (status != HX_OS_OK)
		return status;

	if (endpoint->transport == HX_IPC_I_LOCAL) {
#if defined(_WIN32)
		if (!(hx_os_capabilities() & HX_OS_CAP_ASYNC_PIPE))
#else
		if (!(hx_os_capabilities() & HX_OS_CAP_LOCAL_SOCKETS) ||
			!(hx_os_capabilities() & HX_OS_CAP_LOOP_FD))
#endif
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		return hx_ipc_i_native_prepare(endpoint, address, error);
	}

	status = hx_ipc_parse_ip(endpoint, address, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_string_assign_cstr(&endpoint->address_text, address, error);
	if (status != HX_OS_OK ||
		(!endpoint->server && endpoint->transport == HX_IPC_I_TCP))
		return status;
	status = hx_os_socket_create(endpoint->network,
		endpoint->address.family,
		endpoint->transport == HX_IPC_I_TCP ? HX_OS_TCP : HX_OS_UDP,
		&endpoint->listener, error);
#if !defined(_WIN32)
	// A restarted TCP listener may reuse its own TIME_WAIT local address.
	// The raw socket API remains opt-in; IPC explicitly selects this
	// policy.
	if (status == HX_OS_OK && endpoint->transport == HX_IPC_I_TCP) {
		int reuse = 1;

		if (setsockopt(endpoint->listener->native, SOL_SOCKET,
			    SO_REUSEADDR, &reuse, sizeof(reuse)))
			status = hx_os_i_errno(error, errno);
	}
#endif
#if defined(_WIN32)
	if (status == HX_OS_OK && endpoint->server) {
		BOOL exclusive = TRUE;

		if (setsockopt(endpoint->listener->native, SOL_SOCKET,
			    SO_EXCLUSIVEADDRUSE, (const char *)&exclusive,
			    sizeof(exclusive)))
			status = hx_os_i_socket_error(error, WSAGetLastError());
	}

	if (status == HX_OS_OK && endpoint->transport != HX_IPC_I_TCP) {
		BOOL enabled = FALSE;
		DWORD completed;

		// A closed UDP peer must not fail every peer on this listener.
		if (WSAIoctl(endpoint->listener->native, SIO_UDP_CONNRESET,
			    &enabled, sizeof(enabled), NULL, 0, &completed,
			    NULL, NULL))
			status = hx_os_i_socket_error(error, WSAGetLastError());
	}
#endif
	local = endpoint->address;
	if (!endpoint->server) {
		local.port = 0;
		if (endpoint->remote_kcp) {
			memset(local.bytes, 0, sizeof(local.bytes));
			local.scope_id = 0;
		}
	}

	if (status == HX_OS_OK)
		status = hx_os_socket_bind(endpoint->listener, &local, error);

	if (status == HX_OS_OK && endpoint->transport == HX_IPC_I_TCP)
		status = hx_os_socket_listen(endpoint->listener,
			(int)endpoint->options.max_peers, error);

	if (status == HX_OS_OK && endpoint->server) {
		char endpoint_text[128];

		status = hx_os_socket_local_address(
			endpoint->listener, &local, error);
		if (status == HX_OS_OK && endpoint->remote_kcp) {
			status = hx_os_endpoint_format(&local, endpoint_text,
				sizeof(endpoint_text), error);
			if (status == HX_OS_OK)
				status = hx_string_format(
					&endpoint->address_text, error,
					"kcp://%s", endpoint_text);
			return status;
		}
		if (status == HX_OS_OK)
			status = hx_string_format(&endpoint->address_text,
				error, "%s://%s:%u",
				endpoint->transport == HX_IPC_I_TCP ? "tcp"
					: endpoint->transport == HX_IPC_I_UDP
					? "udp"
					: "kcp",
				local.family == HX_OS_IPV4 ? "127.0.0.1"
							   : "[::1]",
				(unsigned int)local.port);
	}

	return status;
}

void hx_ipc_i_dispose(hx_ipc *endpoint)
{
	size_t index;
	hx_ipc_frame *frame;
	hx_ipc_received *message;

	if (!endpoint)
		return;

	for (index = 0; endpoint->peers && index < endpoint->options.max_peers;
		++index) {
		hx_ipc_connection *peer = &endpoint->peers[index];

		(void)hx_os_socket_close(&peer->socket, NULL);
		(void)hx_os_pipe_close_until(
			&peer->reader, HX_OS_DEADLINE_NOW, NULL);
		(void)hx_os_pipe_close_until(
			&peer->writer, HX_OS_DEADLINE_NOW, NULL);

		hx_os_kcp_destroy(peer->kcp);
		hx_os_memory_free(peer->incoming);
		hx_os_memory_free(peer->sending);
	}

	(void)hx_os_loop_watch_close_until(
		&endpoint->local_watch, HX_OS_DEADLINE_NOW, NULL);

	(void)hx_os_socket_close(&endpoint->listener, NULL);
	(void)hx_os_pipe_close_until(
		&endpoint->accept_pipe, HX_OS_DEADLINE_NOW, NULL);
	if (endpoint->loop) {
		(void)hx_os_loop_close_until(
			endpoint->loop, HX_OS_DEADLINE_NOW, NULL);

		(void)hx_os_loop_destroy(&endpoint->loop, NULL);
	}

	(void)hx_os_network_destroy(&endpoint->network, NULL);
	hx_ipc_i_native_cleanup(endpoint);

	while ((frame = endpoint->outbox) != NULL) {
		endpoint->outbox = frame->next;

		hx_os_memory_free(frame);
	}

	while ((message = endpoint->inbox) != NULL) {
		endpoint->inbox = message->next;

		hx_os_memory_free(message);
	}

	hx_string_destroy(&endpoint->address_text);
	hx_os_memory_free(endpoint->peers);
	hx_os_memory_free(endpoint->events);
	hx_os_memory_free(endpoint->datagram);
	hx_os_memory_free(endpoint->kcp_message);
	hx_os_memory_free(endpoint->name);
	hx_os_condition_destroy(endpoint->condition);
	hx_os_mutex_destroy(endpoint->mutex);
	hx_os_memory_free(endpoint);
}

static hx_os_status hx_ipc_create(hx_os_context *context, const char *address,
	const hx_ipc_options *input, const hx_os_backoff_options *backoff,
	int server, int remote_kcp, hx_ipc **out, hx_os_error *error)
{
	hx_ipc_options options;
	hx_ipc *endpoint;
	hx_os_status status;
	size_t invalid;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_validate_utf8(address, strlen(address), &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	(void)hx_ipc_options_init(&options, sizeof(options));
	if (input) {
		if (input->size < sizeof(*input) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		options = *input;
	}

	if (!options.max_peers || options.max_peers > 256 ||
		!options.max_messages || options.max_messages > 65536 ||
		!options.max_pending_requests ||
		options.max_pending_requests > 65536 ||
		!options.max_message_bytes ||
		options.max_message_bytes > 16 * 1024 * 1024 ||
		options.max_pending_bytes <
			options.max_message_bytes + HX_IPC_I_HEADER ||
		options.max_pending_bytes >
			SIZE_MAX - HX_IPC_I_PACKET - 256 * 4096 ||
		!options.handshake_timeout_ms || !options.reconnect_delay_ms ||
		options.handshake_timeout_ms > 3600000 ||
		(options.idle_timeout_ms && options.idle_timeout_ms < 100))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!server)
		options.max_peers = 1;

	endpoint = hx_os_i_zero(context, sizeof(*endpoint));
	if (!endpoint)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	endpoint->context = context;
	endpoint->options = options;
	endpoint->server = server;
	endpoint->remote_kcp = remote_kcp;
#if !defined(_WIN32)
	endpoint->lock_descriptor = -1;
#endif
	hx_string_init(&endpoint->address_text, context);
	endpoint->event_capacity = 2 * options.max_peers + 8;
	endpoint->peers = hx_os_i_zero(
		context, options.max_peers * sizeof(*endpoint->peers));
	endpoint->events = hx_os_i_zero(
		context, endpoint->event_capacity * sizeof(*endpoint->events));

	status = hx_os_mutex_create(context, 0, &endpoint->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &endpoint->condition, error);

	if (!endpoint->peers || !endpoint->events)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	if (status == HX_OS_OK)
		status = hx_os_random_bytes(
			&endpoint->serial, sizeof(endpoint->serial), error);
	if (status == HX_OS_OK && backoff) {
		hx_os_backoff_options policy = *backoff;

		if (!policy.seed)
			policy.seed = endpoint->serial;
		status = hx_os_backoff_init(&endpoint->backoff, &policy, error);
		endpoint->use_backoff = 1;
		endpoint->options.reconnect_attempts = policy.max_retries;
	}

	if (status == HX_OS_OK)
		status = hx_ipc_prepare(endpoint, address, error);

	if (status == HX_OS_OK)
		status = hx_os_thread_create(context, hx_ipc_i_worker, endpoint,
			&endpoint->thread, error);

	if (status != HX_OS_OK) {
		hx_ipc_i_dispose(endpoint);

		return status;
	}

	*out = endpoint;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_ipc_listen_with_options(hx_os_context *context,
	const char *address, const hx_ipc_options *options, hx_ipc **out,
	hx_os_error *error)
{
	return hx_ipc_create(context, address, options, NULL, 1, 0, out, error);
}

hx_os_status hx_ipc_connect_until(hx_os_context *context, const char *address,
	const hx_ipc_options *options, hx_os_deadline deadline, hx_ipc **out,
	hx_os_error *error)
{
	hx_ipc_peer peer;
	hx_os_status status = hx_ipc_create(
		context, address, options, NULL, 0, 0, out, error);

	if (status == HX_OS_OK)
		status = hx_ipc_wait_connected_until(
			*out, deadline, &peer, error);

	return status;
}

hx_os_status hx_ipc_wait_connected_until(hx_ipc *endpoint,
	hx_os_deadline deadline, hx_ipc_peer *peer, hx_os_error *error)
{
	hx_os_status status;
	hx_os_error detail = {0};

	if (!peer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*peer = 0;

	status = hx_ipc_enter(endpoint, error);
	if (status != HX_OS_OK)
		return status;

	hx_ipc_i_lock(endpoint);
	if (endpoint->server)
		status = HX_OS_INVALID;

	while (status == HX_OS_OK) {
		hx_ipc_connection *connection = endpoint->peers;

		if (connection->state == 3) {
			*peer = connection->id;
			break;
		}

		if (endpoint->closing || endpoint->finished) {
			status = endpoint->error.status ? endpoint->error.status
							: HX_OS_CLOSED;
			detail = endpoint->error;
			break;
		}

		if (connection->state == 5 && !endpoint->retry_at) {
			status = connection->error.status;
			detail = connection->error;
			break;
		}

		status = hx_os_condition_wait_until(endpoint->condition,
			endpoint->mutex, deadline, &detail);
	}

	hx_ipc_i_unlock(endpoint);
	hx_ipc_leave(endpoint);
	detail.status = status;
	if (error)
		*error = detail;

	return status;
}

hx_os_status hx_ipc_address(
	hx_ipc *endpoint, hx_string *out, hx_os_error *error)
{
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_ipc_enter(endpoint, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_string_copy(out, &endpoint->address_text, error);
	hx_ipc_leave(endpoint);

	return status;
}

static hx_os_status hx_ipc_call(hx_ipc *endpoint, hx_ipc_peer identity,
	uint64_t channel, uint64_t reply_to, int kind, int encoding,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_ipc_message **reply, hx_os_error *error)
{
	hx_ipc_waiter waiter, **cursor;
	hx_ipc_connection *peer;
	hx_ipc_header header = {0};
	hx_os_status status;

	if (reply)
		*reply = NULL;

	if (!data && bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_ipc_enter(endpoint, error);
	if (status != HX_OS_OK)
		return status;

	memset(&waiter, 0, sizeof(waiter));
	hx_ipc_i_lock(endpoint);
	peer = hx_ipc_i_find(endpoint, identity);
	if (!peer || peer->state != 3)
		status = HX_OS_CLOSED;
	else if (bytes > peer->max_message ||
		(endpoint->transport == HX_IPC_I_UDP &&
			bytes > 65507 - HX_IPC_I_HEADER) ||
		endpoint->wait_count >= endpoint->options.max_pending_requests)
		status = HX_OS_LIMIT;
	else if (endpoint->serial == UINT64_MAX)
		status = HX_OS_RANGE;
	else {
		waiter.id = ++endpoint->serial;
		waiter.peer = peer->id;
		waiter.kind = kind;
		waiter.next = endpoint->waiters;
		endpoint->waiters = &waiter;
		++endpoint->wait_count;
		header.source = peer->id;
		header.destination = peer->remote;
		header.id = waiter.id;
		header.channel = channel;
		header.auxiliary = reply_to;
		header.kind = kind;
		header.encoding = encoding;
		header.bytes = (uint32_t)bytes;
	}

	hx_ipc_i_unlock(endpoint);
	if (status != HX_OS_OK) {
		hx_ipc_leave(endpoint);

		return hx_os_i_error(error, status, 0, 0);
	}

	status = hx_ipc_i_queue(peer, &header, data, deadline, 0, error);
	hx_ipc_i_lock(endpoint);

	while (status == HX_OS_OK && !waiter.done && !endpoint->closing)
		status = hx_os_condition_wait_until(
			endpoint->condition, endpoint->mutex, deadline, error);

	if (waiter.done) {
		status = waiter.error.status;
		if (error)
			*error = waiter.error;

		if (reply)
			*reply = waiter.reply;
	}
	else if (endpoint->closing)
		status = HX_OS_CANCELLED;

	for (cursor = &endpoint->waiters; *cursor; cursor = &(*cursor)->next)
		if (*cursor == &waiter) {
			*cursor = waiter.next;
			break;
		}

	--endpoint->wait_count;
	hx_ipc_i_unlock(endpoint);
	if (!reply)
		hx_ipc_message_release(waiter.reply);

	hx_ipc_leave(endpoint);
	if (error)
		error->status = status;

	return status;
}

hx_os_status hx_ipc_send_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_ipc_call(endpoint, peer, channel, 0, HX_IPC_I_DATA,
		HX_IPC_BINARY, data, bytes, deadline, NULL, error);
}

hx_os_status hx_ipc_send_text_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const char *text, hx_os_deadline deadline,
	hx_os_error *error)
{
	size_t bytes, offset;
	hx_os_status status;

	if (!text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	bytes = strlen(text);

	status = hx_os_text_validate_utf8(text, bytes, &offset);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	return hx_ipc_call(endpoint, peer, channel, 0, HX_IPC_I_DATA,
		HX_IPC_UTF8, text, bytes, deadline, NULL, error);
}

hx_os_status hx_ipc_request_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_ipc_message **reply, hx_os_error *error)
{
	if (!reply)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ipc_call(endpoint, peer, channel, 0, HX_IPC_I_REQUEST,
		HX_IPC_BINARY, data, bytes, deadline, reply, error);
}

hx_os_status hx_ipc_reply_until(hx_ipc *endpoint, const hx_ipc_message *request,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_error *error)
{
	if (!request || request->kind != HX_IPC_REQUEST)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_ipc_call(endpoint, request->peer, request->channel,
		request->request_id, HX_IPC_I_REPLY, HX_IPC_BINARY, data, bytes,
		deadline, NULL, error);
}

hx_os_status hx_ipc_request_text_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const char *text, hx_os_deadline deadline,
	hx_ipc_message **reply, hx_os_error *error)
{
	size_t bytes, offset;
	hx_os_status status;

	if (reply)
		*reply = NULL;

	if (!reply || !text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	bytes = strlen(text);

	status = hx_os_text_validate_utf8(text, bytes, &offset);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	return hx_ipc_call(endpoint, peer, channel, 0, HX_IPC_I_REQUEST,
		HX_IPC_UTF8, text, bytes, deadline, reply, error);
}

hx_os_status hx_ipc_reply_text_until(hx_ipc *endpoint,
	const hx_ipc_message *request, const char *text,
	hx_os_deadline deadline, hx_os_error *error)
{
	size_t bytes, offset;
	hx_os_status status;

	if (!request || request->kind != HX_IPC_REQUEST || !text)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	bytes = strlen(text);

	status = hx_os_text_validate_utf8(text, bytes, &offset);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	return hx_ipc_call(endpoint, request->peer, request->channel,
		request->request_id, HX_IPC_I_REPLY, HX_IPC_UTF8, text, bytes,
		deadline, NULL, error);
}

hx_os_status hx_ipc_receive_until(hx_ipc *endpoint, hx_os_deadline deadline,
	hx_ipc_message **out, hx_os_error *error)
{
	hx_os_status status;
	hx_os_error detail = {0};
	hx_ipc_received *message = NULL;
	hx_ipc_received *event = NULL;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;

	status = hx_ipc_enter(endpoint, error);
	if (status != HX_OS_OK)
		return status;

	hx_ipc_i_lock(endpoint);

	for (;;) {
		while (!endpoint->inbox && !endpoint->event_count &&
			!endpoint->overflow && !endpoint->closing &&
			!endpoint->finished && status == HX_OS_OK)
			status = hx_os_condition_wait_until(endpoint->condition,
				endpoint->mutex, deadline, &detail);

		if (endpoint->closing)
			status = endpoint->error.status ? endpoint->error.status
							: HX_OS_CLOSED;
		else if (endpoint->overflow || endpoint->event_count) {
			if (!event) {
				// enter() retains the endpoint while
				// allocating. Recheck queues and close after
				// user code runs.
				hx_ipc_i_unlock(endpoint);
				event = hx_os_i_zero(
					endpoint->context, sizeof(*event));
				hx_ipc_i_lock(endpoint);
				if (!event && !endpoint->closing) {
					status = HX_OS_NO_MEMORY;
					break;
				}

				continue;
			}

			message = event;
			event = NULL;
			if (endpoint->overflow) {
				message->message.kind = HX_IPC_OVERFLOW;
				message->message.error.status = HX_OS_LIMIT;
				endpoint->overflow = 0;
			}
			else {
				message->message =
					endpoint->events[endpoint->event_read];
				endpoint->event_read =
					(endpoint->event_read + 1) %
					endpoint->event_capacity;
				--endpoint->event_count;
			}
		}
		else if (endpoint->inbox) {
			message = endpoint->inbox;
			endpoint->inbox = message->next;
			if (!endpoint->inbox)
				endpoint->inbox_last = NULL;

			endpoint->statistics.pending_bytes -=
				message->message.bytes;
			--endpoint->inbox_count;
		}
		else if (endpoint->finished)
			status = endpoint->error.status ? endpoint->error.status
							: HX_OS_CLOSED;

		break;
	}

	if (message)
		status = HX_OS_OK;

	if (message)
		*out = &message->message;

	if (status != HX_OS_OK && status == endpoint->error.status)
		detail = endpoint->error;

	hx_ipc_i_unlock(endpoint);
	if (event)
		hx_os_memory_free(event);
	hx_ipc_leave(endpoint);
	detail.status = status;
	if (error)
		*error = detail;

	return status;
}

hx_os_status hx_ipc_statistics_read(hx_ipc *endpoint, hx_ipc_statistics *out,
	size_t capacity, hx_os_error *error)
{
	hx_os_status status;

	if (!out || capacity < sizeof(*out))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_ipc_enter(endpoint, error);
	if (status != HX_OS_OK)
		return status;

	hx_ipc_i_lock(endpoint);
	*out = endpoint->statistics;
	hx_ipc_i_unlock(endpoint);
	hx_ipc_leave(endpoint);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_ipc_close_until(
	hx_ipc **owner, hx_os_deadline deadline, hx_os_error *error)
{
	hx_ipc *endpoint;
	hx_os_status status = HX_OS_OK;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	endpoint = *owner;
	if (!endpoint)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	hx_ipc_i_lock(endpoint);
	if (endpoint->close_running) {
		hx_ipc_i_unlock(endpoint);

		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}

	endpoint->close_running = 1;
	endpoint->closing = 1;
	endpoint->shutdown_deadline = deadline;
	if (hx_os_deadline_expired(deadline))
		hx_os_atomic_store(&endpoint->stop, 2, HX_OS_RELEASE);
	else if (!hx_ipc_i_stopping(endpoint))
		hx_os_atomic_store(&endpoint->stop, 1, HX_OS_RELEASE);

	hx_os_condition_notify_all(endpoint->condition);
	if (endpoint->loop)
		hx_os_i_loop_wake(endpoint->loop);

	while (endpoint->users && status == HX_OS_OK)
		status = hx_os_condition_wait_until(
			endpoint->condition, endpoint->mutex, deadline, error);

	hx_ipc_i_unlock(endpoint);
	if (status == HX_OS_OK)
		status = hx_os_thread_join_until(
			endpoint->thread, deadline, NULL, error);

	if (status != HX_OS_OK) {
		hx_ipc_i_lock(endpoint);
		if (status == HX_OS_TIMEOUT) {
			hx_os_atomic_store(&endpoint->stop, 2, HX_OS_RELEASE);
			if (endpoint->loop)
				hx_os_i_loop_wake(endpoint->loop);
		}

		endpoint->close_running = 0;
		hx_ipc_i_unlock(endpoint);

		return status;
	}

	status = hx_os_thread_destroy(&endpoint->thread, error);
	if (status != HX_OS_OK)
		return status;

	hx_ipc_i_dispose(endpoint);
	*owner = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#else
hx_os_status hx_ipc_listen_with_options(hx_os_context *context,
	const char *address, const hx_ipc_options *options, hx_ipc **out,
	hx_os_error *error)
{
	(void)context;
	(void)address;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_ipc_connect_until(hx_os_context *context, const char *address,
	const hx_ipc_options *options, hx_os_deadline deadline, hx_ipc **out,
	hx_os_error *error)
{
	(void)deadline;

	return hx_ipc_listen_with_options(
		context, address, options, out, error);
}

hx_os_status hx_ipc_wait_connected_until(hx_ipc *endpoint,
	hx_os_deadline deadline, hx_ipc_peer *peer, hx_os_error *error)
{
	(void)endpoint;
	(void)deadline;
	if (peer)
		*peer = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_ipc_address(
	hx_ipc *endpoint, hx_string *out, hx_os_error *error)
{
	(void)endpoint;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_ipc_send_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)endpoint;
	(void)peer;
	(void)channel;
	(void)data;
	(void)bytes;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_ipc_send_text_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const char *text, hx_os_deadline deadline,
	hx_os_error *error)
{
	return hx_ipc_send_until(
		endpoint, peer, channel, text, 0, deadline, error);
}

hx_os_status hx_ipc_request_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_ipc_message **reply, hx_os_error *error)
{
	if (reply)
		*reply = NULL;

	return hx_ipc_send_until(
		endpoint, peer, channel, data, bytes, deadline, error);
}

hx_os_status hx_ipc_request_text_until(hx_ipc *endpoint, hx_ipc_peer peer,
	uint64_t channel, const char *text, hx_os_deadline deadline,
	hx_ipc_message **reply, hx_os_error *error)
{
	return hx_ipc_request_until(
		endpoint, peer, channel, text, 0, deadline, reply, error);
}

hx_os_status hx_ipc_reply_text_until(hx_ipc *endpoint,
	const hx_ipc_message *request, const char *text,
	hx_os_deadline deadline, hx_os_error *error)
{
	(void)request;

	return hx_ipc_send_until(endpoint, 0, 0, text, 0, deadline, error);
}

hx_os_status hx_ipc_reply_until(hx_ipc *endpoint, const hx_ipc_message *request,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_error *error)
{
	(void)request;

	return hx_ipc_send_until(endpoint, 0, 0, data, bytes, deadline, error);
}

hx_os_status hx_ipc_receive_until(hx_ipc *endpoint, hx_os_deadline deadline,
	hx_ipc_message **message, hx_os_error *error)
{
	if (message)
		*message = NULL;

	return hx_ipc_send_until(endpoint, 0, 0, NULL, 0, deadline, error);
}

hx_os_status hx_ipc_statistics_read(hx_ipc *endpoint, hx_ipc_statistics *out,
	size_t capacity, hx_os_error *error)
{
	(void)endpoint;
	(void)out;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_ipc_close_until(
	hx_ipc **owner, hx_os_deadline deadline, hx_os_error *error)
{
	(void)owner;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif

hx_os_status hx_ipc_connect_backoff_until(hx_os_context *context,
	const char *address, const hx_ipc_options *options,
	const hx_os_backoff_options *backoff, hx_os_deadline deadline,
	hx_ipc **out, hx_os_error *error)
{
#if HX_OS_HAVE_IPC
	hx_os_backoff_options defaults = HX_OS_BACKOFF_OPTIONS_INIT;
	hx_os_status status;
	hx_ipc_peer peer;

	defaults.seed = 0;
	status = hx_ipc_create(context, address, options,
		backoff ? backoff : &defaults, 0, 0, out, error);
	if (status == HX_OS_OK)
		status = hx_ipc_wait_connected_until(
			*out, deadline, &peer, error);
	return status;
#else
	(void)context;
	(void)address;
	(void)options;
	(void)backoff;
	(void)deadline;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_ipc_i_open_kcp(hx_os_context *context, const char *address,
	const hx_ipc_options *options, int server, hx_os_deadline deadline,
	hx_ipc **out, hx_os_error *error)
{
#if HX_OS_HAVE_IPC
	hx_string uri = HX_STRING_INIT;
	hx_os_status status;
	hx_os_address numeric;
	hx_ipc_peer peer;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (address && !strncmp(address, "kcp://", 6))
		address += 6;
	status = hx_os_endpoint_parse(address, &numeric, error);
	if (status != HX_OS_OK)
		return status;
	if (!server && !numeric.port)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_string_format(&uri, error, "kcp://%s", address);
	if (status == HX_OS_OK)
		status = hx_ipc_create(context, hx_string_c_str(&uri), options,
			NULL, server, 1, out, error);
	hx_string_destroy(&uri);
	if (status == HX_OS_OK && !server)
		status = hx_ipc_wait_connected_until(
			*out, deadline, &peer, error);
	return status;
#else
	(void)context;
	(void)address;
	(void)options;
	(void)server;
	(void)deadline;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
