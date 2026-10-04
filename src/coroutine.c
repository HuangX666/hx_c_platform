#include "coroutine_private.h"

#if defined(_MSC_VER)
static __declspec(thread) hx_os_coro *hx_os_coro_active;

static __declspec(thread) hx_os_coro_worker *hx_os_coro_worker_active;

static __declspec(thread) hx_os_coro_binding *hx_os_coro_bound;
#else
static __thread hx_os_coro *hx_os_coro_active;
static __thread hx_os_coro_worker *hx_os_coro_worker_active;
static __thread hx_os_coro_binding *hx_os_coro_bound;
#endif

struct hx_os_coro_local {
	hx_os_coro_local *next;
	void *key;
	void *value;
	hx_os_tls_destructor cleanup;
};

struct hx_os_coro_local_key {
	hx_os_atomic references;
	hx_os_tls_destructor cleanup;
	void *user;
};

struct hx_os_coro_binding {
	hx_os_coro_runtime *runtime;
	hx_os_coro_binding *previous;
	uint64_t thread;
};

void *hx_os_i_coro_local_get(void *key)
{
	hx_os_coro_local *local;

	if (!hx_os_coro_active)
		return NULL;

	for (local = hx_os_coro_active->locals; local; local = local->next)
		if (local->key == key)
			return local->value;

	return NULL;
}

static hx_os_status hx_os_coro_local_room(hx_os_coro *coroutine, void *key)
{
	hx_os_coro_local *local;
	size_t count = 0;

	for (local = coroutine->locals; local; local = local->next) {
		if (local->key == key)
			return HX_OS_BUSY;

		++count;
	}

	return count < 64 ? HX_OS_OK : HX_OS_LIMIT;
}

hx_os_status hx_os_i_coro_local_set(void *key, void *value,
	hx_os_tls_destructor cleanup, hx_os_error *error)
{
	hx_os_coro *coroutine = hx_os_coro_active;
	hx_os_coro_local *local;
	hx_os_status status;

	if (!coroutine || !key || !value || !cleanup)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_coro_local_room(coroutine, key);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	local = hx_os_i_alloc(coroutine->runtime->context, sizeof(*local), 0);
	if (!local)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	/* An allocator callback can reenter through a different profiler. */
	status = hx_os_coro_local_room(coroutine, key);
	if (status != HX_OS_OK) {
		hx_os_memory_free(local);

		return hx_os_i_error(error, status, 0, 0);
	}

	local->key = key;
	local->value = value;
	local->cleanup = cleanup;
	local->next = coroutine->locals;
	coroutine->locals = local;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_coro_local_clear(void *key)
{
	hx_os_coro_local **cursor;

	if (!hx_os_coro_active)
		return;

	for (cursor = &hx_os_coro_active->locals; *cursor;
		cursor = &(*cursor)->next)
		if ((*cursor)->key == key) {
			hx_os_coro_local *local = *cursor;

			*cursor = local->next;
			++hx_os_coro_active->preparing;

			local->cleanup(local->value, local->key);
			--hx_os_coro_active->preparing;

			hx_os_memory_free(local);

			return;
		}
}

static void hx_os_coro_locals_destroy(hx_os_coro *coroutine)
{
	while (coroutine->locals) {
		hx_os_coro_local *local = coroutine->locals;

		coroutine->locals = local->next;

		local->cleanup(local->value, local->key);
		hx_os_memory_free(local);
	}
}

hx_os_status hx_os_coro_local_key_create(hx_os_context *context,
	hx_os_tls_destructor cleanup, void *user, hx_os_coro_local_key **out,
	hx_os_error *error)
{
	hx_os_coro_local_key *key;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	key = hx_os_i_zero(context, sizeof(*key));
	if (!key)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	hx_os_atomic_store(&key->references, 1, HX_OS_RELAXED);
	key->cleanup = cleanup;
	key->user = user;
	*out = key;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_coro_local_key_retain(hx_os_coro_local_key *key)
{
	if (key)
		(void)hx_os_atomic_fetch_add(
			&key->references, 1, HX_OS_RELAXED);
}

void hx_os_coro_local_key_release(hx_os_coro_local_key *key)
{
	if (key &&
		hx_os_atomic_fetch_sub(&key->references, 1, HX_OS_ACQ_REL) == 1)
		hx_os_memory_free(key);
}

static void hx_os_coro_local_drop(void *value, void *user)
{
	hx_os_coro_local_key *key = user;

	if (key->cleanup)
		key->cleanup(value, key->user);

	hx_os_coro_local_key_release(key);
}

void *hx_os_coro_local_get(hx_os_coro_local_key *key)
{
	return key ? hx_os_i_coro_local_get(key) : NULL;
}

hx_os_status hx_os_coro_local_set(
	hx_os_coro_local_key *key, void *value, hx_os_error *error)
{
	hx_os_coro *self = hx_os_coro_active;
	hx_os_coro_local *local;
	hx_os_status status;

	if (!key || !self)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (self->preparing)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (!value) {
		hx_os_i_coro_local_clear(key);

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	for (local = self->locals; local; local = local->next)
		if (local->key == key) {
			void *previous = local->value;

			if (previous == value)
				return hx_os_i_error(error, HX_OS_OK, 0, 0);

			local->value = value;
			++self->preparing;
			if (key->cleanup)
				key->cleanup(previous, key->user);

			--self->preparing;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

	hx_os_coro_local_key_retain(key);
	++self->preparing;

	status = hx_os_i_coro_local_set(
		key, value, hx_os_coro_local_drop, error);
	--self->preparing;
	if (status != HX_OS_OK)
		hx_os_coro_local_key_release(key);

	return status;
}

#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
void *hx_os_i_signal_exchange_execution(void *value);
#endif
#if HX_OS_ENABLE_PROFILER
void *hx_os_i_profiler_exchange_execution(void *value);
#endif
#if HX_OS_ENABLE_CONTAINERS
void *hx_os_i_map_exchange_execution(void *value);
#endif

static void hx_os_coro_exchange(
	hx_os_coro_execution *save, const hx_os_coro_execution *restore)
{
	(void)save;
	(void)restore;
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	save->signal = hx_os_i_signal_exchange_execution(restore->signal);
#endif
#if HX_OS_ENABLE_PROFILER
	save->profiler = hx_os_i_profiler_exchange_execution(restore->profiler);
#endif
#if HX_OS_ENABLE_CONTAINERS
	save->hash = hx_os_i_map_exchange_execution(restore->hash);
#endif
}

hx_os_status hx_os_coro_options_init(
	hx_os_coro_options *options, size_t capacity)
{
	const hx_os_coro_options defaults = HX_OS_CORO_OPTIONS_INIT;

	if (!options || capacity < sizeof(defaults))
		return HX_OS_INVALID;

	*options = defaults;

	return HX_OS_OK;
}

void hx_os_i_coro_runtime_retain(hx_os_coro_runtime *runtime)
{
	(void)hx_os_atomic_fetch_add(&runtime->references, 1, HX_OS_RELAXED);
}

void hx_os_i_coro_runtime_release(hx_os_coro_runtime *runtime)
{
	if (hx_os_atomic_fetch_sub(&runtime->references, 1, HX_OS_ACQ_REL) != 1)
		return;

	hx_os_i_coro_io_destroy(runtime);
	hx_os_condition_destroy(runtime->condition);
	hx_os_mutex_destroy(runtime->mutex);
	hx_os_memory_free(runtime->timers);
	hx_os_memory_free(runtime->workers);
	hx_os_memory_free(runtime);
}

hx_os_context *hx_os_coro_runtime_context(const hx_os_coro_runtime *runtime)
{
	return runtime ? runtime->context : NULL;
}

hx_os_coro *hx_os_coro_current(void)
{
	return hx_os_coro_active;
}

hx_os_coro_runtime *hx_os_coro_current_runtime(void)
{
	return hx_os_coro_active   ? hx_os_coro_active->runtime
		: hx_os_coro_bound ? hx_os_coro_bound->runtime
				   : NULL;
}

hx_os_status hx_os_coro_binding_create(hx_os_coro_runtime *runtime,
	hx_os_coro_binding **out, hx_os_error *error)
{
	hx_os_coro_binding *binding;

	if (out)
		*out = NULL;

	if (!runtime || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_coro_worker_active || hx_os_i_coro_io_inside())
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	binding = hx_os_i_alloc(runtime->context, sizeof(*binding), 0);
	if (!binding)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (runtime->closing) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		hx_os_memory_free(binding);

		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	}

	hx_os_i_coro_runtime_retain(runtime);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	binding->runtime = runtime;
	binding->previous = hx_os_coro_bound;
	binding->thread = hx_os_thread_id();
	hx_os_coro_bound = binding;
	*out = binding;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_coro_binding_close(
	hx_os_coro_binding **binding, hx_os_error *error)
{
	hx_os_coro_binding *value;
	hx_os_coro_runtime *runtime;

	if (!binding)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	value = *binding;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (value != hx_os_coro_bound || value->thread != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_os_coro_bound = value->previous;
	runtime = value->runtime;
	*binding = NULL;

	hx_os_memory_free(value);
	hx_os_i_coro_runtime_release(runtime);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_i_coro_worker_inside(void)
{
	return hx_os_coro_worker_active != NULL;
}

static void hx_os_coro_record_error(
	hx_os_coro_runtime *runtime, const hx_os_error *error)
{
	if (error->status == HX_OS_OK)
		return;

	if (!runtime->errors.count)
		runtime->errors.first = *error;

	if (runtime->errors.count != UINT64_MAX)
		++runtime->errors.count;
}

hx_os_status hx_os_coro_errors_read(hx_os_coro_runtime *runtime, int clear,
	hx_os_coro_errors *out, hx_os_error *error)
{
	if (!runtime || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	*out = runtime->errors;
	if (clear)
		memset(&runtime->errors, 0, sizeof(runtime->errors));

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_coro_runtime_wait_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;

	if (!runtime)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_coro_worker_active || hx_os_i_coro_io_inside())
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	while (status == HX_OS_OK && (runtime->active || runtime->preparing))
		status = hx_os_condition_wait_until(
			runtime->condition, runtime->mutex, deadline, error);

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

int hx_os_coro_kind(const hx_os_coro *coroutine)
{
	return coroutine ? coroutine->kind : -1;
}

int hx_os_coro_wait_pending(void)
{
	hx_os_coro *coroutine = hx_os_coro_active;
	int pending;

	if (!coroutine)
		return 0;

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
	pending = coroutine->wait_kind != HX_OS_CO_WAIT_NONE;
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);

	return pending;
}

hx_os_status hx_os_coro_complete(hx_os_status status, const hx_os_error *detail)
{
	hx_os_coro *coroutine = hx_os_coro_active;

	if (!coroutine)
		return HX_OS_INVALID;

	if (detail)
		coroutine->result = *detail;
	else
		hx_os_error_clear(&coroutine->result);

	coroutine->result.status = status;
	coroutine->returning = 1;

	return status;
}

void hx_os_coro_retain(hx_os_coro *coroutine)
{
	if (coroutine)
		(void)hx_os_atomic_fetch_add(
			&coroutine->references, 1, HX_OS_RELAXED);
}

void hx_os_coro_release(hx_os_coro *coroutine)
{
	hx_os_coro_runtime *runtime;

	if (!coroutine ||
		hx_os_atomic_fetch_sub(
			&coroutine->references, 1, HX_OS_ACQ_REL) != 1)
		return;
	runtime = coroutine->runtime;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	--runtime->objects;
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	hx_os_memory_free(coroutine);
	hx_os_i_coro_runtime_release(runtime);
}

int hx_os_coro_cancel_requested(const hx_os_coro *coroutine)
{
	return coroutine &&
		hx_os_atomic_load(&coroutine->cancelled, HX_OS_ACQUIRE) != 0;
}

int hx_os_coro_ready(const hx_os_coro *coroutine)
{
	return coroutine &&
		hx_os_atomic_load(&coroutine->finished, HX_OS_ACQUIRE) != 0;
}

static void hx_os_coro_timer_swap(
	hx_os_coro_runtime *runtime, size_t a, size_t b)
{
	hx_os_coro *saved = runtime->timers[a];

	runtime->timers[a] = runtime->timers[b];
	runtime->timers[b] = saved;
	runtime->timers[a]->timer_index = a;
	runtime->timers[b]->timer_index = b;
}

static void hx_os_coro_timer_up(hx_os_coro_runtime *runtime, size_t index)
{
	while (index &&
		runtime->timers[index]->deadline <
			runtime->timers[(index - 1) / 2]->deadline) {
		size_t parent = (index - 1) / 2;

		hx_os_coro_timer_swap(runtime, index, parent);
		index = parent;
	}
}

static void hx_os_coro_timer_remove(hx_os_coro *coroutine)
{
	hx_os_coro_runtime *runtime = coroutine->runtime;
	size_t index = coroutine->timer_index;

	if (index == SIZE_MAX)
		return;

	coroutine->timer_index = SIZE_MAX;
	--runtime->timer_count;
	if (index == runtime->timer_count)
		return;

	runtime->timers[index] = runtime->timers[runtime->timer_count];
	runtime->timers[index]->timer_index = index;
	if (index &&
		runtime->timers[index]->deadline <
			runtime->timers[(index - 1) / 2]->deadline)
		hx_os_coro_timer_up(runtime, index);
	else
		while (index < runtime->timer_count / 2) {
			size_t child = index * 2 + 1;

			if (child + 1 < runtime->timer_count &&
				runtime->timers[child + 1]->deadline <
					runtime->timers[child]->deadline)
				++child;
			if (runtime->timers[index]->deadline <=
				runtime->timers[child]->deadline)
				break;
			hx_os_coro_timer_swap(runtime, index, child);
			index = child;
		}
}

void hx_os_i_coro_enqueue(hx_os_coro *coroutine)
{
	hx_os_coro_worker *worker = coroutine->worker;

	coroutine->state = HX_OS_CO_READY;
	coroutine->ready_next = NULL;
	if (worker->last)
		worker->last->ready_next = coroutine;
	else
		worker->first = coroutine;

	worker->last = coroutine;
	hx_os_condition_notify_all(coroutine->runtime->condition);
}

void hx_os_i_coro_finish_wait(hx_os_coro *coroutine, const hx_os_error *result)
{
	hx_os_error completed = *result;

	if (coroutine->wait_kind == HX_OS_CO_WAIT_NONE || coroutine->wait_done)
		return;

	hx_os_coro_timer_remove(coroutine);
	if (coroutine->wait_kind == HX_OS_CO_WAIT_SYNC &&
		!hx_os_i_coro_sync_finish(coroutine, &completed))
		return;
	if (coroutine->target) {
		if (coroutine->join_previous)
			coroutine->join_previous->join_next =
				coroutine->join_next;
		else
			coroutine->target->joiners = coroutine->join_next;

		if (coroutine->join_next)
			coroutine->join_next->join_previous =
				coroutine->join_previous;

		coroutine->join_previous = coroutine->join_next = NULL;
	}

	if (coroutine->wait_kind == HX_OS_CO_WAIT_CHANNEL)
		hx_os_i_coro_channel_unlink(coroutine);

	coroutine->wait_result = completed;
	coroutine->wait_done = 1;
	if (coroutine->state == HX_OS_CO_WAITING)
		hx_os_i_coro_enqueue(coroutine);
}

void hx_os_i_coro_cancel_wait(hx_os_coro *coroutine, hx_os_status status)
{
	hx_os_error error;

	if (coroutine->wait_kind == HX_OS_CO_WAIT_NONE || coroutine->wait_done)
		return;

	(void)hx_os_i_error(&error, status, 0, 0);
	if (coroutine->wait_kind == HX_OS_CO_WAIT_IO) {
		hx_os_coro_timer_remove(coroutine);
		coroutine->wait_result = error;
		hx_os_i_coro_io_cancel(coroutine);
	}
	else
		hx_os_i_coro_finish_wait(coroutine, &error);
}

static void hx_os_coro_expire(hx_os_coro_runtime *runtime)
{
	uint64_t now;
	hx_os_error error;

	if (!runtime->timer_count)
		return;

	if (hx_os_monotonic_read(&now, &error) != HX_OS_OK) {
		while (runtime->timer_count)
			hx_os_i_coro_cancel_wait(
				runtime->timers[0], error.status);

		return;
	}

	while (runtime->timer_count && runtime->timers[0]->deadline <= now) {
		hx_os_coro *coroutine = runtime->timers[0];

		if (coroutine->wait_kind == HX_OS_CO_WAIT_SLEEP) {
			hx_os_error_clear(&error);
			hx_os_i_coro_finish_wait(coroutine, &error);
		}
		else
			hx_os_i_coro_cancel_wait(coroutine, HX_OS_TIMEOUT);
	}
}

hx_os_status hx_os_i_coro_begin_wait(hx_os_coro *coroutine, int kind,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro_runtime *runtime = coroutine->runtime;

	if (coroutine->preparing || coroutine->returning ||
		coroutine->wait_kind != HX_OS_CO_WAIT_NONE)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (hx_os_coro_cancel_requested(coroutine))
		return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);

	coroutine->wait_kind = kind;
	coroutine->wait_done = 0;
	coroutine->deadline = deadline;
	hx_os_error_clear(&coroutine->wait_result);
	if (deadline != HX_OS_DEADLINE_INFINITE &&
		kind != HX_OS_CO_WAIT_YIELD) {
		coroutine->timer_index = runtime->timer_count;
		runtime->timers[runtime->timer_count++] = coroutine;
		hx_os_coro_timer_up(runtime, coroutine->timer_index);
		hx_os_condition_notify_all(runtime->condition);
	}

	return HX_OS_OK;
}

hx_os_status hx_os_i_coro_suspend(hx_os_coro *coroutine, hx_os_error *error)
{
	if (coroutine->kind == HX_OS_CORO_STACKLESS)
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
#if HX_OS_ENABLE_STACKFUL_COROUTINES
	hx_os_coro_exchange(
		&coroutine->execution, &coroutine->worker->execution);
	hx_os_coro_active = NULL;
	hx_os_i_coro_context_switch(
		coroutine->context, &coroutine->worker->root);

	return HX_OS_OK;
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_i_coro_clear_wait(hx_os_coro *coroutine)
{
	hx_os_coro *target;
	int kind;

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
	target = coroutine->target;
	kind = coroutine->wait_kind;
	coroutine->target = NULL;
	coroutine->wait_kind = HX_OS_CO_WAIT_NONE;
	coroutine->wait_done = 0;
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
	if (kind == HX_OS_CO_WAIT_CHANNEL)
		hx_os_i_coro_channel_dispose_wait(coroutine);
	else if (kind == HX_OS_CO_WAIT_IO)
		hx_os_i_coro_io_dispose_wait(coroutine);
	else if (kind == HX_OS_CO_WAIT_SYNC)
		hx_os_i_coro_sync_dispose(coroutine);

	hx_os_coro_release(target);
}

hx_os_status hx_os_i_coro_consume(hx_os_coro *coroutine, hx_os_error *error)
{
	hx_os_error result = coroutine->wait_result;

	hx_os_i_coro_clear_wait(coroutine);
	if (error)
		*error = result;

	return result.status;
}

int hx_os_i_coro_wait_ready(hx_os_coro *coroutine, int kind)
{
	int ready;

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
	ready = coroutine->wait_kind == kind && coroutine->wait_done;
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);

	return ready;
}

void hx_os_coro_cancel(hx_os_coro *coroutine)
{
	if (!coroutine)
		return;

	hx_os_atomic_store(&coroutine->cancelled, 1, HX_OS_RELEASE);
	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);
	hx_os_i_coro_cancel_wait(coroutine, HX_OS_CANCELLED);
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
}

hx_os_status hx_os_coro_yield(hx_os_error *error)
{
	hx_os_coro *coroutine = hx_os_coro_active;
	hx_os_status status;

	if (!coroutine)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_i_coro_wait_ready(coroutine, HX_OS_CO_WAIT_YIELD))
		return hx_os_i_coro_consume(coroutine, error);

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);

	status = hx_os_i_coro_begin_wait(
		coroutine, HX_OS_CO_WAIT_YIELD, HX_OS_DEADLINE_INFINITE, error);
	if (status == HX_OS_OK)
		coroutine->wait_done = 1;

	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_i_coro_suspend(coroutine, error);

	return status == HX_OS_OK ? hx_os_i_coro_consume(coroutine, error)
				  : status;
}

hx_os_status hx_os_coro_sleep_until(hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_coro *coroutine = hx_os_coro_active;
	hx_os_status status;
	uint64_t now;

	if (!coroutine)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_i_coro_wait_ready(coroutine, HX_OS_CO_WAIT_SLEEP))
		return hx_os_i_coro_consume(coroutine, error);

	if (coroutine->preparing || coroutine->wait_kind != HX_OS_CO_WAIT_NONE)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (hx_os_coro_cancel_requested(coroutine))
		return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;

	if (deadline <= now)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	(void)hx_os_mutex_lock(coroutine->runtime->mutex, NULL);

	status = hx_os_i_coro_begin_wait(
		coroutine, HX_OS_CO_WAIT_SLEEP, deadline, error);
	(void)hx_os_mutex_unlock(coroutine->runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_i_coro_suspend(coroutine, error);

	return status == HX_OS_OK ? hx_os_i_coro_consume(coroutine, error)
				  : status;
}

hx_os_status hx_os_coro_sleep_ms(uint64_t milliseconds, hx_os_error *error)
{
	return hx_os_coro_sleep_until(
		hx_os_deadline_after_ms(milliseconds), error);
}

hx_os_status hx_os_coro_wait_until(hx_os_coro *coroutine,
	hx_os_deadline deadline, hx_os_error *work_error, hx_os_error *error)
{
	hx_os_coro *self = hx_os_coro_active;
	hx_os_coro_runtime *runtime;
	hx_os_status status = HX_OS_OK;

	if (!coroutine)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (self == coroutine)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	if (!self && (hx_os_coro_worker_active || hx_os_i_coro_io_inside()) &&
		!hx_os_coro_ready(coroutine))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	runtime = coroutine->runtime;
	if (self && self->runtime != runtime)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	if (self && hx_os_i_coro_wait_ready(self, HX_OS_CO_WAIT_TASK)) {
		if (self->target != coroutine)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		if (self->wait_result.status == HX_OS_OK && work_error)
			*work_error = coroutine->result;

		return hx_os_i_coro_consume(self, error);
	}

	if (self && (self->preparing || self->wait_kind != HX_OS_CO_WAIT_NONE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	while (!hx_os_coro_ready(coroutine)) {
		if (self) {
			status = hx_os_i_coro_begin_wait(
				self, HX_OS_CO_WAIT_TASK, deadline, error);
			if (status == HX_OS_OK) {
				hx_os_coro_retain(coroutine);
				self->target = coroutine;
				self->join_next = coroutine->joiners;
				if (self->join_next)
					self->join_next->join_previous = self;

				coroutine->joiners = self;
			}

			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
			if (status != HX_OS_OK)
				return status;

			status = hx_os_i_coro_suspend(self, error);
			if (status != HX_OS_OK)
				return status;

			if (self->wait_result.status == HX_OS_OK && work_error)
				*work_error = coroutine->result;

			return hx_os_i_coro_consume(self, error);
		}

		status = hx_os_condition_wait_until(
			runtime->condition, runtime->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}

	if (hx_os_coro_ready(coroutine)) {
		status = HX_OS_OK;
		if (work_error)
			*work_error = coroutine->result;
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

#if HX_OS_ENABLE_STACKFUL_COROUTINES
static void hx_os_coro_run_body(void *user)
{
	hx_os_coro *coroutine = user;

	hx_os_status status = coroutine->function(
		coroutine, coroutine->user, &coroutine->result);
	if (!coroutine->returning)
		coroutine->result.status = status;

	coroutine->returning = 1;
	hx_os_coro_exchange(
		&coroutine->execution, &coroutine->worker->execution);
	hx_os_coro_active = NULL;
}
#endif

static void hx_os_coro_finish(hx_os_coro *coroutine)
{
	hx_os_coro_runtime *runtime = coroutine->runtime;
	hx_os_error ready;

	hx_os_error_clear(&ready);
	hx_os_i_coro_clear_wait(coroutine);
	if (coroutine->cleanup)
		coroutine->cleanup(coroutine->user);
#if HX_OS_ENABLE_STACKFUL_COROUTINES
	if (coroutine->context)
		hx_os_i_coro_context_destroy(coroutine->context);
#endif
	hx_os_coro_locals_destroy(coroutine);
	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (coroutine->managed)
		hx_os_coro_record_error(runtime, &coroutine->result);

	runtime->stack_bytes -= coroutine->stack_cost;
	coroutine->stack_cost = 0;
	if (coroutine->previous)
		coroutine->previous->next = coroutine->next;
	else
		runtime->first = coroutine->next;

	if (coroutine->next)
		coroutine->next->previous = coroutine->previous;

	--runtime->active;
	coroutine->state = HX_OS_CO_FINISHED;
	hx_os_atomic_store(&coroutine->finished, 1, HX_OS_RELEASE);

	while (coroutine->joiners)
		hx_os_i_coro_finish_wait(coroutine->joiners, &ready);

	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	hx_os_coro_release(coroutine);
}

static void hx_os_coro_step(hx_os_coro *coroutine)
{
	hx_os_coro_worker *worker = coroutine->worker;
	hx_os_status status;

	if (!coroutine->started && hx_os_coro_cancel_requested(coroutine)) {
		(void)hx_os_i_error(&coroutine->result, HX_OS_CANCELLED, 0, 0);
		coroutine->returning = 1;

		return;
	}

	if (coroutine->returning)
		return;
#if HX_OS_ENABLE_STACKFUL_COROUTINES
	if (coroutine->kind == HX_OS_CORO_STACKFUL) {
		if (!coroutine->started) {
			status = worker->root_ready
				? HX_OS_OK
				: hx_os_i_coro_context_root(
					  &worker->root, &coroutine->result);
			if (status == HX_OS_OK) {
				worker->root_ready = 1;

				status = hx_os_i_coro_context_create(
					coroutine->context,
					coroutine->runtime->options.stack_bytes,
					hx_os_coro_run_body, coroutine,
					&coroutine->result);
				coroutine->context->caller = &worker->root;
			}

			if (status != HX_OS_OK) {
				coroutine->returning = 1;

				return;
			}
		}

		coroutine->started = 1;
		hx_os_coro_active = coroutine;
		hx_os_coro_exchange(&worker->execution, &coroutine->execution);
		hx_os_i_coro_context_switch(&worker->root, coroutine->context);

		return;
	}
#endif
	coroutine->started = 1;
	hx_os_coro_active = coroutine;
	hx_os_coro_exchange(&worker->execution, &coroutine->execution);

	status = coroutine->function(
		coroutine, coroutine->user, &coroutine->result);
	hx_os_coro_exchange(&coroutine->execution, &worker->execution);
	hx_os_coro_active = NULL;
	if (status != HX_OS_WOULD_BLOCK && !coroutine->returning) {
		coroutine->result.status = status;
		coroutine->returning = 1;
	}
}

static void *hx_os_coro_worker_main(void *user)
{
	hx_os_coro_worker *worker = user;
	hx_os_coro_runtime *runtime = worker->runtime;

	hx_os_coro_worker_active = worker;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	while (!runtime->started && !runtime->closing)
		(void)hx_os_condition_wait_until(runtime->condition,
			runtime->mutex, HX_OS_DEADLINE_INFINITE, NULL);

	for (;;) {
		hx_os_coro *coroutine;

		hx_os_coro_expire(runtime);
		coroutine = worker->first;
		if (!coroutine) {
			hx_os_status status;
			hx_os_deadline deadline = runtime->timer_count
				? runtime->timers[0]->deadline
				: HX_OS_DEADLINE_INFINITE;

			if (runtime->closing && !runtime->active &&
				!runtime->preparing)
				break;
			status = hx_os_condition_wait_until(runtime->condition,
				runtime->mutex, deadline, NULL);
			if (status != HX_OS_OK && status != HX_OS_TIMEOUT) {
				hx_os_coro *pending;

				for (pending = runtime->first; pending;
					pending = pending->next)
					hx_os_i_coro_cancel_wait(
						pending, status);
			}

			continue;
		}

		worker->first = coroutine->ready_next;
		if (!worker->first)
			worker->last = NULL;

		coroutine->state = HX_OS_CO_RUNNING;
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		hx_os_coro_step(coroutine);
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		if (coroutine->returning &&
			coroutine->wait_kind != HX_OS_CO_WAIT_NONE &&
			!coroutine->wait_done) {
			(void)hx_os_i_error(
				&coroutine->result, HX_OS_INVALID, 0, 0);
			hx_os_i_coro_cancel_wait(coroutine, HX_OS_INVALID);
		}

		if (coroutine->wait_kind != HX_OS_CO_WAIT_NONE &&
			!coroutine->wait_done)
			coroutine->state = HX_OS_CO_WAITING;
		else if (coroutine->returning) {
			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
			hx_os_coro_finish(coroutine);
			(void)hx_os_mutex_lock(runtime->mutex, NULL);
		}
		else
			hx_os_i_coro_enqueue(coroutine);
	}

	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
#if HX_OS_ENABLE_STACKFUL_COROUTINES
	if (worker->root_ready)
		hx_os_i_coro_context_root_close(&worker->root);
#endif
	hx_os_coro_worker_active = NULL;

	return NULL;
}

hx_os_status hx_os_coro_runtime_create(hx_os_context *context,
	const hx_os_coro_options *input, hx_os_coro_runtime **out,
	hx_os_error *error)
{
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_coro_runtime *runtime;
	hx_os_status status;
	size_t i;

	hx_os_error_clear(error);
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (input) {
		if (input->size < sizeof(options) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		options = *input;
	}

	if (!options.workers || options.workers > 64 ||
		!options.max_coroutines ||
		options.max_coroutines > 1024 * 1024 ||
		options.stack_bytes < 16384 || !options.max_channels ||
		!options.max_channel_bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	runtime = hx_os_i_zero(context, sizeof(*runtime));
	if (!runtime)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	runtime->options = options;
	runtime->context = context;
	hx_os_atomic_store(&runtime->references, 1, HX_OS_RELAXED);
	runtime->workers = hx_os_i_zero(
		context, options.workers * sizeof(*runtime->workers));
	runtime->timers = hx_os_i_alloc(
		context, options.max_coroutines * sizeof(*runtime->timers), 0);

	status = runtime->workers && runtime->timers ? HX_OS_OK
						     : HX_OS_NO_MEMORY;
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &runtime->mutex, error);

	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &runtime->condition, error);
#if HX_OS_ENABLE_STACKFUL_COROUTINES
	if (status == HX_OS_OK)
		status = hx_os_i_coro_stack_cost(
			options.stack_bytes, &runtime->stack_cost, error);
#endif
	if (status == HX_OS_OK)
		status = hx_os_i_coro_io_create(runtime, error);

	if (status == HX_OS_OK)
		for (i = 0; i < options.workers; ++i) {
			runtime->workers[i].runtime = runtime;

			status = hx_os_thread_create(context,
				hx_os_coro_worker_main, &runtime->workers[i],
				&runtime->workers[i].thread, error);
			if (status != HX_OS_OK)
				break;

			++runtime->worker_count;
		}

	if (status != HX_OS_OK) {
		/* The startup gate admits no user code; rollback only joins
		 * idle workers. */
		if (runtime->mutex) {
			(void)hx_os_mutex_lock(runtime->mutex, NULL);
			runtime->closing = 2;
			if (runtime->condition)
				hx_os_condition_notify_all(runtime->condition);

			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		}

		for (i = 0; i < runtime->worker_count; ++i) {
			(void)hx_os_thread_join_until(
				runtime->workers[i].thread,
				HX_OS_DEADLINE_INFINITE, NULL, NULL);

			(void)hx_os_thread_destroy(
				&runtime->workers[i].thread, NULL);
		}

		(void)hx_os_i_coro_io_close(
			runtime, HX_OS_DEADLINE_INFINITE, NULL);
		hx_os_i_coro_runtime_release(runtime);
		if (error && error->status == HX_OS_OK)
			(void)hx_os_i_error(error, status, 0, 0);

		return status;
	}

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	runtime->started = 1;
	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	*out = runtime;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_coro_submit(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_fn function, void *user, hx_os_coro_cleanup_fn cleanup,
	hx_os_coro_prepare_fn prepare, void *factory, hx_os_coro **out,
	int managed, hx_os_error *error)
{
	hx_os_coro *coroutine;
	hx_os_status status;
	size_t stack_cost, bytes = sizeof(*coroutine);
	int prepared = 0;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!runtime || !function ||
		(kind != HX_OS_CORO_STACKFUL && kind != HX_OS_CORO_STACKLESS))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if !HX_OS_ENABLE_STACKFUL_COROUTINES
	if (kind == HX_OS_CORO_STACKFUL)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	stack_cost = kind == HX_OS_CORO_STACKFUL ? runtime->stack_cost : 0;
	(void)hx_os_mutex_lock(runtime->mutex, NULL);

	status = runtime->closing ? HX_OS_CLOSED
		: runtime->objects >= runtime->options.max_coroutines ||
			runtime->next_identity == UINT64_MAX ||
			stack_cost > runtime->options.max_stack_bytes -
					runtime->stack_bytes
		? HX_OS_LIMIT
		: HX_OS_OK;
	if (status == HX_OS_OK) {
		++runtime->objects;
		++runtime->preparing;
		runtime->stack_bytes += stack_cost;
		hx_os_i_coro_runtime_retain(runtime);
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (kind == HX_OS_CORO_STACKFUL)
		bytes += sizeof(hx_os_i_coro_context);

	coroutine = hx_os_i_zero(runtime->context, bytes);
	if (!coroutine)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	else if (prepare) {
		hx_os_error detail;

		hx_os_error_clear(&detail);
		user = NULL;

		status = prepare(factory, &user, &detail);
		if (error) {
			*error = detail;
			error->status = status;
		}

		prepared = status == HX_OS_OK;
	}

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (status == HX_OS_OK && runtime->closing)
		status = hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	if (status == HX_OS_OK) {
		--runtime->preparing;
		coroutine->runtime = runtime;
		coroutine->function = function;
		coroutine->user = user;
		coroutine->cleanup = cleanup;
		coroutine->kind = kind;
		coroutine->managed = managed;
		coroutine->identity = ++runtime->next_identity;
		coroutine->stack_cost = stack_cost;
		coroutine->timer_index = SIZE_MAX;
		if (kind == HX_OS_CORO_STACKFUL)
			coroutine->context =
				(hx_os_i_coro_context *)(coroutine + 1);

		coroutine->worker = hx_os_coro_active &&
				hx_os_coro_active->runtime == runtime
			? hx_os_coro_active->worker
			: &runtime->workers[runtime->next_worker++ %
				  runtime->worker_count];
		coroutine->next = runtime->first;
		if (runtime->first)
			runtime->first->previous = coroutine;

		runtime->first = coroutine;
		++runtime->active;
		hx_os_atomic_store(&coroutine->references, 2, HX_OS_RELAXED);
		hx_os_i_coro_enqueue(coroutine);
	}
	else {
		--runtime->objects;
		runtime->stack_bytes -= stack_cost;
	}

	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status != HX_OS_OK) {
		if (prepared && cleanup)
			cleanup(user);

		hx_os_memory_free(coroutine);
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		--runtime->preparing;
		hx_os_condition_notify_all(runtime->condition);
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		hx_os_i_coro_runtime_release(runtime);

		return status;
	}

	*out = coroutine;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_coro_go(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_fn function, void *user, hx_os_coro_cleanup_fn cleanup,
	hx_os_coro **out, hx_os_error *error)
{
	return hx_os_coro_submit(runtime, kind, function, user, cleanup, NULL,
		NULL, out, 0, error);
}

hx_os_status hx_os_coro_go_prepared(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_prepare_fn prepare, void *factory, hx_os_coro_fn function,
	hx_os_coro_cleanup_fn cleanup, hx_os_coro **out, hx_os_error *error)
{
	if (out)
		*out = NULL;

	if (!prepare)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_coro_submit(runtime, kind, function, NULL, cleanup,
		prepare, factory, out, 0, error);
}

static hx_os_status hx_os_coro_spawn_submit(hx_os_coro_runtime *runtime,
	int kind, hx_os_coro_fn function, void *user,
	hx_os_coro_cleanup_fn cleanup, hx_os_coro_prepare_fn prepare,
	void *factory, hx_os_error *error)
{
	hx_os_coro *task = NULL;
	hx_os_error detail;
	hx_os_status status;

	hx_os_error_clear(&detail);

	status = hx_os_coro_submit(runtime, kind, function, user, cleanup,
		prepare, factory, &task, 1, &detail);
	if (status != HX_OS_OK && runtime) {
		(void)hx_os_mutex_lock(runtime->mutex, NULL);
		hx_os_coro_record_error(runtime, &detail);
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	}

	if (error)
		*error = detail;

	hx_os_coro_release(task);

	return status;
}

hx_os_status hx_os_coro_spawn(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_fn function, void *user, hx_os_coro_cleanup_fn cleanup,
	hx_os_error *error)
{
	return hx_os_coro_spawn_submit(
		runtime, kind, function, user, cleanup, NULL, NULL, error);
}

hx_os_status hx_os_coro_spawn_prepared(hx_os_coro_runtime *runtime, int kind,
	hx_os_coro_prepare_fn prepare, void *factory, hx_os_coro_fn function,
	hx_os_coro_cleanup_fn cleanup, hx_os_error *error)
{
	if (!prepare) {
		hx_os_error detail;

		(void)hx_os_i_error(&detail, HX_OS_INVALID, 0, 0);
		if (runtime) {
			(void)hx_os_mutex_lock(runtime->mutex, NULL);
			hx_os_coro_record_error(runtime, &detail);
			(void)hx_os_mutex_unlock(runtime->mutex, NULL);
		}

		if (error)
			*error = detail;

		return HX_OS_INVALID;
	}

	return hx_os_coro_spawn_submit(runtime, kind, function, NULL, cleanup,
		prepare, factory, error);
}

static hx_os_status hx_os_coro_close(hx_os_coro_runtime *runtime, int mode,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;
	size_t i;

	if (!runtime)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_coro_worker_active || hx_os_i_coro_io_inside())
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	if (runtime->closed) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	if (mode > runtime->closing)
		runtime->closing = mode;

	if (runtime->closing == 2) {
		hx_os_coro *coroutine;

		for (coroutine = runtime->first; coroutine;
			coroutine = coroutine->next) {
			hx_os_atomic_store(
				&coroutine->cancelled, 1, HX_OS_RELEASE);
			hx_os_i_coro_cancel_wait(coroutine, HX_OS_CANCELLED);
		}
	}

	hx_os_condition_notify_all(runtime->condition);
	if (runtime->joining || runtime->preparing) {
		(void)hx_os_mutex_unlock(runtime->mutex, NULL);

		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}

	runtime->joining = 1;

	while (runtime->active) {
		status = hx_os_condition_wait_until(
			runtime->condition, runtime->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}

	(void)hx_os_mutex_unlock(runtime->mutex, NULL);
	if (status == HX_OS_OK)
		for (i = 0; i < runtime->worker_count; ++i) {
			if (!runtime->workers[i].thread)
				continue;

			status = hx_os_thread_join_until(
				runtime->workers[i].thread, deadline, NULL,
				error);
			if (status == HX_OS_OK)
				status = hx_os_thread_destroy(
					&runtime->workers[i].thread, error);

			if (status != HX_OS_OK)
				break;
		}

	if (status == HX_OS_OK)
		status = hx_os_i_coro_io_close(runtime, deadline, error);

	(void)hx_os_mutex_lock(runtime->mutex, NULL);
	runtime->joining = 0;
	if (status == HX_OS_OK)
		runtime->closed = 1;

	hx_os_condition_notify_all(runtime->condition);
	(void)hx_os_mutex_unlock(runtime->mutex, NULL);

	return status == HX_OS_OK ? hx_os_i_error(error, status, 0, 0) : status;
}

hx_os_status hx_os_coro_runtime_close_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_close(runtime, 2, deadline, error);
}

hx_os_status hx_os_coro_runtime_drain_until(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_coro_close(runtime, 1, deadline, error);
}

hx_os_status hx_os_coro_runtime_destroy(
	hx_os_coro_runtime **runtime, hx_os_error *error)
{
	hx_os_coro_runtime *value;

	if (!runtime)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	value = *runtime;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (!value->closed)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	*runtime = NULL;

	hx_os_i_coro_runtime_release(value);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
