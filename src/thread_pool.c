#include "thread_pool_private.h"

#if defined(_MSC_VER)
static __declspec(thread) hx_os_thread_pool *hx_os_current_pool;
#else
static __thread hx_os_thread_pool *hx_os_current_pool;
#endif

/* All queue helpers run under the existing pool mutex. */
static void hx_os_pool_ready(hx_os_thread_pool *pool, hx_os_task *task)
{
	task->next = NULL;
	if (pool->last)
		pool->last->next = task;
	else
		pool->first = task;
	pool->last = task;
}
#if HX_OS_ENABLE_DELAYED_TASKS
static void hx_os_pool_promote(hx_os_thread_pool *pool, hx_os_status failure)
{
	uint64_t now = 0;
	int promoted = 0;
	if (!pool->delayed)
		return;
	if (failure == HX_OS_OK)
		failure = hx_os_monotonic_read(&now, NULL);
	while (pool->delayed &&
		(failure != HX_OS_OK || pool->delayed->delayed->due <= now)) {
		hx_os_task *task = pool->delayed;
		pool->delayed = task->next;
		task->delayed->failure = failure;
		hx_os_pool_ready(pool, task);
		promoted = 1;
	}
	if (promoted)
		hx_os_condition_notify_all(pool->condition);
}
#endif

void hx_os_thread_pool_options_init(hx_os_thread_pool_options *options)
{
	memset(options, 0, sizeof(*options));
	options->size = sizeof(*options);
	options->version = HX_OS_ABI_VERSION;
	options->threads = 2;
	options->queue_capacity = 256;
}

static void *hx_os_pool_worker(void *user)
{
	hx_os_thread_pool *pool = user;
	hx_os_current_pool = pool;
	for (;;) {
		hx_os_task *task;
		(void)hx_os_mutex_lock(pool->mutex, NULL);
		for (;;) {
#if HX_OS_ENABLE_DELAYED_TASKS
			hx_os_status status;
			if (pool->delayed)
				hx_os_pool_promote(pool, HX_OS_OK);
			if (pool->first || (pool->closing && !pool->delayed))
				break;
			status = hx_os_condition_wait_until(pool->condition,
				pool->mutex,
				pool->delayed ? pool->delayed->delayed->due
					      : HX_OS_DEADLINE_INFINITE,
				NULL);
			if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
				hx_os_pool_promote(pool, status);
#else
			if (pool->first || pool->closing)
				break;
			(void)hx_os_condition_wait_until(pool->condition,
				pool->mutex, HX_OS_DEADLINE_INFINITE, NULL);
#endif
		}
		task = pool->first;
		if (!task) {
			(void)hx_os_mutex_unlock(pool->mutex, NULL);
			break;
		}
		pool->first = task->next;
		if (!pool->first)
			pool->last = NULL;
		--pool->queued;
		hx_os_atomic_store(
			&task->state, HX_OS_TASK_RUNNING, HX_OS_RELEASE);
		(void)hx_os_mutex_unlock(pool->mutex, NULL);
#if HX_OS_ENABLE_DELAYED_TASKS
		if (task->delayed) {
			/* Never take this lock under the pool mutex:
			 * cancellation takes delay->mutex first to pin pool
			 * lifetime.
			 */
			(void)hx_os_mutex_lock(task->delayed->mutex, NULL);
			task->delayed->pool = NULL;
			(void)hx_os_mutex_unlock(task->delayed->mutex, NULL);
		}
#endif
		task->result = hx_os_task_cancel_requested(task)
			? HX_OS_CANCELLED
#if HX_OS_ENABLE_DELAYED_TASKS
			: task->delayed && task->delayed->failure
			? task->delayed->failure
#endif
			: task->function(task, task->user);
		if (task->cleanup)
			task->cleanup(task->user);
		hx_os_atomic_store(
			&task->state, HX_OS_TASK_FINISHED, HX_OS_RELEASE);
		hx_os_event_set(task->finished);
		hx_os_task_release(task);
	}
	hx_os_current_pool = NULL;
	return NULL;
}

hx_os_status hx_os_thread_pool_create(hx_os_context *context,
	const hx_os_thread_pool_options *input, hx_os_thread_pool **out,
	hx_os_error *error)
{
	hx_os_thread_pool_options options;
	hx_os_thread_pool *pool;
	hx_os_status status;
	size_t i;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	hx_os_thread_pool_options_init(&options);
	if (input) {
		if (input->size < sizeof(*input) ||
			input->version != HX_OS_ABI_VERSION)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		options = *input;
	}
	if (!options.threads || options.threads > 1024 ||
		!options.queue_capacity ||
		options.queue_capacity > SIZE_MAX / 2)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	pool = hx_os_i_zero(context, sizeof(*pool));
	if (!pool)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	pool->context = context;
	pool->capacity = options.queue_capacity;
	pool->threads =
		hx_os_i_zero(context, options.threads * sizeof(*pool->threads));
	if (!pool->threads) {
		hx_os_memory_free(pool);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	status = hx_os_mutex_create(context, 0, &pool->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &pool->condition, error);
	if (status == HX_OS_OK) {
		for (i = 0; i < options.threads; ++i) {
			status = hx_os_thread_create(context, hx_os_pool_worker,
				pool, &pool->threads[i], error);
			if (status != HX_OS_OK)
				break;
			++pool->thread_count;
		}
	}
	if (status != HX_OS_OK) {
		/*
		 * No user task is admitted before create returns. These workers
		 * can only be in our condition wait, so rollback can join them.
		 */
		if (pool->thread_count) {
			(void)hx_os_mutex_lock(pool->mutex, NULL);
			pool->closing = 1;
			hx_os_condition_notify_all(pool->condition);
			(void)hx_os_mutex_unlock(pool->mutex, NULL);
			for (i = 0; i < pool->thread_count; ++i) {
				(void)hx_os_thread_join_until(pool->threads[i],
					HX_OS_DEADLINE_INFINITE, NULL, NULL);
				(void)hx_os_thread_destroy(
					&pool->threads[i], NULL);
			}
		}
		hx_os_condition_destroy(pool->condition);
		hx_os_mutex_destroy(pool->mutex);
		hx_os_memory_free(pool->threads);
		hx_os_memory_free(pool);
		return status;
	}
	*out = pool;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_pool_submit(hx_os_thread_pool *pool,
	hx_os_task_fn function, void *user, hx_os_task_cleanup cleanup,
	hx_os_task_prepare_fn prepare, void *factory, hx_os_task **out,
	hx_os_deadline due, hx_os_error *error)
{
	hx_os_task *task;
	hx_os_status status = HX_OS_OK;
	int prepared = 0, event_failed = 0;
	if (!pool || !function || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	if (pool->closing)
		status = HX_OS_CLOSED;
	else if (pool->queued + pool->submitting >= pool->capacity)
		status = HX_OS_LIMIT;
	if (status != HX_OS_OK) {
		(void)hx_os_mutex_unlock(pool->mutex, NULL);
		return hx_os_i_error(error, status, 0, 0);
	}
	++pool->submitting;
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
	task = hx_os_i_zero(pool->context,
		sizeof(*task)
#if HX_OS_ENABLE_DELAYED_TASKS
			+ (due ? sizeof(hx_os_delayed_task) : 0)
#endif
	);
	if (!task)
		status = HX_OS_NO_MEMORY;
	else {
		status = hx_os_event_create(
			pool->context, 1, 0, &task->finished, error);
		event_failed = status != HX_OS_OK;
	}
#if HX_OS_ENABLE_DELAYED_TASKS
	if (status == HX_OS_OK && due) {
		task->delayed = (hx_os_delayed_task *)(task + 1);
		task->delayed->due = due;
		status = hx_os_mutex_create(
			pool->context, 0, &task->delayed->mutex, error);
		event_failed = status != HX_OS_OK;
	}
#else
	(void)due;
#endif
	if (status == HX_OS_OK && prepare) {
		user = NULL;
		status = prepare(factory, &user);
		prepared = status == HX_OS_OK;
	}
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	--pool->submitting;
	if (status == HX_OS_OK && pool->closing)
		status = HX_OS_CLOSED;
	if (status == HX_OS_OK) {
		task->function = function;
		task->cleanup = cleanup;
		task->user = user;
		task->pool = pool;
		hx_os_atomic_store(&task->references, 2, HX_OS_RELAXED);
#if HX_OS_ENABLE_DELAYED_TASKS
		if (task->delayed) {
			hx_os_task **cursor = &pool->delayed;
			task->delayed->pool = pool;
			while (*cursor && (*cursor)->delayed->due <= due)
				cursor = &(*cursor)->next;
			task->next = *cursor;
			*cursor = task;
		}
		else
#endif
			hx_os_pool_ready(pool, task);
		++pool->queued;
		*out = task;
		hx_os_condition_notify_one(pool->condition);
	}
	else {
		/* Keep the pool alive across allocator callbacks on rejection.
		 */
		++pool->submitting;
	}
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
	if (status != HX_OS_OK) {
		if (prepared)
			cleanup(user);
		if (task)
			hx_os_event_destroy(task->finished);
#if HX_OS_ENABLE_DELAYED_TASKS
		if (task && task->delayed)
			hx_os_mutex_destroy(task->delayed->mutex);
#endif
		hx_os_memory_free(task);
		(void)hx_os_mutex_lock(pool->mutex, NULL);
		--pool->submitting;
		(void)hx_os_mutex_unlock(pool->mutex, NULL);
	}
	/* Keep the native event error captured before rejection cleanup. */
	if (event_failed)
		return status;
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_thread_pool_submit(hx_os_thread_pool *pool,
	hx_os_task_fn function, void *user, hx_os_task_cleanup cleanup,
	hx_os_task **out, hx_os_error *error)
{
	return hx_os_pool_submit(
		pool, function, user, cleanup, NULL, NULL, out, 0, error);
}

hx_os_status hx_os_thread_pool_submit_prepared(hx_os_thread_pool *pool,
	hx_os_task_prepare_fn prepare, void *factory, hx_os_task_fn function,
	hx_os_task_cleanup cleanup, hx_os_task **out, hx_os_error *error)
{
	if (!prepare || !cleanup) {
		if (out)
			*out = NULL;
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
	return hx_os_pool_submit(
		pool, function, NULL, cleanup, prepare, factory, out, 0, error);
}
hx_os_status hx_os_thread_pool_submit_delayed(hx_os_thread_pool *pool,
	hx_os_deadline due, hx_os_task_fn function, void *user,
	hx_os_task_cleanup cleanup, hx_os_task **out, hx_os_error *error)
{
#if HX_OS_ENABLE_DELAYED_TASKS
	if (due == HX_OS_DEADLINE_INFINITE) {
		if (out)
			*out = NULL;
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
	return hx_os_pool_submit(
		pool, function, user, cleanup, NULL, NULL, out, due, error);
#else
	(void)pool;
	(void)due;
	(void)function;
	(void)user;
	(void)cleanup;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_thread_pool_submit_delayed_prepared(hx_os_thread_pool *pool,
	hx_os_deadline due, hx_os_task_prepare_fn prepare, void *factory,
	hx_os_task_fn function, hx_os_task_cleanup cleanup, hx_os_task **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_DELAYED_TASKS
	if (!prepare || !cleanup || due == HX_OS_DEADLINE_INFINITE) {
		if (out)
			*out = NULL;
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
	return hx_os_pool_submit(pool, function, NULL, cleanup, prepare,
		factory, out, due, error);
#else
	(void)pool;
	(void)due;
	(void)prepare;
	(void)factory;
	(void)function;
	(void)cleanup;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_context *hx_os_thread_pool_context(const hx_os_thread_pool *pool)
{
	return pool ? pool->context : NULL;
}

hx_os_status hx_os_thread_pool_inside(
	const hx_os_thread_pool *pool, int *inside)
{
	if (!pool || !inside)
		return HX_OS_INVALID;
	*inside = hx_os_current_pool == pool;
	return HX_OS_OK;
}

int hx_os_task_cancel_requested(hx_os_task *task)
{
	return (int)hx_os_atomic_load(&task->cancelled, HX_OS_ACQUIRE);
}

void hx_os_task_cancel(hx_os_task *task)
{
	hx_os_atomic_store(&task->cancelled, 1, HX_OS_RELEASE);
#if HX_OS_ENABLE_DELAYED_TASKS
	if (task->delayed) {
		hx_os_delayed_task *delay = task->delayed;
		(void)hx_os_mutex_lock(delay->mutex, NULL);
		if (delay->pool) {
			hx_os_thread_pool *pool = delay->pool;
			hx_os_task **cursor;
			(void)hx_os_mutex_lock(pool->mutex, NULL);
			for (cursor = &pool->delayed; *cursor;
				cursor = &(*cursor)->next)
				if (*cursor == task) {
					*cursor = task->next;
					hx_os_pool_ready(pool, task);
					break;
				}
			hx_os_condition_notify_all(pool->condition);
			(void)hx_os_mutex_unlock(pool->mutex, NULL);
		}
		(void)hx_os_mutex_unlock(delay->mutex, NULL);
	}
#endif
}

int hx_os_task_state(hx_os_task *task)
{
	return (int)hx_os_atomic_load(&task->state, HX_OS_ACQUIRE);
}

hx_os_status hx_os_task_wait_until(hx_os_task *task, hx_os_deadline deadline,
	hx_os_status *work_status, hx_os_error *error)
{
	hx_os_status status;
	if (!task)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_task_state(task) != HX_OS_TASK_FINISHED) {
		if (hx_os_current_pool && hx_os_current_pool == task->pool)
			return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
		status =
			hx_os_event_wait_until(task->finished, deadline, error);
		if (status != HX_OS_OK)
			return status;
	}
	if (work_status)
		*work_status = task->result;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_task_retain(hx_os_task *task)
{
	(void)hx_os_atomic_fetch_add(&task->references, 1, HX_OS_RELAXED);
}

void hx_os_task_release(hx_os_task *task)
{
	if (!task)
		return;
	if (hx_os_atomic_fetch_add(&task->references, -1, HX_OS_ACQ_REL) == 1) {
		hx_os_event_destroy(task->finished);
#if HX_OS_ENABLE_DELAYED_TASKS
		if (task->delayed)
			hx_os_mutex_destroy(task->delayed->mutex);
#endif
		hx_os_memory_free(task);
	}
}

hx_os_status hx_os_thread_pool_close_until(hx_os_thread_pool *pool, int mode,
	hx_os_deadline deadline, hx_os_error *error)
{
	int64_t expected = 0;
	hx_os_status status = HX_OS_OK;
	hx_os_task *task;
	size_t i;
	if (!pool ||
		(mode != HX_OS_POOL_DRAIN && mode != HX_OS_POOL_CANCEL_PENDING))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_current_pool == pool)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	if (!hx_os_atomic_compare_exchange(&pool->close_claim, &expected, 1,
		    HX_OS_ACQUIRE, HX_OS_RELAXED))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	(void)hx_os_mutex_lock(pool->mutex, NULL);
	pool->closing = 1;
	if (mode == HX_OS_POOL_CANCEL_PENDING) {
		for (task = pool->first; task; task = task->next)
			hx_os_atomic_store(&task->cancelled, 1, HX_OS_RELEASE);
#if HX_OS_ENABLE_DELAYED_TASKS
		while (pool->delayed) {
			task = pool->delayed;
			pool->delayed = task->next;
			hx_os_atomic_store(&task->cancelled, 1, HX_OS_RELEASE);
			hx_os_pool_ready(pool, task);
		}
#endif
	}
	hx_os_condition_notify_all(pool->condition);
	(void)hx_os_mutex_unlock(pool->mutex, NULL);
	for (i = 0; i < pool->thread_count; ++i) {
		status = hx_os_thread_join_until(
			pool->threads[i], deadline, NULL, error);
		if (status != HX_OS_OK)
			break;
	}
	if (status == HX_OS_OK) {
		(void)hx_os_mutex_lock(pool->mutex, NULL);
		pool->joined = 1;
		if (pool->submitting)
			status = HX_OS_BUSY;
		(void)hx_os_mutex_unlock(pool->mutex, NULL);
	}
	hx_os_atomic_store(&pool->close_claim, 0, HX_OS_RELEASE);
	if (status == HX_OS_BUSY)
		return hx_os_i_error(error, status, 0, 0);
	return status;
}

hx_os_status hx_os_thread_pool_destroy(
	hx_os_thread_pool **pool, hx_os_error *error)
{
	hx_os_thread_pool *value;
	size_t i;
	if (!pool)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *pool;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	(void)hx_os_mutex_lock(value->mutex, NULL);
	if (!value->joined || value->submitting ||
		hx_os_atomic_load(&value->close_claim, HX_OS_ACQUIRE)) {
		(void)hx_os_mutex_unlock(value->mutex, NULL);
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}
	(void)hx_os_mutex_unlock(value->mutex, NULL);
	for (i = 0; i < value->thread_count; ++i)
		(void)hx_os_thread_destroy(&value->threads[i], NULL);
	hx_os_condition_destroy(value->condition);
	hx_os_mutex_destroy(value->mutex);
	hx_os_memory_free(value->threads);
	hx_os_memory_free(value);
	*pool = NULL;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
