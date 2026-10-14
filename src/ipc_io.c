#include "ipc_private.h"

static void hx_ipc_read_start(hx_ipc_connection *peer);

static void hx_ipc_accept_start(hx_ipc *endpoint);

static void hx_ipc_datagram_start(hx_ipc *endpoint);

void hx_ipc_i_failed(hx_ipc *endpoint, const hx_os_error *error)
{
	hx_ipc_i_lock(endpoint);
	if (!endpoint->error.status)
		endpoint->error = *error;

	endpoint->closing = 1;
	hx_os_atomic_store(&endpoint->stop, 2, HX_OS_RELEASE);
	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
}

void hx_ipc_i_peer_failed(
	hx_ipc_connection *peer, hx_os_status status, const hx_os_error *error)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_ipc_waiter *waiter;

	if (peer->state >= 4 || !peer->state)
		return;

	hx_ipc_i_lock(endpoint);
	peer->state = 4;
	if (error)
		peer->error = *error;

	peer->error.status = status;

	for (waiter = endpoint->waiters; waiter; waiter = waiter->next)
		if (waiter->peer == peer->id && !waiter->done) {
			waiter->done = 1;
			waiter->error = peer->error;
		}

	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
	if (peer->reading)
		hx_os_request_cancel(peer->reading);

	if (peer->writing)
		hx_os_request_cancel(peer->writing);

	if (peer->connecting)
		hx_os_request_cancel(peer->connecting);

	if (peer->announced)
		hx_ipc_i_event(peer, HX_IPC_DISCONNECTED);
}

hx_ipc_connection *hx_ipc_i_new_peer(
	hx_ipc *endpoint, const hx_os_address *address)
{
	size_t index;
	uint64_t id;
	hx_os_error error;
	hx_ipc_connection *peer = NULL;

	if (hx_os_random_bytes(&id, sizeof(id), &error) != HX_OS_OK || !id) {
		if (!error.status)
			error.status = HX_OS_IO_ERROR;

		hx_ipc_i_failed(endpoint, &error);

		return NULL;
	}

	hx_ipc_i_lock(endpoint);

	for (index = 0; index < endpoint->options.max_peers; ++index)
		if (!endpoint->peers[index].state ||
			(!endpoint->server &&
				endpoint->peers[index].state == 5)) {
			peer = &endpoint->peers[index];
			memset(peer, 0, sizeof(*peer));
			peer->endpoint = endpoint;
			peer->id = id;
			peer->state = endpoint->server ? 2 : 1;
			peer->max_message = endpoint->options.max_message_bytes;
			peer->handshake_at = hx_os_deadline_after_ms(
				endpoint->options.handshake_timeout_ms);
			peer->received_at = hx_os_monotonic_ns();
			if (address)
				peer->address = *address;

			++endpoint->statistics.peers;
			break;
		}

	hx_ipc_i_unlock(endpoint);

	return peer;
}

void hx_ipc_i_accepted(hx_ipc *endpoint, hx_os_socket *socket,
	hx_os_pipe *reader, hx_os_pipe *writer)
{
	hx_ipc_connection *peer = hx_ipc_i_new_peer(endpoint, NULL);
	if (!peer || hx_ipc_i_stopping(endpoint)) {
		(void)hx_os_socket_close(&socket, NULL);
		(void)hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL);
		(void)hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_NOW, NULL);

		return;
	}

	peer->socket = socket;
	peer->reader = reader;
	peer->writer = writer;
	if (socket && socket->family != HX_OS_LOCAL)
		(void)hx_os_socket_set_nodelay(socket, 1, NULL);

	hx_ipc_read_start(peer);
}

void HX_OS_CALL hx_ipc_i_pipe_accepted(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc *endpoint = user;
	hx_os_pipe *reader, *writer = NULL;
	hx_os_error error;
	hx_os_status status;

	endpoint->accepting = NULL;

	hx_os_request_release(request);
	if (hx_ipc_i_stopping(endpoint))
		return;

	if (completion->error.status != HX_OS_OK) {
		hx_ipc_i_failed(endpoint, &completion->error);

		return;
	}

	reader = endpoint->accept_pipe;
	endpoint->accept_pipe = NULL;

	// Establish the next instance before giving up this instance's name.
	status = hx_ipc_i_native_accept(endpoint, 0, &error);
	if (status == HX_OS_OK)
		status = hx_ipc_i_pipe_write_end(
			endpoint, reader, &writer, &error);

	if (status != HX_OS_OK) {
		(void)hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL);
		hx_ipc_i_failed(endpoint, &error);

		return;
	}

	hx_ipc_i_accepted(endpoint, NULL, reader, writer);
}

static void hx_ipc_accepted(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc *endpoint = user;

	hx_os_socket *socket = hx_os_request_take_socket(request);
	endpoint->accepting = NULL;

	hx_os_request_release(request);
	if (hx_ipc_i_stopping(endpoint)) {
		(void)hx_os_socket_close(&socket, NULL);

		return;
	}

	if (completion->error.status != HX_OS_OK) {
		hx_ipc_i_failed(endpoint, &completion->error);

		return;
	}

	hx_ipc_i_accepted(endpoint, socket, NULL, NULL);
	hx_ipc_accept_start(endpoint);
}

static void hx_ipc_accept_start(hx_ipc *endpoint)
{
	hx_os_operation operation;
	hx_os_error error;
	hx_os_status status;

	if (endpoint->transport == HX_IPC_I_LOCAL)
		status = hx_ipc_i_native_accept(endpoint, 1, &error);
	else {
		hx_os_operation_init(&operation, HX_OS_OP_ACCEPT);
		operation.socket = endpoint->listener;

		status = hx_os_loop_submit(endpoint->loop, &operation,
			hx_ipc_accepted, endpoint, &endpoint->accepting,
			&error);
	}

	if (status != HX_OS_OK)
		hx_ipc_i_failed(endpoint, &error);
}

static void hx_ipc_read(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc_connection *peer = user;

	peer->reading = NULL;

	hx_os_request_release(request);
	if (peer->state >= 4 || hx_ipc_i_aborting(peer->endpoint))
		return;

	if (completion->error.status != HX_OS_OK) {
		hx_os_status status = completion->error.status;

		if (status == HX_OS_EOF)
			status = peer->header_bytes ? HX_OS_TRUNCATED_INPUT
						    : HX_OS_CLOSED;

		hx_ipc_i_peer_failed(peer, status, &completion->error);

		return;
	}

	hx_ipc_i_stream_input(peer, peer->input, completion->bytes);
	if (peer->state < 4)
		hx_ipc_read_start(peer);
}

static void hx_ipc_read_start(hx_ipc_connection *peer)
{
	hx_os_operation operation;
	hx_os_error error;
	hx_os_status status;

	if (peer->reader)
		status = hx_os_pipe_read_async(peer->endpoint->loop,
			peer->reader, peer->input, sizeof(peer->input),
			HX_OS_DEADLINE_INFINITE, hx_ipc_read, peer,
			&peer->reading, &error);
	else {
		hx_os_operation_init(&operation, HX_OS_OP_READ);
		operation.socket = peer->socket;
		operation.data = peer->input;
		operation.bytes = sizeof(peer->input);

		status = hx_os_loop_submit(peer->endpoint->loop, &operation,
			hx_ipc_read, peer, &peer->reading, &error);
	}

	if (status != HX_OS_OK)
		hx_ipc_i_peer_failed(peer, status, &error);
}

static void hx_ipc_datagram_read(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc *endpoint = user;

	endpoint->datagram_read = NULL;

	hx_os_request_release(request);
	if (hx_ipc_i_aborting(endpoint))
		return;

	if (completion->error.status == HX_OS_OK)
		hx_ipc_i_datagram(endpoint, &completion->peer,
			endpoint->datagram, completion->bytes);
	else if (completion->error.status == HX_OS_LIMIT) {
		hx_ipc_i_lock(endpoint);
		++endpoint->statistics.discarded_datagrams;
		hx_ipc_i_unlock(endpoint);
	}
	else {
		hx_ipc_i_failed(endpoint, &completion->error);

		return;
	}

	hx_ipc_datagram_start(endpoint);
}

static void hx_ipc_datagram_start(hx_ipc *endpoint)
{
	hx_os_operation operation;
	hx_os_error error;

	hx_os_operation_init(&operation, HX_OS_OP_RECEIVE_FROM);
	operation.socket = endpoint->listener;
	operation.data = endpoint->datagram;
	operation.bytes = HX_IPC_I_PACKET;
	if (hx_os_loop_submit(endpoint->loop, &operation, hx_ipc_datagram_read,
		    endpoint, &endpoint->datagram_read, &error) != HX_OS_OK)
		hx_ipc_i_failed(endpoint, &error);
}

static void hx_ipc_connected(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc_connection *peer = user;

	peer->connecting = NULL;

	hx_os_request_release(request);
	if (peer->state >= 4 || hx_ipc_i_stopping(peer->endpoint))
		return;

	if (completion->error.status != HX_OS_OK) {
		hx_ipc_i_peer_failed(
			peer, completion->error.status, &completion->error);

		return;
	}

	hx_ipc_i_lock(peer->endpoint);
	peer->state = 2;
	hx_ipc_i_unlock(peer->endpoint);
	hx_ipc_read_start(peer);
	hx_ipc_i_control(peer, HX_IPC_I_HELLO, 0,
		peer->endpoint->options.max_message_bytes, HX_OS_OK);
}

static void hx_ipc_connect_start(hx_ipc_connection *peer)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_os_error error;
	hx_os_status status;
	hx_os_operation operation;

	if (endpoint->transport == HX_IPC_I_LOCAL) {
		status = hx_ipc_i_native_connect(peer, &error);
		if (status == HX_OS_WOULD_BLOCK) {
			hx_ipc_i_lock(endpoint);
			peer->error = error;
			hx_ipc_i_unlock(endpoint);

			return;
		}

		if (status == HX_OS_OK) {
			hx_ipc_i_lock(endpoint);
			peer->state = 2;
			hx_ipc_i_unlock(endpoint);
			hx_ipc_read_start(peer);
			hx_ipc_i_control(peer, HX_IPC_I_HELLO, 0,
				endpoint->options.max_message_bytes, HX_OS_OK);
		}
	}
	else if (endpoint->transport == HX_IPC_I_TCP) {
		status = hx_os_socket_create(endpoint->network,
			endpoint->address.family, HX_OS_TCP, &peer->socket,
			&error);
		if (status == HX_OS_OK) {
			(void)hx_os_socket_set_nodelay(peer->socket, 1, NULL);
			hx_os_operation_init(&operation, HX_OS_OP_CONNECT);
			operation.socket = peer->socket;
			operation.address = endpoint->address;
			operation.deadline = peer->handshake_at;

			status = hx_os_loop_submit(endpoint->loop, &operation,
				hx_ipc_connected, peer, &peer->connecting,
				&error);
		}
	}
	else {
		status = HX_OS_OK;
		hx_ipc_i_lock(endpoint);
		peer->state = 2;
		hx_ipc_i_unlock(endpoint);
	}

	if (status != HX_OS_OK)
		hx_ipc_i_peer_failed(peer, status, &error);
}

hx_os_status hx_ipc_i_kcp_output(
	const void *packet, size_t bytes, void *user, hx_os_error *error)
{
	hx_ipc_connection *peer = user;
	hx_ipc_header header = {0};
	unsigned char wire[HX_IPC_I_HEADER + 1400];
	size_t written;
	hx_os_status status;

	if (bytes > 1400)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	header.kind = HX_IPC_I_KCP_PACKET;
	header.source = peer->id;
	header.destination = peer->remote;
	header.bytes = (uint32_t)bytes;
	hx_ipc_i_encode(wire, &header);
	memcpy(wire + HX_IPC_I_HEADER, packet, bytes);

	// Retry a temporarily full native send buffer within the control
	// budget. This also preserves ACK-only packets during bounded close
	// draining.
	status = hx_os_socket_send_to(peer->endpoint->listener, &peer->address,
		wire, HX_IPC_I_HEADER + bytes, &written, error);
	if (status == HX_OS_WOULD_BLOCK)
		status = hx_ipc_i_queue(peer, &header, packet,
			hx_os_deadline_after_ms(5000), 1, error);

	return status;
}

static void hx_ipc_written(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_ipc_connection *peer = user;
	hx_ipc_frame *frame = peer->sending;

	peer->writing = NULL;
	peer->sending = NULL;

	hx_os_request_release(request);
	if (completion->error.status != HX_OS_OK)
		hx_ipc_i_peer_failed(
			peer, completion->error.status, &completion->error);

	hx_ipc_i_frame_free(peer->endpoint, frame);
}

static hx_ipc_frame *hx_ipc_next_frame(hx_ipc *endpoint)
{
	hx_ipc_frame *frame, *previous = NULL;

	hx_ipc_i_lock(endpoint);

	for (frame = endpoint->outbox; frame; frame = frame->next) {
		hx_ipc_connection *peer = hx_ipc_i_find(endpoint, frame->peer);
		if (!peer || peer->state >= 4 || !peer->writing)
			break;

		previous = frame;
	}

	if (frame) {
		if (previous)
			previous->next = frame->next;
		else
			endpoint->outbox = frame->next;

		if (endpoint->outbox_last == frame)
			endpoint->outbox_last = previous;

		frame->next = NULL;
	}

	hx_ipc_i_unlock(endpoint);

	return frame;
}

static void hx_ipc_defer(hx_ipc *endpoint, hx_ipc_frame *frame)
{
	hx_ipc_i_lock(endpoint);
	if (endpoint->outbox_last)
		endpoint->outbox_last->next = frame;
	else
		endpoint->outbox = frame;

	endpoint->outbox_last = frame;
	hx_ipc_i_unlock(endpoint);
}

static void hx_ipc_write(hx_ipc *endpoint)
{
	size_t count;

	for (count = 0; count < endpoint->options.max_pending_requests +
			endpoint->options.max_peers * 4;
		++count) {
		hx_ipc_frame *frame = hx_ipc_next_frame(endpoint);

		hx_ipc_connection *peer;
		hx_os_error error = {0};
		hx_os_status status = HX_OS_OK;
		hx_os_operation operation;

		if (!frame)
			return;

		peer = hx_ipc_i_find(endpoint, frame->peer);
		if (!peer || peer->state >= 4 ||
			(hx_ipc_i_stopping(endpoint) && !frame->control) ||
			hx_os_deadline_expired(frame->deadline)) {
			hx_ipc_i_complete(endpoint, frame->peer, frame->id,
				peer && peer->state < 4 ? HX_OS_TIMEOUT
							: HX_OS_CLOSED,
				NULL);

			hx_ipc_i_frame_free(endpoint, frame);
			continue;
		}

		if (endpoint->transport == HX_IPC_I_KCP &&
			frame->kind != HX_IPC_I_HELLO &&
			frame->kind != HX_IPC_I_WELCOME &&
			frame->kind != HX_IPC_I_KCP_PACKET) {
			if (!peer->kcp)
				status = HX_OS_CLOSED;
			else
				status = hx_os_kcp_send(peer->kcp, frame->wire,
					frame->bytes, &error);

			if (status == HX_OS_LIMIT) {
				hx_ipc_defer(endpoint, frame);

				return;
			}
		}
		else if (endpoint->transport == HX_IPC_I_UDP ||
			endpoint->transport == HX_IPC_I_KCP) {
			size_t written;

			status = hx_os_socket_send_to(endpoint->listener,
				&peer->address, frame->wire, frame->bytes,
				&written, &error);
			if (status == HX_OS_WOULD_BLOCK) {
				hx_ipc_defer(endpoint, frame);

				return;
			}

			if (status == HX_OS_OK &&
				endpoint->transport == HX_IPC_I_UDP &&
				frame->kind != HX_IPC_I_REQUEST)
				hx_ipc_i_complete(endpoint, peer->id, frame->id,
					HX_OS_OK, NULL);
		}
		else {
			peer->sending = frame;
			hx_os_operation_init(&operation, HX_OS_OP_WRITE);
			operation.socket = peer->socket;
			operation.data = frame->wire;
			operation.bytes = frame->bytes;
			operation.deadline = frame->deadline;
			operation.send_mode = HX_OS_SEND_BORROW;
			if (peer->writer) {
				hx_os_pipe_operation pipe;

				(void)hx_os_pipe_operation_init(
					&pipe, sizeof(pipe), HX_OS_PIPE_WRITE);
				pipe.pipe = peer->writer;
				pipe.data = frame->wire;
				pipe.bytes = frame->bytes;
				pipe.send_mode = HX_OS_SEND_BORROW;
				pipe.deadline = frame->deadline;

				status = hx_os_loop_submit_pipe(endpoint->loop,
					&pipe, hx_ipc_written, peer,
					&peer->writing, &error);
			}
			else
				status = hx_os_loop_submit(endpoint->loop,
					&operation, hx_ipc_written, peer,
					&peer->writing, &error);

			if (status == HX_OS_OK)
				continue;

			peer->sending = NULL;
		}

		if (status != HX_OS_OK)
			hx_ipc_i_peer_failed(peer, status, &error);

		hx_ipc_i_frame_free(endpoint, frame);
	}
}

static void hx_ipc_peer_retire(hx_ipc_connection *peer)
{
	hx_ipc *endpoint = peer->endpoint;
	size_t kcp_bytes;

	if (peer->reading || peer->writing || peer->connecting)
		return;

	if (hx_os_socket_close(&peer->socket, NULL) != HX_OS_OK ||
		(peer->reader &&
			hx_os_pipe_close_until(&peer->reader,
				HX_OS_DEADLINE_NOW, NULL) != HX_OS_OK) ||
		(peer->writer &&
			hx_os_pipe_close_until(&peer->writer,
				HX_OS_DEADLINE_NOW, NULL) != HX_OS_OK))
		return;
	kcp_bytes = hx_os_kcp_reserved_bytes(peer->kcp);

	hx_os_kcp_destroy(peer->kcp);
	peer->kcp = NULL;
	if (peer->incoming) {
		hx_ipc_i_received_free(endpoint, peer->incoming,
			peer->header.kind != HX_IPC_I_REPLY);
		peer->incoming = NULL;
	}

	hx_ipc_i_lock(endpoint);
	endpoint->statistics.kcp_bytes -= kcp_bytes;
	--endpoint->statistics.peers;
	peer->state = endpoint->server ? 0 : 5;
	if (!endpoint->server && !endpoint->closing &&
		endpoint->attempts < endpoint->options.reconnect_attempts) {
		++endpoint->attempts;
		endpoint->retry_at = hx_os_deadline_after_ms(
			endpoint->options.reconnect_delay_ms);
	}
	else
		endpoint->retry_at = 0;

	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
}

static hx_os_deadline hx_ipc_schedule(hx_ipc *endpoint)
{
	uint64_t now = hx_os_monotonic_ns();

	hx_os_deadline next = HX_OS_DEADLINE_INFINITE;
	size_t index;
	int reconnect = 0;

	hx_ipc_i_lock(endpoint);
	if (!endpoint->server && endpoint->retry_at &&
		now >= endpoint->retry_at) {
		endpoint->retry_at = 0;
		reconnect = 1;
	}

	if (endpoint->retry_at)
		next = endpoint->retry_at;

	hx_ipc_i_unlock(endpoint);
	if (reconnect)
		(void)hx_ipc_i_new_peer(endpoint, &endpoint->address);

	for (index = 0; index < endpoint->options.max_peers; ++index) {
		hx_ipc_connection *peer = &endpoint->peers[index];
		uint64_t target = HX_OS_DEADLINE_INFINITE;

		if (!peer->state || peer->state == 5)
			continue;

		if (peer->state == 4) {
			hx_ipc_peer_retire(peer);
			target = hx_os_deadline_after_ms(10);
		}
		else if (peer->state < 3) {
			if (now >= peer->handshake_at)
				hx_ipc_i_peer_failed(peer, HX_OS_TIMEOUT, NULL);
			else if (peer->state == 1 && !peer->connecting)
				hx_ipc_connect_start(peer);
			else if (!endpoint->server && peer->state == 2 &&
				(endpoint->transport == HX_IPC_I_UDP ||
					endpoint->transport == HX_IPC_I_KCP) &&
				now >= peer->ping_at) {
				hx_ipc_i_control(peer, HX_IPC_I_HELLO, 0,
					endpoint->options.max_message_bytes,
					HX_OS_OK);
				peer->ping_at = hx_os_deadline_after_ms(100);
			}

			target = hx_os_deadline_after_ms(20);
		}
		else if (endpoint->options.idle_timeout_ms) {
			uint64_t interval =
				(uint64_t)endpoint->options.idle_timeout_ms *
				UINT64_C(1000000);

			if (now - peer->received_at >= interval)
				hx_ipc_i_peer_failed(peer, HX_OS_TIMEOUT, NULL);
			else if (now >= peer->ping_at) {
				hx_ipc_i_control(
					peer, HX_IPC_I_PING, 0, 0, HX_OS_OK);
				peer->ping_at = now + interval / 3;
			}

			target = peer->ping_at;
			if (target > peer->received_at + interval)
				target = peer->received_at + interval;
		}

		if (peer->kcp && peer->state < 4) {
			hx_os_error error;

			hx_os_status status = hx_os_kcp_update(
				peer->kcp, now / UINT64_C(1000000), &error);

			uint64_t tick = now +
				(uint64_t)hx_os_kcp_next_update_ms(
					peer->kcp, now / UINT64_C(1000000)) *
					1000000;

			if (status != HX_OS_OK && status != HX_OS_WOULD_BLOCK)
				hx_ipc_i_peer_failed(peer, status, &error);

			if (tick < target)
				target = tick;
		}

		if (target < next)
			next = target;
	}

	hx_ipc_i_lock(endpoint);
	if (endpoint->outbox) {
		uint64_t retry = hx_os_deadline_after_ms(10);
		if (retry < next)
			next = retry;
	}

	hx_ipc_i_unlock(endpoint);

	return next;
}

static int hx_ipc_drain(hx_ipc *endpoint)
{
	size_t index;
	int pending = 0;

	uint64_t now = hx_os_monotonic_ns() / UINT64_C(1000000);
	hx_ipc_write(endpoint);

	for (index = 0; index < endpoint->options.max_peers; ++index) {
		hx_ipc_connection *peer = &endpoint->peers[index];

		if (!peer->state || peer->state >= 4)
			continue;

		if (peer->state < 3) {
			hx_ipc_i_peer_failed(peer, HX_OS_CANCELLED, NULL);
			continue;
		}

		if (peer->writing)
			pending = 1;

		if (peer->kcp) {
			hx_os_error error;
			hx_os_status status =
				hx_os_kcp_update(peer->kcp, now, &error);

			if (status == HX_OS_OK)
				status = hx_os_kcp_flush(peer->kcp, &error);

			if (status != HX_OS_OK)
				hx_ipc_i_peer_failed(peer, status, &error);

			if (hx_os_kcp_pending_segments(peer->kcp))
				pending = 1;
		}
	}

	hx_ipc_i_lock(endpoint);
	if (endpoint->outbox)
		pending = 1;

	hx_ipc_i_unlock(endpoint);

	return !pending;
}

static int hx_ipc_shutdown(hx_ipc *endpoint)
{
	size_t index;
	int live = 0;

	if (endpoint->accepting)
		hx_os_request_cancel(endpoint->accepting);

	if (endpoint->datagram_read)
		hx_os_request_cancel(endpoint->datagram_read);

	if (endpoint->local_watch)
		hx_os_loop_watch_stop(endpoint->local_watch);

	for (index = 0; index < endpoint->options.max_peers; ++index) {
		hx_ipc_connection *peer = &endpoint->peers[index];

		if (!peer->state || peer->state == 5)
			continue;

		hx_ipc_i_peer_failed(peer, HX_OS_CANCELLED, NULL);
		hx_ipc_peer_retire(peer);
		if (peer->state == 4)
			live = 1;
	}

	if (live || hx_os_loop_pending(endpoint->loop))
		return 0;

	if ((endpoint->local_watch &&
		    hx_os_loop_watch_close_until(&endpoint->local_watch,
			    HX_OS_DEADLINE_NOW, NULL) != HX_OS_OK) ||
		hx_os_socket_close(&endpoint->listener, NULL) != HX_OS_OK ||
		(endpoint->accept_pipe &&
			hx_os_pipe_close_until(&endpoint->accept_pipe,
				HX_OS_DEADLINE_NOW, NULL) != HX_OS_OK))
		return 0;
	return 1;
}

void *HX_OS_CALL hx_ipc_i_worker(void *user)
{
	hx_ipc *endpoint = user;
	hx_os_loop *loop = endpoint->loop;
	hx_os_error error;

	// The private loop has no submissions yet. Transfer its sole ownership
	// exactly once before exposing any native asynchronous operation.
	loop->owner = hx_os_thread_id();
	if (endpoint->transport == HX_IPC_I_UDP ||
		endpoint->transport == HX_IPC_I_KCP)
		hx_ipc_datagram_start(endpoint);
	if (endpoint->server) {
		if (endpoint->transport == HX_IPC_I_TCP ||
			endpoint->transport == HX_IPC_I_LOCAL)
			hx_ipc_accept_start(endpoint);
	}
	else
		(void)hx_ipc_i_new_peer(endpoint, &endpoint->address);

	for (;;) {
		hx_os_status status;
		hx_os_deadline deadline;

		if (hx_ipc_i_stopping(endpoint)) {
			hx_ipc_i_lock(endpoint);
			deadline = endpoint->shutdown_deadline;
			hx_ipc_i_unlock(endpoint);
			if (!hx_ipc_i_aborting(endpoint) &&
				(hx_os_deadline_expired(deadline) ||
					hx_ipc_drain(endpoint)))
				hx_os_atomic_store(
					&endpoint->stop, 2, HX_OS_RELEASE);
			if (hx_ipc_i_aborting(endpoint) &&
				hx_ipc_shutdown(endpoint))
				break;
			deadline = hx_os_deadline_after_ms(10);
		}
		else {
			hx_ipc_write(endpoint);
			deadline = hx_ipc_schedule(endpoint);
		}

		status = hx_os_loop_pending(loop)
			? hx_os_loop_poll_until(loop, deadline, &error)
			: hx_os_i_loop_wait(loop, deadline, &error);
		if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
			hx_ipc_i_failed(endpoint, &error);
	}

	(void)hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL);
	hx_ipc_i_lock(endpoint);
	endpoint->loop = NULL;
	hx_ipc_i_unlock(endpoint);

	(void)hx_os_loop_destroy(&loop, NULL);
	hx_ipc_i_lock(endpoint);
	endpoint->finished = 1;
	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);

	return NULL;
}
