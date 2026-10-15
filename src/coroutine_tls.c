#include "coroutine_private.h"
#include "tls_private.h"
#include "hx_os_coro_tls.h"

enum { HX_CO_TLS_READY, HX_CO_TLS_SEND, HX_CO_TLS_RECEIVE, HX_CO_TLS_FLUSH };

#if HX_OS_HAVE_TLS && HX_OS_ENABLE_COROUTINES
typedef struct hx_tls_coro_wait {
	struct hx_tls_coro_wait *next;
	hx_os_coro *coroutine;
	hx_os_tls *tls;
	void *data;
	uint64_t peer, generation;
	hx_os_deadline deadline;
	size_t bytes, completed;
	int kind;
} hx_tls_coro_wait;

void hx_tls_i_coro_notify(hx_os_tls *tls)
{
	hx_tls_coro_wait *wait;

	// All call sites hold the TLS mutex; scheduler cleanup takes it
	// unlocked.
	for (wait = tls->coroutine_waiters; wait; wait = wait->next) {
		hx_os_error ready = {0};
		hx_os_coro *coroutine = wait->coroutine;

		if (wait->generation != tls->cancelled)
			ready.status = HX_OS_CANCELLED;
		(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
		if (coroutine->wait_kind == HX_OS_CO_WAIT_TLS &&
			coroutine->tls_wait == wait)
			hx_os_i_coro_finish_wait(coroutine, &ready);
		(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
	}
}

void hx_os_i_coro_tls_dispose(hx_os_coro *coroutine)
{
	hx_tls_coro_wait *wait = coroutine->tls_wait;
	hx_tls_coro_wait **cursor;

	if (!wait)
		return;
	coroutine->tls_wait = NULL;
	hx_tls_i_lock(wait->tls);
	cursor = (hx_tls_coro_wait **)&wait->tls->coroutine_waiters;
	while (*cursor && *cursor != wait)
		cursor = &(*cursor)->next;
	if (*cursor)
		*cursor = wait->next;
	--wait->tls->users;
	hx_tls_i_wake(wait->tls);
	hx_tls_i_unlock(wait->tls);
	hx_os_memory_free(wait);
}

static hx_os_status hx_tls_coro_try(hx_os_tls *tls, int kind, uint64_t peer,
	void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	*completed = 0;
	switch (kind) {
	case HX_CO_TLS_READY:
		return hx_os_tls_wait_ready_until(
			tls, HX_OS_DEADLINE_NOW, error);
	case HX_CO_TLS_SEND:
		return hx_os_tls_send_until(tls, peer, data, bytes,
			HX_OS_DEADLINE_NOW, completed, error);
	case HX_CO_TLS_RECEIVE:
		return hx_os_tls_receive_until(tls, peer, data, bytes,
			HX_OS_DEADLINE_NOW, completed, error);
	default:
		return hx_os_tls_flush_until(
			tls, peer, HX_OS_DEADLINE_NOW, error);
	}
}

static void hx_tls_coro_lease(hx_os_tls *tls, int acquire)
{
	hx_tls_i_lock(tls);
	if (acquire)
		++tls->users;
	else {
		--tls->users;
		hx_tls_i_wake(tls);
	}
	hx_tls_i_unlock(tls);
}
#else
#if HX_OS_HAVE_TLS
void hx_tls_i_coro_notify(hx_os_tls *tls)
{
	(void)tls;
}
#endif
void hx_os_i_coro_tls_dispose(hx_os_coro *coroutine)
{
	(void)coroutine;
}
#endif

static hx_os_status hx_tls_coro_call(hx_os_tls *tls, int kind, uint64_t peer,
	void *data, size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	if (completed)
		*completed = 0;
#if HX_OS_HAVE_TLS && HX_OS_ENABLE_COROUTINES
	{
		hx_os_coro *coroutine = hx_os_coro_current();
		hx_tls_coro_wait *wait;
		hx_os_status status;
		size_t total = 0, count;
		uint64_t sequence, generation;

		if (!coroutine || !tls ||
			((kind == HX_CO_TLS_SEND ||
				 kind == HX_CO_TLS_RECEIVE) &&
				(!completed || (!data && bytes))))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		hx_tls_i_lock(tls);
		++tls->users;
		generation = tls->cancelled;
		hx_tls_i_unlock(tls);
		if (hx_os_i_coro_wait_ready(coroutine, HX_OS_CO_WAIT_TLS)) {
			wait = coroutine->tls_wait;
			if (wait->tls != tls || wait->kind != kind ||
				wait->peer != peer || wait->data != data ||
				wait->bytes != bytes ||
				wait->deadline != deadline) {
				status = hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
				goto finish;
			}
			total = wait->completed;
			generation = wait->generation;
			status = hx_os_i_coro_consume(coroutine, error);
			if (status != HX_OS_OK)
				goto finish;
		}
		if (coroutine->wait_kind != HX_OS_CO_WAIT_NONE) {
			status = hx_os_i_error(error, HX_OS_BUSY, 0, 0);
			goto finish;
		}
		for (;;) {
			if (hx_os_coro_cancel_requested(coroutine)) {
				status = hx_os_i_error(
					error, HX_OS_CANCELLED, 0, 0);
				break;
			}
			hx_tls_i_lock(tls);
			sequence = tls->change_sequence;
			status = generation == tls->cancelled ? HX_OS_OK
							      : HX_OS_CANCELLED;
			hx_tls_i_unlock(tls);
			if (status != HX_OS_OK) {
				(void)hx_os_i_error(error, status, 0, 0);
				break;
			}
			status = hx_tls_coro_try(tls, kind, peer,
				data ? (unsigned char *)data + total : NULL,
				bytes - total, &count, error);
			total += count;
			if (status != HX_OS_TIMEOUT ||
				hx_os_deadline_expired(deadline))
				break;
			wait = hx_os_i_zero(
				coroutine->runtime->context, sizeof(*wait));
			if (!wait) {
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
				break;
			}
			wait->coroutine = coroutine;
			wait->tls = tls;
			wait->kind = kind;
			wait->peer = peer;
			wait->data = data;
			wait->bytes = bytes;
			wait->completed = total;
			wait->deadline = deadline;
			hx_tls_i_lock(tls);
			if (sequence != tls->change_sequence) {
				hx_tls_i_unlock(tls);
				hx_os_memory_free(wait);
				continue;
			}
			(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
			status = hx_os_i_coro_begin_wait(
				coroutine, HX_OS_CO_WAIT_TLS, deadline, error);
			if (status == HX_OS_OK) {
				wait->generation = tls->cancelled;
				wait->next = tls->coroutine_waiters;
				tls->coroutine_waiters = wait;
				++tls->users;
				coroutine->tls_wait = wait;
			}
			(void)hx_os_mutex_unlock(
				coroutine->runtime->mutex, NULL);
			hx_tls_i_unlock(tls);
			if (status != HX_OS_OK) {
				hx_os_memory_free(wait);
				break;
			}
			hx_tls_coro_lease(tls, 0);
			status = hx_os_i_coro_suspend(coroutine, error);
			if (status != HX_OS_OK)
				return status;
			hx_tls_coro_lease(tls, 1);
			status = hx_os_i_coro_consume(coroutine, error);
			if (status != HX_OS_OK)
				break;
		}
	finish:
		if (completed)
			*completed = total;
		hx_tls_coro_lease(tls, 0);
		return status;
	}
#else
	(void)tls;
	(void)kind;
	(void)peer;
	(void)data;
	(void)bytes;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_coro_tls_wait_ready(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_coro_call(
		tls, HX_CO_TLS_READY, 0, NULL, 0, deadline, NULL, error);
}

hx_os_status hx_os_coro_tls_send_until(hx_os_tls *tls, uint64_t peer,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	return hx_tls_coro_call(tls, HX_CO_TLS_SEND, peer, (void *)data, bytes,
		deadline, completed, error);
}

hx_os_status hx_os_coro_tls_receive_until(hx_os_tls *tls, uint64_t peer,
	void *data, size_t capacity, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	return hx_tls_coro_call(tls, HX_CO_TLS_RECEIVE, peer, data, capacity,
		deadline, completed, error);
}

hx_os_status hx_os_coro_tls_flush_until(hx_os_tls *tls, uint64_t peer,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_tls_coro_call(
		tls, HX_CO_TLS_FLUSH, peer, NULL, 0, deadline, NULL, error);
}
