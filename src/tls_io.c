#include "tls_private.h"

#if HX_OS_HAVE_TLS
/**
 * \brief SSL/BIO/request fields belong exclusively to the owned I/O thread.
 *
 * \details Public flags, errors, queues and progress counters use the endpoint
 * mutex. No native operation or allocator callback runs under that mutex.
 */
void hx_tls_i_fail(hx_os_tls *tls, const hx_os_error *error)
{
	hx_tls_i_lock(tls);
	if (!tls->error.status)
		tls->error = *error;
	tls->closing = 2;
	hx_tls_i_wake(tls);
	hx_tls_i_unlock(tls);
}

void hx_tls_i_peer_fail(hx_tls_peer *peer, const hx_os_error *error)
{
	hx_os_tls *tls = peer->owner;

	hx_tls_i_lock(tls);
	if (peer->error.status >= 0)
		peer->error = *error;
	peer->terminal = 1;
	if (!tls->server && !tls->ready && !tls->error.status)
		tls->error = peer->error;
	hx_tls_i_notify(tls);
	hx_tls_i_wake(tls);
	hx_tls_i_unlock(tls);
}

static void hx_tls_peer_status(hx_tls_peer *peer, hx_os_status status)
{
	hx_os_error error;

	(void)hx_os_i_error(&error, status, 0, 0);
	hx_tls_i_peer_fail(peer, &error);
}

static void hx_tls_marker_complete(hx_tls_peer *peer)
{
	hx_os_tls *tls = peer->owner;

	hx_tls_i_lock(tls);
	while (peer->marker_count) {
		hx_tls_marker *marker = &peer->markers[peer->marker_head];

		if (marker->wire > peer->sent)
			break;
		peer->flushed = marker->plain;
		peer->marker_head = (peer->marker_head + 1) % HX_TLS_MARKERS;
		--peer->marker_count;
	}
	hx_tls_i_notify(tls);
	hx_tls_i_unlock(tls);
}

static void HX_OS_CALL hx_tls_read_done(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_tls_peer *peer = user;
	unsigned char *buffer = peer->buffers;

	peer->reading = NULL;
	if (completion->error.status == HX_OS_OK) {
		int copied =
			BIO_write(peer->bio, buffer, (int)completion->bytes);

		if (copied < 0 || (size_t)copied != completion->bytes)
			hx_tls_peer_status(peer, HX_OS_IO_ERROR);
	}
	else if (completion->error.status == HX_OS_EOF) {
		peer->tcp_eof = 1;
		(void)BIO_shutdown_wr(peer->bio);
	}
	else if (!peer->terminal)
		hx_tls_i_peer_fail(peer, &completion->error);
	hx_os_request_release(request);
}

static void HX_OS_CALL hx_tls_write_done(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_tls_peer *peer = user;

	peer->writing = NULL;
	peer->wire_offset += completion->bytes;
	peer->sent += completion->bytes;
	hx_tls_marker_complete(peer);
	if (completion->error.status != HX_OS_OK && !peer->terminal)
		hx_tls_i_peer_fail(peer, &completion->error);
	hx_os_request_release(request);
}

static int hx_tls_transport(hx_tls_peer *peer)
{
	hx_os_operation operation;
	hx_os_error error;
	size_t bytes;
	int progress = 0;

	if (!peer->writing) {
		if (peer->wire_offset == peer->wire_bytes) {
			int read = BIO_read(peer->bio,
				peer->buffers + HX_TLS_RECORD, HX_TLS_RECORD);

			peer->wire_bytes = read > 0 ? (size_t)read : 0;
			peer->wire_offset = 0;
		}
		if (peer->wire_bytes) {
			hx_os_operation_init(&operation, HX_OS_OP_WRITE);
			operation.socket = peer->socket;
			operation.send_mode = HX_OS_SEND_BORROW;
			operation.data = peer->buffers + HX_TLS_RECORD +
				peer->wire_offset;
			operation.bytes = peer->wire_bytes - peer->wire_offset;
			if (hx_os_loop_submit(peer->owner->loop, &operation,
				    hx_tls_write_done, peer, &peer->writing,
				    &error) != HX_OS_OK)
				hx_tls_i_peer_fail(peer, &error);
			progress = 1;
		}
	}
	bytes = BIO_ctrl_get_write_guarantee(peer->bio);
	if (bytes > HX_TLS_RECORD)
		bytes = HX_TLS_RECORD;
	if (!peer->reading && !peer->tcp_eof && bytes && !peer->terminal) {
		hx_os_operation_init(&operation, HX_OS_OP_READ);
		operation.socket = peer->socket;
		operation.data = peer->buffers;
		operation.bytes = bytes;
		if (hx_os_loop_submit(peer->owner->loop, &operation,
			    hx_tls_read_done, peer, &peer->reading,
			    &error) != HX_OS_OK)
			hx_tls_i_peer_fail(peer, &error);
		progress = 1;
	}

	return progress;
}

static int hx_tls_ssl_result(
	hx_tls_peer *peer, int result, size_t before, int shutdown)
{
	int code =
		result > 0 ? SSL_ERROR_NONE : SSL_get_error(peer->ssl, result);
	size_t after = BIO_ctrl_pending(peer->bio);
	hx_os_error error;

	// SSL only appends to this side of the BIO. Stable wire offsets let a
	// flush complete even while later application calls continue sending.
	if (after < before || after - before > UINT64_MAX - peer->produced) {
		hx_tls_peer_status(peer, HX_OS_LIMIT);

		return 0;
	}
	peer->produced += after - before;
	if (result > 0 || (shutdown && result == 0))
		return 1;
	if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE)
		return 0;
	if (code == SSL_ERROR_ZERO_RETURN) {
		hx_tls_i_lock(peer->owner);
		if (!peer->error.status)
			(void)hx_os_i_error(&peer->error, HX_OS_EOF, 0, 0);
		hx_tls_i_notify(peer->owner);
		hx_tls_i_unlock(peer->owner);

		return 0;
	}
	(void)hx_os_i_crypto_fail(&error,
		SSL_get_verify_result(peer->ssl) != X509_V_OK
			? HX_OS_AUTH_FAILED
			: peer->tcp_eof ? HX_OS_TRUNCATED_INPUT
					: HX_OS_IO_ERROR);
	if (error.status == HX_OS_AUTH_FAILED)
		error.cause_code = SSL_get_verify_result(peer->ssl);
	if (ERR_GET_REASON((unsigned long)error.native_code) ==
		SSL_R_UNEXPECTED_EOF_WHILE_READING)
		error.status = HX_OS_TRUNCATED_INPUT;
	hx_tls_i_peer_fail(peer, &error);

	return 0;
}

static int hx_tls_ready(hx_tls_peer *peer)
{
	hx_os_tls *tls = peer->owner;
	hx_os_tls_peer_info info = peer->info;
	X509 *certificate = SSL_get1_peer_certificate(peer->ssl);
	EVP_MD *digest = EVP_MD_fetch(tls->crypto->native, "SHA256", NULL);
	unsigned int bytes;
	const unsigned char *protocol = NULL;
	unsigned int protocol_bytes = 0;
	hx_os_address local;
	hx_os_error error;
	size_t required;
	int valid = !certificate ||
		(digest &&
			X509_digest(certificate, digest,
				info.certificate_sha256, &bytes) &&
			bytes == sizeof(info.certificate_sha256));

	if (certificate)
		info.flags |= HX_OS_TLS_PEER_CERTIFICATE;
	if (certificate && SSL_get_verify_mode(peer->ssl) != SSL_VERIFY_NONE &&
		SSL_get_verify_result(peer->ssl) == X509_V_OK)
		info.flags |= HX_OS_TLS_PEER_VERIFIED;
	EVP_MD_free(digest);
	X509_free(certificate);
	if (!valid) {
		(void)hx_os_i_crypto_fail(&error, HX_OS_IO_ERROR);
		hx_tls_i_peer_fail(peer, &error);

		return 0;
	}
	SSL_get0_alpn_selected(peer->ssl, &protocol, &protocol_bytes);
	if ((tls->pinned &&
		    (!(info.flags & HX_OS_TLS_PEER_VERIFIED) ||
			    CRYPTO_memcmp(tls->certificate_sha256,
				    info.certificate_sha256, 32))) ||
		(tls->require_alpn && !protocol_bytes) ||
		!hx_tls_i_profile_verify(peer)) {
		hx_tls_peer_status(peer, HX_OS_AUTH_FAILED);
		return 0;
	}
	if (SSL_session_reused(peer->ssl))
		info.flags |= HX_OS_TLS_PEER_RESUMED;
	if (hx_os_i_text_copy(SSL_get_version(peer->ssl), info.protocol,
		    sizeof(info.protocol), &required, &error) != HX_OS_OK ||
		hx_os_i_text_copy(SSL_get_cipher_name(peer->ssl), info.cipher,
			sizeof(info.cipher), &required, &error) != HX_OS_OK) {
		hx_tls_i_peer_fail(peer, &error);

		return 0;
	}
	if (!tls->server &&
		hx_os_socket_local_address(peer->socket, &local, &error) !=
			HX_OS_OK) {
		hx_tls_i_peer_fail(peer, &error);

		return 0;
	}
	hx_tls_i_lock(tls);
	peer->info = info;
	if (protocol_bytes)
		memcpy(peer->alpn, protocol, protocol_bytes);
	peer->alpn[protocol_bytes] = '\0';
	peer->ready = 1;
	if (!tls->server) {
		tls->local = local;
		tls->ready = 1;
	}
	hx_tls_i_notify(tls);
	hx_tls_i_unlock(tls);
	hx_tls_i_profile_capture(peer);

	return 1;
}

static int hx_tls_plain_write(hx_tls_peer *peer)
{
	hx_os_tls *tls = peer->owner;
	unsigned char *buffer = peer->buffers + HX_TLS_RECORD * 2;
	size_t bytes = 0, before;
	int result;

	if (peer->marker_count == HX_TLS_MARKERS)
		return 0;
	if (!peer->tx_bytes) {
		hx_tls_i_lock(tls);
		peer->tx_bytes = hx_tls_i_ring_read(&peer->tx,
			tls->options.queue_capacity, buffer, HX_TLS_RECORD);
		if (peer->tx_bytes)
			hx_tls_i_notify(tls);
		hx_tls_i_unlock(tls);
	}
	if (!peer->tx_bytes)
		return 0;
	before = BIO_ctrl_pending(peer->bio);
	ERR_clear_error();
	result = SSL_write_ex(peer->ssl, buffer, peer->tx_bytes, &bytes);
	if (!hx_tls_ssl_result(peer, result, before, 0))
		return 0;
	if (bytes != peer->tx_bytes) {
		hx_tls_peer_status(peer, HX_OS_IO_ERROR);

		return 0;
	}
	peer->encoded += bytes;
	peer->markers[(peer->marker_head + peer->marker_count) %
		HX_TLS_MARKERS] =
		(hx_tls_marker){peer->produced, peer->encoded};
	++peer->marker_count;
	peer->tx_bytes = 0;
	hx_tls_marker_complete(peer);

	return 1;
}

static int hx_tls_plain_read(hx_tls_peer *peer, int closing)
{
	hx_os_tls *tls = peer->owner;
	unsigned char *buffer = peer->buffers + HX_TLS_RECORD * 3;
	size_t bytes = 0, before, capacity;
	int result;

	hx_tls_i_lock(tls);
	capacity = tls->options.queue_capacity - peer->rx.size;
	hx_tls_i_unlock(tls);
	if (!closing && capacity < HX_TLS_RECORD)
		return 0;
	before = BIO_ctrl_pending(peer->bio);
	ERR_clear_error();
	result = SSL_read_ex(peer->ssl, buffer, HX_TLS_RECORD, &bytes);
	if (!hx_tls_ssl_result(peer, result, before, 0))
		return 0;
	if (!closing) {
		hx_tls_i_lock(tls);
		(void)hx_tls_i_ring_write(
			&peer->rx, tls->options.queue_capacity, buffer, bytes);
		hx_tls_i_notify(tls);
		hx_tls_i_unlock(tls);
	}

	return bytes != 0;
}

static void hx_tls_retire(hx_tls_peer *peer)
{
	hx_os_error error;

	if (peer->reading)
		hx_os_request_cancel(peer->reading);
	if (peer->writing)
		hx_os_request_cancel(peer->writing);
	if (peer->reading || peer->writing)
		return;
	if (hx_os_socket_close(&peer->socket, &error) != HX_OS_OK) {
		hx_tls_i_peer_fail(peer, &error);
		return;
	}
	SSL_free(peer->ssl);
	SSL_SESSION_free(peer->pending_session);
	peer->pending_session = NULL;
	BIO_free(peer->bio);
	peer->ssl = NULL;
	peer->bio = NULL;
	hx_tls_i_lock(peer->owner);
	peer->retired = 1;
	// Failed, never announced server handshakes do not consume a slot.
	if (peer->owner->server && !peer->ready)
		peer->release = 1;
	hx_tls_i_notify(peer->owner);
	hx_tls_i_unlock(peer->owner);
}

static int hx_tls_pump(hx_tls_peer *peer, hx_os_deadline *deadline)
{
	hx_os_tls *tls = peer->owner;
	int closing, ready, progress = 0, eof;
	uint64_t accepted;

	hx_tls_i_lock(tls);
	closing = peer->closing > tls->closing ? peer->closing : tls->closing;
	ready = peer->ready;
	eof = peer->error.status == HX_OS_EOF;
	accepted = peer->accepted;
	hx_tls_i_unlock(tls);
	if (closing == 2 || (closing && !ready))
		hx_tls_peer_status(peer, HX_OS_CANCELLED);
	if (!ready && hx_os_deadline_expired(peer->handshake))
		hx_tls_peer_status(peer, HX_OS_TIMEOUT);
	if (peer->terminal) {
		hx_tls_retire(peer);

		return 0;
	}
	if (!ready && peer->handshake < *deadline)
		*deadline = peer->handshake;
	if (!ready) {
		size_t before = BIO_ctrl_pending(peer->bio);
		int result;

		ERR_clear_error();
		result = SSL_do_handshake(peer->ssl);
		if (hx_tls_ssl_result(peer, result, before, 0))
			progress |= hx_tls_ready(peer);
	}
	else {
		if (!eof)
			progress |= hx_tls_plain_read(peer, closing);
		eof = (SSL_get_shutdown(peer->ssl) & SSL_RECEIVED_SHUTDOWN) !=
			0;
		if (!peer->terminal)
			progress |= hx_tls_plain_write(peer);
		if (!peer->terminal && (closing || eof) &&
			peer->encoded == accepted && !peer->tx_bytes) {
			size_t before = BIO_ctrl_pending(peer->bio);
			int result;

			ERR_clear_error();
			result = SSL_shutdown(peer->ssl);
			(void)hx_tls_ssl_result(peer, result, before, 1);
			if (result == 1 && peer->sent == peer->produced) {
				peer->terminal = 1;
				hx_tls_retire(peer);

				return 0;
			}
		}
	}
	if (!peer->terminal)
		progress |= hx_tls_transport(peer);
	if (peer->terminal)
		hx_tls_retire(peer);

	return progress;
}

static hx_tls_peer *hx_tls_new_peer(hx_os_tls *tls, hx_os_socket *socket,
	const hx_os_address *address, hx_os_error *error)
{
	hx_tls_peer *peer = NULL;
	size_t i;
	hx_os_status status;

	hx_tls_i_lock(tls);
	for (i = 0; i < tls->options.max_peers; ++i)
		if (!tls->peers[i].info.id) {
			peer = tls->peers + i;
			break;
		}
	if (peer && tls->next_id != UINT64_MAX) {
		peer->owner = tls;
		peer->info.size = sizeof(peer->info);
		peer->info.version = 1;
		peer->info.id = ++tls->next_id;
		peer->info.address = *address;
		peer->socket = socket;
		peer->handshake = hx_os_deadline_after_ms(
			tls->options.handshake_timeout_ms);
	}
	else
		peer = NULL;
	hx_tls_i_unlock(tls);
	if (!peer) {
		(void)hx_os_socket_close(&socket, NULL);
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}
	peer->buffers = hx_os_i_zero(tls->context,
		HX_TLS_RECORD * 4 + tls->options.queue_capacity * 2);
	status = peer->buffers ? HX_OS_OK
			       : hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (peer->buffers) {
		peer->rx.data = peer->buffers + HX_TLS_RECORD * 4;
		peer->tx.data = peer->rx.data + tls->options.queue_capacity;
	}
	if (status == HX_OS_OK)
		status = hx_tls_i_ssl(peer, error);
	if (status == HX_OS_OK && tls->server && !tls->extended &&
		(tls->options.flags & HX_OS_TLS_NODELAY))
		status = hx_os_socket_set_nodelay(peer->socket, 1, error);
	if (status == HX_OS_OK && tls->extended && tls->server) {
		hx_os_tcp_options options = tls->tcp_options;

		// The listener already chose the address-family binding policy.
		options.dual_stack = -1;
		status = hx_os_socket_apply_tcp_options(
			peer->socket, &options, error);
	}
	if (status != HX_OS_OK)
		hx_tls_i_peer_fail(peer, error);

	return peer;
}

static void HX_OS_CALL hx_tls_accept_done(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	hx_os_tls *tls = user;
	hx_os_error error;
	int closing;

	tls->accepting = NULL;
	hx_tls_i_lock(tls);
	closing = tls->closing;
	hx_tls_i_unlock(tls);
	if (completion->error.status == HX_OS_OK && !closing) {
		hx_os_socket *socket = hx_os_request_take_socket(request);

		(void)hx_tls_new_peer(tls, socket, &completion->peer, &error);
	}
	else if (!closing)
		hx_tls_i_fail(tls, &completion->error);
	hx_os_request_release(request);
}

static int hx_tls_connect_cancelled(void *user)
{
	hx_os_tls *tls = user;
	int cancelled;

	hx_tls_i_lock(tls);
	cancelled = tls->closing;
	hx_tls_i_unlock(tls);

	return cancelled != 0;
}

static hx_os_status hx_tls_dial(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_address addresses[256], address;
	hx_os_dial_options options = HX_OS_DIAL_OPTIONS_INIT;
	hx_os_socket *socket = NULL;
	hx_tls_peer *peer;
	hx_os_status status;
	size_t count = hx_os_address_list_count(tls->addresses), i;

	for (i = 0; i < count && i < 256; ++i)
		addresses[i] = *hx_os_address_list_get(tls->addresses, i);
	if (tls->extended) {
		options.socket = tls->tcp_options;
		options.attempt_delay_ms = tls->attempt_delay_ms;
	}
	else if (tls->options.flags & HX_OS_TLS_NODELAY)
		options.socket.nodelay = 1;

	status = hx_os_i_tcp_dial_addresses_until(tls->network, addresses,
		count, &options, deadline, hx_tls_connect_cancelled, tls,
		&socket, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_socket_peer_address(socket, &address, error);
	if (status != HX_OS_OK) {
		hx_os_i_socket_discard(&socket);
		return status;
	}
	peer = hx_tls_new_peer(tls, socket, &address, error);
	if (!peer)
		return error->status;
	peer->handshake = deadline;

	return HX_OS_OK;
}

static hx_os_status hx_tls_startup(hx_os_tls *tls, hx_os_error *error)
{
	hx_os_loop_options options;
	hx_os_address address;
	hx_os_status status = hx_tls_i_credentials(tls, error);
	hx_os_loop *loop = NULL;

	if (status == HX_OS_OK)
		status = hx_os_network_create(
			tls->context, &tls->network, error);
	hx_os_loop_options_init(&options);
	options.dns_threads = 0;
	options.max_requests = (size_t)tls->options.max_peers * 2 + 2;
	options.max_pending_bytes = options.max_requests * HX_TLS_RECORD;
	if (status == HX_OS_OK)
		status =
			hx_os_loop_create(tls->network, &options, &loop, error);
	hx_tls_i_lock(tls);
	tls->loop = loop;
	if (tls->closing && status == HX_OS_OK)
		status = hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
	hx_tls_i_unlock(tls);
	if (status != HX_OS_OK)
		return status;
	if (!tls->server) {
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			tls->options.handshake_timeout_ms);

		if (tls->resolve_async) {
			hx_os_resolver *resolver = NULL;

			status = hx_os_resolver_create(tls->context,
				&tls->resolver_options, &resolver, error);
			if (status == HX_OS_OK)
				status = hx_os_i_resolver_resolve_until(
					resolver, tls->host, tls->port,
					HX_OS_TCP, deadline, NULL,
					hx_tls_connect_cancelled, tls,
					&tls->addresses, error);
			(void)hx_os_resolver_destroy(&resolver, NULL);
		}
		else
			status = hx_os_resolve(tls->network, tls->host,
				tls->port, HX_OS_TCP, &tls->addresses, error);
		if (status == HX_OS_OK) {
			// Keep the original post-DNS budget.
			if (!tls->extended)
				deadline = hx_os_deadline_after_ms(
					tls->options.handshake_timeout_ms);
			return hx_tls_dial(tls, deadline, error);
		}
		return status;
	}
	status = hx_os_address_parse(tls->host, tls->port, &address, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_create(tls->network, address.family,
			HX_OS_TCP, &tls->listener, error);
	if (status == HX_OS_OK && (tls->options.flags & HX_OS_TLS_REUSEADDR))
		status = hx_os_socket_set_reuseaddr(tls->listener, 1, error);
	if (status == HX_OS_OK && tls->extended)
		status = hx_os_socket_apply_tcp_options(
			tls->listener, &tls->tcp_options, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_bind(tls->listener, &address, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_listen(
			tls->listener, (int)tls->options.max_peers, error);
	if (status == HX_OS_OK)
		status = hx_os_socket_local_address(
			tls->listener, &address, error);
	if (status == HX_OS_OK) {
		hx_tls_i_lock(tls);
		tls->local = address;
		tls->ready = 1;
		hx_tls_i_notify(tls);
		hx_tls_i_unlock(tls);
	}

	return status;
}

static void hx_tls_collect(hx_tls_peer *peer)
{
	hx_os_tls *tls = peer->owner;
	unsigned char *buffers = NULL;

	hx_tls_i_lock(tls);
	if (peer->retired && peer->release && !peer->users) {
		buffers = peer->buffers;
		memset(peer, 0, sizeof(*peer));
	}
	hx_tls_i_unlock(tls);
	if (buffers) {
		hx_os_secure_zero(buffers,
			HX_TLS_RECORD * 4 + tls->options.queue_capacity * 2);
		hx_os_memory_free(buffers);
	}
}

static void hx_tls_service(hx_os_tls *tls)
{
	hx_os_loop *loop = tls->loop;
	hx_os_error error;

	for (;;) {
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE;
		size_t i;
		int closing, live = 0, progress = 0, free_slot = 0;
		hx_os_status status;

		hx_tls_i_lock(tls);
		closing = tls->closing;
		hx_tls_i_unlock(tls);
		if (closing && tls->accepting)
			hx_os_request_cancel(tls->accepting);
		for (i = 0; i < tls->options.max_peers; ++i) {
			hx_tls_peer *peer = tls->peers + i;

			if (peer->info.id && !peer->retired)
				progress |= hx_tls_pump(peer, &deadline);
			if (peer->info.id && !peer->retired)
				live = 1;
			if (peer->info.id)
				hx_tls_collect(peer);
			if (!peer->info.id)
				free_slot = 1;
		}
		if (closing && !live && !hx_os_loop_pending(loop))
			break;
		if (!closing && tls->server && !tls->accepting && free_slot) {
			hx_os_operation operation;

			hx_os_operation_init(&operation, HX_OS_OP_ACCEPT);
			operation.socket = tls->listener;
			if (hx_os_loop_submit(loop, &operation,
				    hx_tls_accept_done, tls, &tls->accepting,
				    &error) != HX_OS_OK)
				hx_tls_i_fail(tls, &error);
		}
		if (progress)
			deadline = HX_OS_DEADLINE_NOW;
		status = hx_os_loop_pending(loop)
			? hx_os_loop_poll_until(loop, deadline, &error)
			: hx_os_i_loop_wait(loop, deadline, &error);
		if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
			hx_tls_i_fail(tls, &error);
	}
}

void *HX_OS_CALL hx_tls_i_worker(void *user)
{
	hx_os_tls *tls = user;
	hx_os_crypto_scope scope;
	hx_os_error error;
	hx_os_status status;
	size_t i;

	hx_tls_i_guard(tls);
	status = hx_os_i_crypto_enter(
		tls->profile ? tls->profile->configuration->crypto : NULL,
		tls->context, &scope, &error);
	if (status == HX_OS_OK) {
		tls->crypto = scope.crypto;
		status = hx_tls_startup(tls, &error);
	}
	if (status != HX_OS_OK)
		hx_tls_i_fail(tls, &error);
	if (tls->loop) {
		hx_os_loop *loop = tls->loop;

		hx_tls_service(tls);
		(void)hx_os_socket_close(&tls->listener, NULL);
		(void)hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL);
		hx_tls_i_lock(tls);
		tls->loop = NULL;
		hx_tls_i_unlock(tls);
		(void)hx_os_loop_destroy(&loop, NULL);
	}
	(void)hx_os_network_destroy(&tls->network, NULL);
	hx_os_address_list_destroy(tls->addresses);
	hx_tls_i_lock(tls);
	while (tls->users)
		(void)hx_os_condition_wait_until(tls->condition, tls->mutex,
			HX_OS_DEADLINE_INFINITE, NULL);
	hx_tls_i_unlock(tls);
	for (i = 0; i < tls->options.max_peers; ++i) {
		hx_tls_peer *peer = tls->peers + i;

		if (peer->buffers) {
			hx_os_secure_zero(peer->buffers,
				HX_TLS_RECORD * 4 +
					tls->options.queue_capacity * 2);
			hx_os_memory_free(peer->buffers);
			peer->buffers = NULL;
		}
	}
	SSL_CTX_free(tls->ssl_context);
	if (tls->crypto)
		hx_os_i_crypto_leave(&scope);
	// This thread is library-owned, including when OpenSSL is static.
	hx_os_crypto_thread_cleanup();
	hx_tls_i_lock(tls);
	hx_tls_i_notify(tls);
	hx_tls_i_unlock(tls);
	hx_tls_i_guard(NULL);

	return NULL;
}
#endif
