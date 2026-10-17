#include "ipc_private.h"

static int hx_ipc_address_equal(
	const hx_os_address *first, const hx_os_address *second);

static uint64_t hx_ipc_read(const unsigned char *data, size_t count)
{
	uint64_t result = 0;

	while (count--)
		result = (result << 8) | *data++;

	return result;
}

static void hx_ipc_write(unsigned char *data, size_t count, uint64_t value)
{
	while (count) {
		data[--count] = (unsigned char)value;
		value >>= 8;
	}
}

void hx_ipc_i_encode(unsigned char *wire, const hx_ipc_header *header)
{
	memset(wire, 0, HX_IPC_I_HEADER);
	memcpy(wire, "HXIP", 4);
	wire[4] = 1;
	wire[5] = (unsigned char)header->kind;
	hx_ipc_write(wire + 6, 2, (uint64_t)header->encoding);
	hx_ipc_write(wire + 8, 4, header->bytes);
	hx_ipc_write(wire + 12, 4, (uint32_t)header->status);
	hx_ipc_write(wire + 16, 8, header->source);
	hx_ipc_write(wire + 24, 8, header->destination);
	hx_ipc_write(wire + 32, 8, header->id);
	hx_ipc_write(wire + 40, 8, header->channel);
	hx_ipc_write(wire + 48, 8, header->auxiliary);
}

int hx_ipc_i_decode(const unsigned char *wire, hx_ipc_header *header)
{
	if (memcmp(wire, "HXIP", 4) || wire[4] != 1 ||
		wire[5] < HX_IPC_I_HELLO || wire[5] > HX_IPC_I_KCP_PACKET ||
		wire[6] || wire[7] > HX_IPC_UTF8 || hx_ipc_read(wire + 56, 8))
		return 0;
	header->kind = wire[5];
	header->encoding = wire[7];
	header->bytes = (uint32_t)hx_ipc_read(wire + 8, 4);
	header->status = (hx_os_status)(uint32_t)hx_ipc_read(wire + 12, 4);
	header->source = hx_ipc_read(wire + 16, 8);
	header->destination = hx_ipc_read(wire + 24, 8);
	header->id = hx_ipc_read(wire + 32, 8);
	header->channel = hx_ipc_read(wire + 40, 8);
	header->auxiliary = hx_ipc_read(wire + 48, 8);
	if (!header->source)
		return 0;

	if (header->kind != HX_IPC_I_DATA && header->kind != HX_IPC_I_REQUEST &&
		header->kind != HX_IPC_I_REPLY &&
		header->kind != HX_IPC_I_KCP_PACKET && header->bytes)
		return 0;
	return 1;
}

void hx_ipc_i_control(hx_ipc_connection *peer, int kind, uint64_t id,
	uint64_t auxiliary, hx_os_status status)
{
	hx_ipc_header header = {0};
	hx_os_error error;

	header.kind = kind;
	header.id = id;
	header.auxiliary = auxiliary;
	header.status = status;
	header.source = peer->id;
	header.destination = kind == HX_IPC_I_HELLO ? 0 : peer->remote;
	if (hx_ipc_i_queue(peer->endpoint, peer, &header, NULL,
		    hx_os_deadline_after_ms(5000), 1, &error) != HX_OS_OK)
		hx_ipc_i_peer_failed(peer, error.status, &error);
}

static hx_os_status hx_ipc_kcp_start(hx_ipc_connection *peer)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_os_kcp_options options;
	hx_os_error error;
	hx_os_status status;
	uint32_t conversation = (uint32_t)(peer->id ^ peer->remote);

	if (endpoint->transport != HX_IPC_I_KCP || peer->kcp)
		return HX_OS_OK;

	if (!conversation)
		conversation = 1;

	hx_ipc_i_lock(endpoint);
	if (endpoint->options.max_kcp_bytes - endpoint->statistics.kcp_bytes <
		2 * 1024 * 1024) {
		hx_ipc_i_unlock(endpoint);

		return HX_OS_LIMIT;
	}

	hx_ipc_i_unlock(endpoint);
	(void)hx_os_kcp_options_init(&options, sizeof(options));
	options.max_message_bytes = peer->max_message + HX_IPC_I_HEADER;

	status = hx_os_kcp_create(endpoint->context, conversation, &options,
		hx_ipc_i_kcp_output, peer, &peer->kcp, &error);
	if (status != HX_OS_OK)
		return status;

	hx_ipc_i_lock(endpoint);
	endpoint->statistics.kcp_bytes += hx_os_kcp_reserved_bytes(peer->kcp);
	hx_ipc_i_unlock(endpoint);

	return HX_OS_OK;
}

static void hx_ipc_ready(hx_ipc_connection *peer)
{
	hx_ipc *endpoint = peer->endpoint;
	size_t index;

	if (peer->state == 3 || peer->state >= 4)
		return;

	// A fresh HELLO alone cannot evict a live UDP session: an old HELLO may
	// have been delayed. Confirm possession of the new server nonce first.
	if (endpoint->server &&
		(endpoint->transport == HX_IPC_I_UDP ||
			endpoint->transport == HX_IPC_I_KCP))
		for (index = 0; index < endpoint->options.max_peers; ++index) {
			hx_ipc_connection *old = &endpoint->peers[index];

			if (old != peer && old->state == 3 &&
				hx_ipc_address_equal(
					&old->address, &peer->address))
				hx_ipc_i_peer_failed(old, HX_OS_CLOSED, NULL);
		}

	hx_ipc_i_lock(endpoint);
	peer->state = 3;
	hx_os_error_clear(&peer->error);
	endpoint->attempts = 0;
	if (endpoint->use_backoff)
		(void)hx_os_backoff_reset(&endpoint->backoff, NULL);
	hx_os_condition_notify_all(endpoint->condition);
	hx_ipc_i_unlock(endpoint);
	hx_ipc_i_event(peer, HX_IPC_CONNECTED);
	peer->announced = 1;
}

static void hx_ipc_handshake(
	hx_ipc_connection *peer, const hx_ipc_header *header)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_os_status status;

	if (!header->auxiliary || header->auxiliary > 16 * 1024 * 1024 ||
		(header->kind == HX_IPC_I_HELLO && !endpoint->server) ||
		(header->kind == HX_IPC_I_WELCOME && endpoint->server)) {
		hx_ipc_i_peer_failed(peer, HX_OS_SYNTAX, NULL);

		return;
	}

	hx_ipc_i_lock(endpoint);
	peer->remote = header->source;
	peer->max_message = (size_t)header->auxiliary;
	if (peer->max_message > endpoint->options.max_message_bytes)
		peer->max_message = endpoint->options.max_message_bytes;

	hx_ipc_i_unlock(endpoint);

	status = hx_ipc_kcp_start(peer);
	if (status != HX_OS_OK) {
		hx_ipc_i_peer_failed(peer, status, NULL);

		return;
	}

	if (endpoint->server)
		hx_ipc_i_control(peer, HX_IPC_I_WELCOME, 0,
			endpoint->options.max_message_bytes, HX_OS_OK);
	else
		hx_ipc_i_control(peer, HX_IPC_I_PONG, 0, 1, HX_OS_OK);
}

static hx_ipc_received *hx_ipc_receive_reserve(hx_ipc_connection *peer,
	const hx_ipc_header *header, hx_os_status *status)
{
	hx_ipc *endpoint = peer->endpoint;
	hx_ipc_received *message;
	int counted = header->kind != HX_IPC_I_REPLY;

	hx_ipc_i_lock(endpoint);
	if (endpoint->closing) {
		hx_ipc_i_unlock(endpoint);
		*status = HX_OS_CLOSED;

		return NULL;
	}

	if (header->bytes > endpoint->options.max_pending_bytes -
				endpoint->statistics.pending_bytes ||
		(counted &&
			endpoint->inbox_count >=
				endpoint->options.max_messages)) {
		hx_ipc_i_unlock(endpoint);
		*status = HX_OS_LIMIT;

		return NULL;
	}

	endpoint->statistics.pending_bytes += header->bytes;
	endpoint->inbox_count += (size_t)counted;
	hx_ipc_i_unlock(endpoint);
	message = hx_os_i_zero(
		endpoint->context, sizeof(*message) + header->bytes + 1);
	if (!message) {
		hx_ipc_i_lock(endpoint);
		endpoint->statistics.pending_bytes -= header->bytes;
		endpoint->inbox_count -= (size_t)counted;
		hx_ipc_i_unlock(endpoint);
		*status = HX_OS_NO_MEMORY;

		return NULL;
	}

	message->message.data = message->payload;
	message->message.bytes = header->bytes;
	message->message.peer = peer->id;
	message->message.channel = header->channel;
	message->message.request_id = header->id;
	message->message.encoding = header->encoding;
	message->message.kind =
		header->kind == HX_IPC_I_REQUEST ? HX_IPC_REQUEST : HX_IPC_DATA;

	return message;
}

static void hx_ipc_dispatch_received(hx_ipc_connection *peer,
	const hx_ipc_header *header, hx_ipc_received *message,
	hx_os_status rejected)
{
	hx_ipc *endpoint = peer->endpoint;
	size_t error_offset;
	int data = header->kind == HX_IPC_I_DATA ||
		header->kind == HX_IPC_I_REQUEST ||
		header->kind == HX_IPC_I_REPLY;

	peer->received_at = hx_os_monotonic_ns();
	if (header->kind == HX_IPC_I_HELLO ||
		header->kind == HX_IPC_I_WELCOME) {
		hx_ipc_handshake(peer, header);

		return;
	}

	if (header->kind == HX_IPC_I_PING) {
		hx_ipc_i_control(peer, HX_IPC_I_PONG, header->id, 0, HX_OS_OK);

		return;
	}

	if (header->kind == HX_IPC_I_PONG) {
		if (endpoint->server && header->auxiliary == 1) {
			hx_ipc_ready(peer);
			hx_ipc_i_control(peer, HX_IPC_I_PONG, 0, 2, HX_OS_OK);
		}
		else if (!endpoint->server && header->auxiliary == 2)
			hx_ipc_ready(peer);

		return;
	}

	if (header->kind == HX_IPC_I_CLOSE) {
		hx_ipc_i_peer_failed(peer, HX_OS_CLOSED, NULL);

		return;
	}

	if (header->kind == HX_IPC_I_ACK) {
		hx_ipc_i_complete(
			endpoint, peer->id, header->id, header->status, NULL);

		return;
	}

	if (!data)
		return;

	if (hx_ipc_i_stopping(endpoint))
		rejected = HX_OS_CLOSED;

	if (message && header->encoding == HX_IPC_UTF8 &&
		hx_os_text_validate_utf8(message->payload,
			message->message.bytes, &error_offset) != HX_OS_OK)
		rejected = HX_OS_INVALID_ENCODING;
	if (rejected) {
		if (message)
			hx_ipc_i_received_free(endpoint, message,
				header->kind != HX_IPC_I_REPLY);

		hx_ipc_i_lock(endpoint);
		++endpoint->statistics.rejected_messages;
		hx_ipc_i_unlock(endpoint);
		hx_ipc_i_control(peer, HX_IPC_I_ACK, header->id, 0, rejected);
		if (header->kind == HX_IPC_I_REPLY)
			hx_ipc_i_complete(endpoint, peer->id, header->auxiliary,
				rejected, NULL);

		return;
	}

	if (header->kind == HX_IPC_I_REPLY) {
		hx_ipc_i_complete(endpoint, peer->id, header->auxiliary,
			HX_OS_OK, &message->message);
	}
	else {
		hx_ipc_i_lock(endpoint);
		if (endpoint->inbox_last)
			endpoint->inbox_last->next = message;
		else
			endpoint->inbox = message;

		endpoint->inbox_last = message;
		++endpoint->statistics.received_messages;
		hx_os_condition_notify_all(endpoint->condition);
		hx_ipc_i_unlock(endpoint);
	}

	if (endpoint->transport != HX_IPC_I_UDP ||
		header->kind == HX_IPC_I_REPLY)
		hx_ipc_i_control(peer, HX_IPC_I_ACK, header->id, 0, HX_OS_OK);
}

static int hx_ipc_header_valid(
	hx_ipc_connection *peer, const hx_ipc_header *header)
{
	if (header->bytes >
		(header->kind == HX_IPC_I_KCP_PACKET
				? 1400
				: peer->endpoint->options.max_message_bytes))
		return 0;
	if (header->kind == HX_IPC_I_HELLO)
		return peer->endpoint->server && !header->destination &&
			(!peer->remote || peer->remote == header->source);

	if (header->kind == HX_IPC_I_WELCOME)
		return !peer->endpoint->server &&
			header->destination == peer->id &&
			(!peer->remote || peer->remote == header->source);

	return (peer->state == 3 ||
		       (peer->state == 2 &&
			       (header->kind == HX_IPC_I_PONG ||
				       header->kind == HX_IPC_I_KCP_PACKET))) &&
		header->source == peer->remote &&
		header->destination == peer->id;
}

void hx_ipc_i_stream_input(
	hx_ipc_connection *peer, const unsigned char *data, size_t bytes)
{
	while (bytes && peer->state < 4) {
		size_t count;

		if (peer->header_bytes < HX_IPC_I_HEADER) {
			count = HX_IPC_I_HEADER - peer->header_bytes;
			if (count > bytes)
				count = bytes;

			memcpy(peer->prefix + peer->header_bytes, data, count);
			peer->header_bytes += count;
			data += count;
			bytes -= count;
			if (peer->header_bytes != HX_IPC_I_HEADER)
				continue;

			if (!hx_ipc_i_decode(peer->prefix, &peer->header) ||
				!hx_ipc_header_valid(peer, &peer->header) ||
				peer->header.kind == HX_IPC_I_KCP_PACKET) {
				hx_ipc_i_peer_failed(peer, HX_OS_SYNTAX, NULL);

				return;
			}

			if (peer->header.kind >= HX_IPC_I_DATA &&
				peer->header.kind <= HX_IPC_I_REPLY)
				peer->incoming = hx_ipc_receive_reserve(
					peer, &peer->header, &peer->rejected);
		}

		count = peer->header.bytes - peer->body_bytes;
		if (count > bytes)
			count = bytes;

		if (peer->incoming && count)
			memcpy(peer->incoming->payload + peer->body_bytes, data,
				count);

		peer->body_bytes += count;
		data += count;
		bytes -= count;
		if (peer->body_bytes == peer->header.bytes) {
			hx_ipc_dispatch_received(peer, &peer->header,
				peer->incoming, peer->rejected);
			peer->incoming = NULL;
			peer->header_bytes = peer->body_bytes = 0;
			peer->rejected = HX_OS_OK;
		}
	}
}

static int hx_ipc_address_equal(
	const hx_os_address *first, const hx_os_address *second)
{
	return first->family == second->family && first->port == second->port &&
		first->scope_id == second->scope_id &&
		!memcmp(first->bytes, second->bytes, sizeof(first->bytes));
}

static void hx_ipc_datagram_message(
	hx_ipc_connection *peer, const unsigned char *data, size_t bytes)
{
	hx_ipc_header header;
	hx_ipc_received *message = NULL;
	hx_os_status rejected = HX_OS_OK;

	if (bytes < HX_IPC_I_HEADER || !hx_ipc_i_decode(data, &header) ||
		bytes - HX_IPC_I_HEADER != header.bytes ||
		!hx_ipc_header_valid(peer, &header) ||
		header.kind == HX_IPC_I_KCP_PACKET) {
		hx_ipc_i_lock(peer->endpoint);
		++peer->endpoint->statistics.discarded_datagrams;
		hx_ipc_i_unlock(peer->endpoint);

		return;
	}

	if (header.kind >= HX_IPC_I_DATA && header.kind <= HX_IPC_I_REPLY) {
		message = hx_ipc_receive_reserve(peer, &header, &rejected);
		if (message && header.bytes)
			memcpy(message->payload, data + HX_IPC_I_HEADER,
				header.bytes);
	}

	hx_ipc_dispatch_received(peer, &header, message, rejected);
}

void hx_ipc_i_datagram(hx_ipc *endpoint, const hx_os_address *address,
	const unsigned char *data, size_t bytes)
{
	hx_ipc_header header;
	hx_ipc_connection *peer = NULL;
	size_t index;

	if (bytes < HX_IPC_I_HEADER || !hx_ipc_i_decode(data, &header) ||
		bytes - HX_IPC_I_HEADER != header.bytes)
		goto discarded;
	for (index = 0; index < endpoint->options.max_peers; ++index) {
		hx_ipc_connection *candidate = &endpoint->peers[index];

		if (candidate->state && candidate->state < 4 &&
			hx_ipc_address_equal(&candidate->address, address) &&
			(header.kind == HX_IPC_I_HELLO
					? candidate->remote == header.source
					: candidate->id ==
						header.destination)) {
			peer = candidate;
			break;
		}
	}

	if (endpoint->server && header.kind == HX_IPC_I_HELLO &&
		!header.destination) {
		if (!peer && !hx_ipc_i_stopping(endpoint))
			peer = hx_ipc_i_new_peer(endpoint, address);
	}

	if (!peer || !hx_ipc_header_valid(peer, &header))
		goto discarded;

	if (header.kind == HX_IPC_I_KCP_PACKET) {
		hx_os_status status;
		size_t received;

		if (!peer->kcp)
			goto discarded;

		status = hx_os_kcp_input(
			peer->kcp, data + HX_IPC_I_HEADER, header.bytes, NULL);
		if (status != HX_OS_OK)
			goto discarded;

		peer->received_at = hx_os_monotonic_ns();

		for (;;) {
			status = hx_os_kcp_receive(peer->kcp,
				endpoint->kcp_message,
				endpoint->options.max_message_bytes +
					HX_IPC_I_HEADER,
				&received, NULL);
			if (status == HX_OS_WOULD_BLOCK)
				break;

			if (status != HX_OS_OK) {
				hx_ipc_i_peer_failed(peer, status, NULL);
				break;
			}

			hx_ipc_datagram_message(
				peer, endpoint->kcp_message, received);
			if (peer->state >= 4)
				break;
		}
	}
	else {
		if (endpoint->transport == HX_IPC_I_KCP &&
			header.kind != HX_IPC_I_HELLO &&
			header.kind != HX_IPC_I_WELCOME)
			goto discarded;
		hx_ipc_datagram_message(peer, data, bytes);
	}

	return;

discarded:
	hx_ipc_i_lock(endpoint);
	++endpoint->statistics.discarded_datagrams;
	hx_ipc_i_unlock(endpoint);
}
