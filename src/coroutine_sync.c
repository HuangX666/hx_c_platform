#include "coroutine_private.h"

enum {
	HX_CO_MUTEX,
	HX_CO_SEMAPHORE,
	HX_CO_EVENT,
	HX_CO_RWLOCK,
	HX_CO_CONDITION
};
enum { HX_CO_ACQUIRE, HX_CO_READ, HX_CO_WRITE, HX_CO_COND_WAIT };

struct hx_os_coro_sync {
	hx_os_atomic references;
	hx_os_coro_runtime *runtime;
	hx_os_coro_sync_waiter *first;
	hx_os_coro_sync_waiter *last;
	uint64_t owner;
	size_t count;
	size_t maximum;
	int owner_coroutine;
	int kind;
	int closed;
	int manual;
	int pumping;
};

struct hx_os_coro_sync_waiter {
	hx_os_coro_sync *origin;
	hx_os_coro_sync *mutex;
	hx_os_coro_sync *queue;
	hx_os_coro_sync_waiter *previous;
	hx_os_coro_sync_waiter *next;
	hx_os_coro *coroutine;
	hx_os_error result;
	uint64_t owner;
	int owner_coroutine;
	int operation;
	int relocking;
	int done;
};

static void hx_os_coro_sync_pump(hx_os_coro_sync *sync);

static uint64_t hx_os_coro_sync_identity(int *coroutine)
{
	hx_os_coro *self = hx_os_coro_current();
	*coroutine = self != NULL;
	return self ? self->identity : hx_os_thread_id();
}

static int hx_os_coro_sync_owned(
	hx_os_coro_sync *sync, uint64_t owner, int coroutine)
{
	return sync->owner && sync->owner == owner &&
		sync->owner_coroutine == coroutine;
}

void hx_os_coro_sync_retain(hx_os_coro_sync *sync)
{
	if (sync)
		(void)hx_os_atomic_fetch_add(
			&sync->references, 1, HX_OS_RELAXED);
}

void hx_os_coro_sync_release(hx_os_coro_sync *sync)
{
	hx_os_coro_runtime *runtime;
	if (!sync ||
		hx_os_atomic_fetch_sub(&sync->references, 1, HX_OS_ACQ_REL) !=
			1)
		return;
	runtime = sync->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	--runtime->channel_count;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	hx_os_i_coro_sync_free(runtime, sync);
	hx_os_i_coro_runtime_release(runtime);
}

static hx_os_status hx_os_coro_sync_create(hx_os_coro_runtime *runtime,
	int kind, size_t count, size_t maximum, int manual,
	hx_os_coro_sync **out, hx_os_error *error)
{
	hx_os_coro_sync *sync;
	hx_os_error detail;
	hx_os_status status;
	if (out)
		*out = NULL;
	if (!out || !runtime || !maximum || count > maximum)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	status = runtime->closing ? HX_OS_CLOSED
		: runtime->channel_count == runtime->options.max_channels
		? HX_OS_LIMIT
		: HX_OS_OK;
	if (status == HX_OS_OK) {
		++runtime->channel_count;
		hx_os_i_coro_runtime_retain(runtime);
	}
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_error_clear(&detail);
	sync = hx_os_i_coro_sync_alloc(runtime, sizeof(*sync), 0, &detail);
	if (!sync) {
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		--runtime->channel_count;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		hx_os_i_coro_runtime_release(runtime);
		if (error)
			*error = detail;
		return detail.status;
	}
	memset(sync, 0, sizeof(*sync));
	sync->runtime = runtime;
	sync->kind = kind;
	sync->count = count;
	sync->maximum = maximum;
	sync->manual = manual;
	hx_os_atomic_store(&sync->references, 1, HX_OS_RELAXED);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	status = runtime->closing ? HX_OS_CLOSED : HX_OS_OK;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK) {
		hx_os_coro_sync_release(sync);
		return hx_os_i_error(error, status, 0, 0);
	}
	*out = sync;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_coro_sync_unlink(hx_os_coro_sync_waiter *waiter)
{
	hx_os_coro_sync *sync = waiter->queue;
	if (!sync)
		return;
	if (waiter->previous)
		waiter->previous->next = waiter->next;
	else
		sync->first = waiter->next;
	if (waiter->next)
		waiter->next->previous = waiter->previous;
	else
		sync->last = waiter->previous;
	waiter->queue = NULL;
	waiter->previous = waiter->next = NULL;
}

static void hx_os_coro_sync_link(
	hx_os_coro_sync *sync, hx_os_coro_sync_waiter *waiter)
{
	waiter->queue = sync;
	waiter->previous = sync->last;
	if (sync->last)
		sync->last->next = waiter;
	else
		sync->first = waiter;
	sync->last = waiter;
}

static int hx_os_coro_sync_grant(hx_os_coro_sync *sync, int operation,
	uint64_t owner, int owner_coroutine)
{
	if (sync->kind == HX_CO_MUTEX ||
		(sync->kind == HX_CO_RWLOCK && operation == HX_CO_WRITE)) {
		if (sync->owner || sync->count)
			return 0;
		sync->owner = owner;
		sync->owner_coroutine = owner_coroutine;
		return 1;
	}
	if (sync->kind == HX_CO_RWLOCK) {
		if (sync->owner || sync->count == SIZE_MAX)
			return 0;
		++sync->count;
		return 1;
	}
	if (sync->kind == HX_CO_SEMAPHORE || sync->kind == HX_CO_EVENT) {
		if (!sync->count)
			return 0;
		if (!sync->manual)
			--sync->count;
		return 1;
	}
	return 0;
}

static int hx_os_coro_sync_finish(
	hx_os_coro_sync_waiter *waiter, hx_os_error *result)
{
	hx_os_coro_sync *queue = waiter->queue;
	if (waiter->relocking && result->status != HX_OS_OK)
		return 0;
	hx_os_coro_sync_unlink(waiter);
	if (waiter->operation == HX_CO_COND_WAIT) {
		waiter->operation = HX_CO_ACQUIRE;
		waiter->relocking = 1;
		waiter->result = *result;
		if (waiter->mutex->first ||
			!hx_os_coro_sync_grant(waiter->mutex, HX_CO_ACQUIRE,
				waiter->owner, waiter->owner_coroutine)) {
			hx_os_coro_sync_link(waiter->mutex, waiter);
			return 0;
		}
	}
	if (waiter->relocking)
		*result = waiter->result;
	waiter->result = *result;
	waiter->done = 1;
	if (queue)
		hx_os_coro_sync_pump(queue);
	return 1;
}

int hx_os_i_coro_sync_finish(hx_os_coro *coroutine, hx_os_error *result)
{
	return hx_os_coro_sync_finish(coroutine->sync_wait, result);
}

static void hx_os_coro_sync_complete_error(
	hx_os_coro_sync_waiter *waiter, const hx_os_error *result)
{
	hx_os_error error = *result;
	if (waiter->coroutine)
		hx_os_i_coro_finish_wait(waiter->coroutine, &error);
	else if (hx_os_coro_sync_finish(waiter, &error))
		hx_os_condition_notify_all(waiter->origin->runtime->condition);
}
static void hx_os_coro_sync_complete(
	hx_os_coro_sync_waiter *waiter, hx_os_status status)
{
	hx_os_error error;
	(void)hx_os_i_error(&error, status, 0, 0);
	hx_os_coro_sync_complete_error(waiter, &error);
}

static void hx_os_coro_sync_pump(hx_os_coro_sync *sync)
{
	if (sync->pumping || sync->kind == HX_CO_CONDITION)
		return;
	sync->pumping = 1;
	while (sync->first) {
		hx_os_coro_sync_waiter *waiter = sync->first;
		if (sync->closed && !waiter->relocking) {
			hx_os_coro_sync_complete(waiter, HX_OS_CLOSED);
			continue;
		}
		if (!hx_os_coro_sync_grant(sync, waiter->operation,
			    waiter->owner, waiter->owner_coroutine))
			break;
		hx_os_coro_sync_complete(waiter, HX_OS_OK);
	}
	sync->pumping = 0;
}

static void hx_os_coro_sync_dispose(hx_os_coro_sync_waiter *waiter)
{
	hx_os_coro_sync *sync = waiter->origin;
	hx_os_coro_sync *mutex = waiter->mutex;
	hx_os_i_coro_sync_free(sync->runtime, waiter);
	hx_os_coro_sync_release(mutex);
	hx_os_coro_sync_release(sync);
}

void hx_os_i_coro_sync_dispose(hx_os_coro *coroutine)
{
	hx_os_coro_sync_waiter *waiter = coroutine->sync_wait;
	coroutine->sync_wait = NULL;
	if (waiter)
		hx_os_coro_sync_dispose(waiter);
}

static hx_os_status hx_os_coro_sync_wait(hx_os_coro_sync *sync, int kind,
	int operation, hx_os_coro_sync *mutex, hx_os_deadline deadline,
	hx_os_error *error)
{
	hx_os_coro *self = hx_os_coro_current();
	hx_os_coro_sync_waiter *waiter;
	hx_os_coro_runtime *runtime;
	hx_os_status status = HX_OS_OK;
	hx_os_error detail;
	int owner_coroutine;
	uint64_t owner = hx_os_coro_sync_identity(&owner_coroutine);
	if (!sync || sync->kind != kind ||
		(mutex &&
			(mutex->kind != HX_CO_MUTEX ||
				mutex->runtime != sync->runtime)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	runtime = sync->runtime;
	if (self && self->runtime != runtime)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (self && hx_os_i_coro_wait_ready(self, HX_OS_CO_WAIT_SYNC)) {
		if (self->sync_wait->origin != sync ||
			self->sync_wait->mutex != mutex)
			return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
		return hx_os_i_coro_consume(self, error);
	}
	if (self && (self->preparing || self->wait_kind != HX_OS_CO_WAIT_NONE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (sync->closed || runtime->closed || runtime->closing == 2)
		status = HX_OS_CLOSED;
	else if (self && hx_os_coro_cancel_requested(self))
		status = HX_OS_CANCELLED;
	else if (mutex && !hx_os_coro_sync_owned(mutex, owner, owner_coroutine))
		status = HX_OS_INVALID;
	else if ((kind == HX_CO_MUTEX || kind == HX_CO_RWLOCK) &&
		hx_os_coro_sync_owned(sync, owner, owner_coroutine))
		status = HX_OS_DEADLOCK;
	else if (!mutex && !sync->first &&
		hx_os_coro_sync_grant(
			sync, operation, owner, owner_coroutine)) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	if (status == HX_OS_OK && !self && deadline != HX_OS_DEADLINE_NOW &&
		(hx_os_i_coro_worker_inside() || hx_os_i_coro_io_inside()))
		status = HX_OS_DEADLOCK;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_error_clear(&detail);
	if (self)
		++self->preparing;
	waiter = hx_os_i_coro_sync_alloc(runtime, sizeof(*waiter), 0, &detail);
	if (self)
		--self->preparing;
	if (!waiter) {
		if (error)
			*error = detail;
		return detail.status;
	}
	memset(waiter, 0, sizeof(*waiter));
	waiter->origin = sync;
	waiter->mutex = mutex;
	waiter->coroutine = self;
	waiter->operation = operation;
	waiter->owner = owner;
	waiter->owner_coroutine = owner_coroutine;
	hx_os_coro_sync_retain(sync);
	hx_os_coro_sync_retain(mutex);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (sync->closed || runtime->closed || runtime->closing == 2)
		status = hx_os_i_error(&detail, HX_OS_CLOSED, 0, 0);
	else if (mutex && !hx_os_coro_sync_owned(mutex, owner, owner_coroutine))
		status = hx_os_i_error(&detail, HX_OS_INVALID, 0, 0);
	else if (self)
		status = hx_os_i_coro_begin_wait(
			self, HX_OS_CO_WAIT_SYNC, deadline, &detail);
	if (status != HX_OS_OK) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		hx_os_coro_sync_dispose(waiter);
		if (error)
			*error = detail;
		return status;
	}
	if (self)
		self->sync_wait = waiter;
	hx_os_coro_sync_link(sync, waiter);
	if (mutex) {
		mutex->owner = 0;
		hx_os_coro_sync_pump(mutex);
	}
	else
		hx_os_coro_sync_pump(sync);
	if (!waiter->done && hx_os_deadline_expired(deadline))
		hx_os_coro_sync_complete(waiter,
			deadline == HX_OS_DEADLINE_NOW ? HX_OS_WOULD_BLOCK
						       : HX_OS_TIMEOUT);
	if (self) {
		int done = waiter->done;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		if (!done) {
			status = hx_os_i_coro_suspend(self, error);
			if (status != HX_OS_OK)
				return status;
		}
		return hx_os_i_coro_consume(self, error);
	}
	while (!waiter->done) {
		if (!waiter->relocking &&
			(runtime->closing == 2 || runtime->closed)) {
			hx_os_coro_sync_complete(waiter, HX_OS_CLOSED);
			continue;
		}
		status = hx_os_condition_wait_until(runtime->condition,
			runtime->mutex,
			waiter->relocking ? HX_OS_DEADLINE_INFINITE : deadline,
			&detail);
		if (status != HX_OS_OK && !waiter->done && !waiter->relocking) {
			if (status == HX_OS_TIMEOUT &&
				deadline == HX_OS_DEADLINE_NOW)
				status = HX_OS_WOULD_BLOCK;
			detail.status = status;
			hx_os_coro_sync_complete_error(waiter, &detail);
		}
	}
	detail = waiter->result;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	hx_os_coro_sync_dispose(waiter);
	if (error)
		*error = detail;
	return detail.status;
}

hx_os_status hx_os_coro_sync_close(hx_os_coro_sync *sync, hx_os_error *error)
{
	hx_os_coro_sync_waiter *waiter, *next;
	if (!sync)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(sync->runtime->mutex, NULL);
	sync->closed = 1;
	for (waiter = sync->first; waiter; waiter = next) {
		next = waiter->next;
		hx_os_coro_sync_complete(waiter, HX_OS_CLOSED);
	}
	(void)hx_os_mutex_unlock(sync->runtime->mutex, NULL);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_coro_mutex_create(
	hx_os_coro_runtime *runtime, hx_os_coro_mutex **out, hx_os_error *error)
{
	return hx_os_coro_sync_create(
		runtime, HX_CO_MUTEX, 0, 1, 0, out, error);
}
hx_os_status hx_os_coro_mutex_lock_until(
	hx_os_coro_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_sync_wait(
		mutex, HX_CO_MUTEX, HX_CO_ACQUIRE, NULL, deadline, error);
}
static hx_os_status hx_os_coro_sync_unlock(
	hx_os_coro_sync *sync, int kind, int reader, hx_os_error *error)
{
	int coroutine;
	uint64_t owner = hx_os_coro_sync_identity(&coroutine);
	hx_os_status status = HX_OS_OK;
	if (!sync || sync->kind != kind)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_coro_current() &&
		hx_os_coro_current_runtime() != sync->runtime)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	(void)hx_os_mutex_lock(sync->runtime->mutex, NULL);
	if (reader ? !sync->count
		   : !hx_os_coro_sync_owned(sync, owner, coroutine))
		status = HX_OS_INVALID;
	else {
		if (reader)
			--sync->count;
		else
			sync->owner = 0;
		hx_os_coro_sync_pump(sync);
	}
	(void)hx_os_mutex_unlock(sync->runtime->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}
hx_os_status hx_os_coro_mutex_unlock(
	hx_os_coro_mutex *mutex, hx_os_error *error)
{
	return hx_os_coro_sync_unlock(mutex, HX_CO_MUTEX, 0, error);
}
hx_os_status hx_os_coro_semaphore_create(hx_os_coro_runtime *runtime,
	size_t initial, size_t maximum, hx_os_coro_semaphore **out,
	hx_os_error *error)
{
	return hx_os_coro_sync_create(
		runtime, HX_CO_SEMAPHORE, initial, maximum, 0, out, error);
}
hx_os_status hx_os_coro_semaphore_wait_until(hx_os_coro_semaphore *semaphore,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_sync_wait(semaphore, HX_CO_SEMAPHORE, HX_CO_ACQUIRE,
		NULL, deadline, error);
}
hx_os_status hx_os_coro_semaphore_post(
	hx_os_coro_semaphore *semaphore, size_t count, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;
	if (!semaphore || semaphore->kind != HX_CO_SEMAPHORE || !count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(semaphore->runtime->mutex, NULL);
	if (semaphore->closed)
		status = HX_OS_CLOSED;
	else if (count > semaphore->maximum - semaphore->count)
		status = HX_OS_LIMIT;
	else {
		semaphore->count += count;
		hx_os_coro_sync_pump(semaphore);
	}
	(void)hx_os_mutex_unlock(semaphore->runtime->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}
hx_os_status hx_os_coro_event_create(hx_os_coro_runtime *runtime,
	int manual_reset, int initially_set, hx_os_coro_event **out,
	hx_os_error *error)
{
	return hx_os_coro_sync_create(runtime, HX_CO_EVENT, initially_set != 0,
		1, manual_reset != 0, out, error);
}
hx_os_status hx_os_coro_event_wait_until(
	hx_os_coro_event *event, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_sync_wait(
		event, HX_CO_EVENT, HX_CO_ACQUIRE, NULL, deadline, error);
}
static hx_os_status hx_os_coro_event_update(
	hx_os_coro_event *event, int value, hx_os_error *error)
{
	hx_os_status status;
	if (!event || event->kind != HX_CO_EVENT)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(event->runtime->mutex, NULL);
	status = event->closed ? HX_OS_CLOSED : HX_OS_OK;
	if (status == HX_OS_OK) {
		event->count = (size_t)value;
		if (value)
			hx_os_coro_sync_pump(event);
	}
	(void)hx_os_mutex_unlock(event->runtime->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}
hx_os_status hx_os_coro_event_set(hx_os_coro_event *event, hx_os_error *error)
{
	return hx_os_coro_event_update(event, 1, error);
}
hx_os_status hx_os_coro_event_reset(hx_os_coro_event *event, hx_os_error *error)
{
	return hx_os_coro_event_update(event, 0, error);
}
hx_os_status hx_os_coro_rwlock_create(hx_os_coro_runtime *runtime,
	hx_os_coro_rwlock **out, hx_os_error *error)
{
	return hx_os_coro_sync_create(
		runtime, HX_CO_RWLOCK, 0, SIZE_MAX, 0, out, error);
}
hx_os_status hx_os_coro_rwlock_read_until(
	hx_os_coro_rwlock *lock, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_sync_wait(
		lock, HX_CO_RWLOCK, HX_CO_READ, NULL, deadline, error);
}
hx_os_status hx_os_coro_rwlock_write_until(
	hx_os_coro_rwlock *lock, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_sync_wait(
		lock, HX_CO_RWLOCK, HX_CO_WRITE, NULL, deadline, error);
}
hx_os_status hx_os_coro_rwlock_read_unlock(
	hx_os_coro_rwlock *lock, hx_os_error *error)
{
	return hx_os_coro_sync_unlock(lock, HX_CO_RWLOCK, 1, error);
}
hx_os_status hx_os_coro_rwlock_write_unlock(
	hx_os_coro_rwlock *lock, hx_os_error *error)
{
	return hx_os_coro_sync_unlock(lock, HX_CO_RWLOCK, 0, error);
}
hx_os_status hx_os_coro_condition_create(hx_os_coro_runtime *runtime,
	hx_os_coro_condition **out, hx_os_error *error)
{
	return hx_os_coro_sync_create(
		runtime, HX_CO_CONDITION, 0, 1, 0, out, error);
}
hx_os_status hx_os_coro_condition_wait_until(hx_os_coro_condition *condition,
	hx_os_coro_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	if (!mutex)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_coro_sync_wait(condition, HX_CO_CONDITION, HX_CO_COND_WAIT,
		mutex, deadline, error);
}
static hx_os_status hx_os_coro_condition_notify(
	hx_os_coro_condition *condition, int all, hx_os_error *error)
{
	if (!condition || condition->kind != HX_CO_CONDITION)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	(void)hx_os_mutex_lock(condition->runtime->mutex, NULL);
	do {
		if (!condition->first)
			break;
		hx_os_coro_sync_complete(condition->first, HX_OS_OK);
	} while (all);
	(void)hx_os_mutex_unlock(condition->runtime->mutex, NULL);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_coro_condition_notify_one(
	hx_os_coro_condition *condition, hx_os_error *error)
{
	return hx_os_coro_condition_notify(condition, 0, error);
}
hx_os_status hx_os_coro_condition_notify_all(
	hx_os_coro_condition *condition, hx_os_error *error)
{
	return hx_os_coro_condition_notify(condition, 1, error);
}
