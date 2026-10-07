#include "tls_private.h"

#if HX_OS_HAVE_TLS
static hx_os_status hx_tls_enter(hx_os_tls *tls, hx_os_error *error)
{
	if (!tls || hx_tls_i_reentrant(tls))
		return hx_os_i_error(
			error, tls ? HX_OS_DEADLOCK : HX_OS_INVALID, 0, 0);
	hx_tls_i_lock(tls);
	++tls->users;
	hx_os_error_clear(error);

	return HX_OS_OK;
}

static hx_os_status hx_tls_leave(hx_os_tls *tls, hx_tls_peer *peer,
	hx_os_status status, hx_os_error *error)
{
	if (peer)
		--peer->users;
	--tls->users;
	hx_tls_i_wake(tls);
	hx_tls_i_unlock(tls);
	if (!error || error->status != status)
		(void)hx_os_i_error(error, status, 0, 0);

	return status;
}

static hx_tls_peer *hx_tls_peer_get(hx_os_tls *tls, uint64_t id)
{
	size_t i;

	for (i = 0; i < tls->options.max_peers; ++i) {
		hx_tls_peer *peer = tls->peers + i;

		if (peer->info.id && !peer->release &&
			(peer->info.id == id || (!id && !tls->server))) {
			++peer->users;

			return peer;
		}
	}

	return NULL;
}

static hx_os_status hx_tls_error(hx_os_error *out, const hx_os_error *error)
{
	if (out)
		*out = *error;

	return error->status;
}

static hx_os_status hx_tls_wait(hx_os_tls *tls, uint64_t generation,
	hx_os_deadline deadline, hx_os_error *error)
{
	if (generation != tls->cancelled)
		return HX_OS_CANCELLED;
	if (hx_os_deadline_expired(deadline))
		return HX_OS_TIMEOUT;

	return hx_os_condition_wait_until(
		tls->condition, tls->mutex, deadline, error);
}

hx_os_status hx_os_tls_wait_ready_until(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	uint64_t generation;

	if (status != HX_OS_OK)
		return status;
	generation = tls->cancelled;
	while (status == HX_OS_OK && !tls->ready && !tls->closing &&
		!tls->error.status)
		status = hx_tls_wait(tls, generation, deadline, error);
	if (status == HX_OS_OK) {
		if (tls->error.status)
			status = hx_tls_error(error, &tls->error);
		else if (tls->closing)
			status = HX_OS_CLOSED;
	}

	return hx_tls_leave(tls, NULL, status, error);
}

hx_os_status hx_os_tls_local_address(
	hx_os_tls *tls, hx_os_address *out, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);

	if (status != HX_OS_OK)
		return status;
	if (!out)
		status = HX_OS_INVALID;
	else if (!tls->ready)
		status = tls->error.status ? hx_tls_error(error, &tls->error)
					   : HX_OS_WOULD_BLOCK;
	else
		*out = tls->local;

	return hx_tls_leave(tls, NULL, status, error);
}

hx_os_status hx_os_tls_accept_until(hx_os_tls *tls, hx_os_deadline deadline,
	uint64_t *out, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	uint64_t generation;
	size_t i;

	if (status != HX_OS_OK)
		return status;
	generation = tls->cancelled;
	if (!out || !tls->server)
		status = HX_OS_INVALID;
	if (out)
		*out = 0;
	while (status == HX_OS_OK) {
		if (tls->error.status) {
			status = hx_tls_error(error, &tls->error);
			break;
		}
		if (tls->closing) {
			status = HX_OS_CLOSED;
			break;
		}
		for (i = 0; i < tls->options.max_peers; ++i) {
			hx_tls_peer *peer = tls->peers + i;

			if (peer->ready && !peer->announced && !peer->release) {
				peer->announced = 1;
				*out = peer->info.id;

				return hx_tls_leave(tls, NULL, HX_OS_OK, error);
			}
		}
		status = hx_tls_wait(tls, generation, deadline, error);
	}

	return hx_tls_leave(tls, NULL, status, error);
}

static hx_os_status hx_tls_io_status(
	hx_os_tls *tls, hx_tls_peer *peer, int writing, hx_os_error *error)
{
	if (tls->closing || peer->closing || peer->release)
		return HX_OS_CLOSED;
	if (peer->error.status) {
		if (writing && peer->error.status == HX_OS_EOF)
			return HX_OS_CLOSED;

		return hx_tls_error(error, &peer->error);
	}
	if (!peer->ready)
		return HX_OS_WOULD_BLOCK;

	return HX_OS_OK;
}

hx_os_status hx_os_tls_send_until(hx_os_tls *tls, uint64_t id, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *accepted,
	hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	hx_tls_peer *peer;
	size_t done = 0;
	uint64_t generation, target;
	int admitted = 0;

	if (accepted)
		*accepted = 0;
	if (status != HX_OS_OK)
		return status;
	peer = hx_tls_peer_get(tls, id);
	if (!peer || !accepted || (!data && bytes))
		return hx_tls_leave(tls, peer,
			peer ? HX_OS_INVALID : HX_OS_NOT_FOUND, error);
	generation = tls->cancelled;
	while (status == HX_OS_OK) {
		status = hx_tls_io_status(tls, peer, 1, error);
		if (status != HX_OS_OK || !peer->sender)
			break;
		status = hx_tls_wait(tls, generation, deadline, error);
	}
	if (status == HX_OS_OK) {
		peer->sender = admitted = 1;
		if (bytes > UINT64_MAX - peer->accepted)
			status = HX_OS_LIMIT;
	}
	while (status == HX_OS_OK && done < bytes) {
		size_t count = hx_tls_i_ring_write(&peer->tx,
			tls->options.queue_capacity,
			(const unsigned char *)data + done, bytes - done);

		done += count;
		peer->accepted += count;
		hx_tls_i_wake(tls);
		if (done == bytes)
			break;
		status = hx_tls_wait(tls, generation, deadline, error);
		if (status == HX_OS_OK)
			status = hx_tls_io_status(tls, peer, 1, error);
	}
	target = peer->accepted;
	while (status == HX_OS_OK && peer->flushed < target) {
		status = hx_tls_io_status(tls, peer, 1, error);
		if (status == HX_OS_OK)
			status = hx_tls_wait(tls, generation, deadline, error);
	}
	if (admitted)
		peer->sender = 0;
	*accepted = done;

	return hx_tls_leave(tls, peer, status, error);
}

hx_os_status hx_os_tls_flush_until(hx_os_tls *tls, uint64_t id,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	hx_tls_peer *peer;
	uint64_t generation, target;

	if (status != HX_OS_OK)
		return status;
	peer = hx_tls_peer_get(tls, id);
	if (!peer)
		return hx_tls_leave(tls, NULL, HX_OS_NOT_FOUND, error);
	target = peer->accepted;
	generation = tls->cancelled;
	while (status == HX_OS_OK && peer->flushed < target) {
		status = hx_tls_io_status(tls, peer, 1, error);
		if (status == HX_OS_OK)
			status = hx_tls_wait(tls, generation, deadline, error);
	}

	return hx_tls_leave(tls, peer, status, error);
}

static hx_os_status hx_tls_receive(hx_os_tls *tls, uint64_t id, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *received, int exact,
	hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	hx_tls_peer *peer;
	size_t done = 0;
	uint64_t generation;

	if (received)
		*received = 0;
	if (status != HX_OS_OK)
		return status;
	peer = hx_tls_peer_get(tls, id);
	if (!peer || !received || (!data && bytes))
		return hx_tls_leave(tls, peer,
			peer ? HX_OS_INVALID : HX_OS_NOT_FOUND, error);
	if (peer->receiver)
		return hx_tls_leave(tls, peer, HX_OS_BUSY, error);
	peer->receiver = 1;
	generation = tls->cancelled;
	while (status == HX_OS_OK && done < bytes) {
		if (tls->closing || peer->closing) {
			status = HX_OS_CLOSED;
			break;
		}
		done += hx_tls_i_ring_read(&peer->rx,
			tls->options.queue_capacity,
			(unsigned char *)data + done, bytes - done);
		hx_tls_i_wake(tls);
		if (done == bytes || (done && !exact))
			break;
		status = hx_tls_io_status(tls, peer, 0, error);
		if (status == HX_OS_OK)
			status = hx_tls_wait(tls, generation, deadline, error);
	}
	peer->receiver = 0;
	*received = done;
	if (exact && status == HX_OS_EOF)
		status = HX_OS_TRUNCATED_INPUT;

	return hx_tls_leave(tls, peer, status, error);
}

hx_os_status hx_os_tls_receive_until(hx_os_tls *tls, uint64_t peer, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *received,
	hx_os_error *error)
{
	return hx_tls_receive(
		tls, peer, data, bytes, deadline, received, 0, error);
}

hx_os_status hx_os_tls_receive_exact_until(hx_os_tls *tls, uint64_t peer,
	void *data, size_t bytes, hx_os_deadline deadline, size_t *received,
	hx_os_error *error)
{
	return hx_tls_receive(
		tls, peer, data, bytes, deadline, received, 1, error);
}

hx_os_status hx_os_tls_cancel_waits(hx_os_tls *tls, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);

	if (status != HX_OS_OK)
		return status;
	++tls->cancelled;

	return hx_tls_leave(tls, NULL, HX_OS_OK, error);
}

hx_os_status hx_os_tls_get_peer_info(hx_os_tls *tls, uint64_t id,
	hx_os_tls_peer_info *out, size_t capacity, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	hx_tls_peer *peer;

	if (status != HX_OS_OK)
		return status;
	peer = hx_tls_peer_get(tls, id);
	if (!out || !peer)
		status = peer ? HX_OS_INVALID : HX_OS_NOT_FOUND;
	else if (capacity < sizeof(*out))
		status = HX_OS_OUTPUT_FULL;
	else if (!peer->ready)
		status = peer->error.status ? hx_tls_error(error, &peer->error)
					    : HX_OS_WOULD_BLOCK;
	else
		*out = peer->info;

	return hx_tls_leave(tls, peer, status, error);
}

static hx_os_status hx_tls_peer_close(hx_os_tls *tls, uint64_t id,
	hx_os_deadline deadline, int aborting, hx_os_error *error)
{
	hx_os_status status = hx_tls_enter(tls, error);
	hx_tls_peer *peer;
	uint64_t generation;

	if (status != HX_OS_OK)
		return status;
	peer = hx_tls_peer_get(tls, id);
	if (!peer)
		return hx_tls_leave(tls, NULL, HX_OS_NOT_FOUND, error);
	generation = tls->cancelled;
	if (peer->closing < aborting)
		peer->closing = aborting;
	hx_tls_i_wake(tls);
	while (status == HX_OS_OK && !peer->retired)
		status = hx_tls_wait(tls, generation, deadline, error);
	if (status == HX_OS_OK) {
		peer->release = 1;
		if (peer->error.status < 0 &&
			peer->error.status != HX_OS_CANCELLED)
			status = hx_tls_error(error, &peer->error);
	}

	return hx_tls_leave(tls, peer, status, error);
}

hx_os_status hx_os_tls_peer_close_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_peer_close(tls, peer, deadline, 1, error);
}

hx_os_status hx_os_tls_peer_abort_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_peer_close(tls, peer, deadline, 2, error);
}

static hx_os_status hx_tls_close(hx_os_tls **owner, hx_os_deadline deadline,
	int aborting, hx_os_error *error)
{
	hx_os_tls *tls;
	hx_os_status status;
	hx_os_error saved;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	tls = *owner;
	if (!tls)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_tls_i_reentrant(tls))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	hx_tls_i_lock(tls);
	if (tls->close_caller) {
		hx_tls_i_unlock(tls);

		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}
	tls->close_caller = 1;
	if (tls->closing < aborting)
		tls->closing = aborting;
	hx_tls_i_wake(tls);
	hx_tls_i_unlock(tls);
	status = hx_os_thread_join_until(tls->worker, deadline, NULL, error);
	if (status != HX_OS_OK) {
		hx_tls_i_lock(tls);
		tls->close_caller = 0;
		hx_tls_i_unlock(tls);

		return status;
	}
	(void)hx_os_thread_destroy(&tls->worker, NULL);
	saved = tls->error;
	*owner = NULL;
	hx_tls_i_guard(tls);
	hx_tls_i_dispose(tls);
	hx_tls_i_guard(NULL);

	return hx_tls_error(error, &saved);
}

hx_os_status hx_os_tls_close_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_close(tls, deadline, 1, error);
}

hx_os_status hx_os_tls_abort_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_close(tls, deadline, 2, error);
}
#endif
