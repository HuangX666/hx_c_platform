#include "coord_private.h"
#include "hx_os_loop.h"
#include "hx_os_coroutine.h"

typedef struct hx_os_cancel_slot {
	hx_os_cancel_fn function;
	hx_os_cancel_fn cleanup;
	void *user;
	uint64_t id;
	int active;
	int removed;
} hx_os_cancel_slot;

struct hx_os_cancel_token {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_cancel_slot *slots;
	hx_os_atomic requested;
	size_t capacity;
	size_t waiters;
	uint64_t next_id;
	uint64_t dispatcher;
	int dispatching;
};

hx_os_status hx_os_cancel_create(hx_os_context *context, size_t max_callbacks,
	hx_os_cancel_token **out, hx_os_error *error)
{
	hx_os_cancel_token *token;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_THREAD_EX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!max_callbacks)
		max_callbacks = 64;
	if (max_callbacks > SIZE_MAX / sizeof(hx_os_cancel_slot))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	token = hx_os_i_zero(context, sizeof(*token));
	if (!token)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	token->slots =
		hx_os_i_zero(context, max_callbacks * sizeof(*token->slots));
	status = token->slots ? HX_OS_OK : HX_OS_NO_MEMORY;
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &token->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &token->condition, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(token->mutex);
		hx_os_memory_free(token->slots);
		hx_os_memory_free(token);
		if (status == HX_OS_NO_MEMORY)
			return hx_os_i_error(error, status, 0, 0);
		return status;
	}

	token->capacity = max_callbacks;
	*out = token;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_cancel_requested(const hx_os_cancel_token *token)
{
	return token && hx_os_atomic_load(&token->requested, HX_OS_ACQUIRE);
}

hx_os_status hx_os_cancel_request(hx_os_cancel_token *token, hx_os_error *error)
{
	hx_os_status status;
	size_t i;

	if (!token)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(token->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (!hx_os_cancel_requested(token)) {
		hx_os_atomic_store(&token->requested, 1, HX_OS_RELEASE);
		token->dispatching = 1;
		token->dispatcher = hx_os_thread_id();
		hx_os_condition_notify_all(token->condition);
		for (i = 0; i < token->capacity; ++i) {
			hx_os_cancel_slot *slot = &token->slots[i];
			hx_os_cancel_fn function = slot->function;
			void *user = slot->user;

			if (!function || slot->removed)
				continue;
			slot->active = 1;
			(void)hx_os_mutex_unlock(token->mutex, NULL);
			function(user);
			(void)hx_os_mutex_lock(token->mutex, NULL);
			slot->active = 0;
			hx_os_condition_notify_all(token->condition);
		}
		token->dispatching = 0;
		hx_os_condition_notify_all(token->condition);
	}
	(void)hx_os_mutex_unlock(token->mutex, NULL);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_cancel_register(hx_os_cancel_token *token,
	hx_os_cancel_fn function, void *user, hx_os_cancel_fn cleanup,
	uint64_t *id, hx_os_error *error)
{
	hx_os_status status;
	size_t i;

	if (id)
		*id = 0;
	if (!token || !function || !id)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(token->mutex, error);
	if (status != HX_OS_OK)
		return status;
	status = HX_OS_LIMIT;
	if (hx_os_cancel_requested(token))
		status = HX_OS_CANCELLED;
	else if (token->next_id != UINT64_MAX) {
		for (i = 0; i < token->capacity; ++i) {
			hx_os_cancel_slot *slot = &token->slots[i];

			if (slot->id)
				continue;
			slot->id = ++token->next_id;
			slot->function = function;
			slot->cleanup = cleanup;
			slot->user = user;
			*id = slot->id;
			status = HX_OS_OK;
			break;
		}
	}
	(void)hx_os_mutex_unlock(token->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_cancel_subscribe(hx_os_cancel_token *token,
	hx_os_cancel_fn function, void *user, uint64_t *id, hx_os_error *error)
{
	return hx_os_cancel_register(token, function, user, NULL, id, error);
}

hx_os_status hx_os_cancel_unsubscribe_until(hx_os_cancel_token *token,
	uint64_t id, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_cancel_slot *slot = NULL;
	hx_os_status status;
	hx_os_cancel_fn cleanup = NULL;
	void *user = NULL;
	size_t i;

	if (!token || !id)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(token->mutex, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; i < token->capacity; ++i)
		if (token->slots[i].id == id) {
			slot = &token->slots[i];
			break;
		}
	if (!slot)
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	else if (slot->active && token->dispatcher == hx_os_thread_id())
		status = hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	else {
		slot->removed = 1;
		++token->waiters;
		while (slot->active) {
			status = hx_os_condition_wait_until(token->condition,
				token->mutex, deadline, error);
			if (status != HX_OS_OK)
				break;
		}
		if (status == HX_OS_OK) {
			cleanup = slot->cleanup;
			user = slot->user;
			memset(slot, 0, sizeof(*slot));
		}
		--token->waiters;
	}
	(void)hx_os_mutex_unlock(token->mutex, NULL);
	if (cleanup)
		cleanup(user);
	return status;
}

hx_os_status hx_os_cancel_wait_until(
	hx_os_cancel_token *token, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!token)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(token->mutex, error);
	if (status != HX_OS_OK)
		return status;
	++token->waiters;
	while (!hx_os_cancel_requested(token)) {
		status = hx_os_condition_wait_until(
			token->condition, token->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	--token->waiters;
	(void)hx_os_mutex_unlock(token->mutex, NULL);
	return status;
}

hx_os_status hx_os_sleep_cancellable_until(
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	hx_os_status status;

	if (!token)
		return hx_os_sleep_until(deadline, error);
	status = hx_os_cancel_wait_until(token, deadline, error);
	if (status == HX_OS_TIMEOUT)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (status == HX_OS_OK)
		return hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
	return status;
}

hx_os_status hx_os_cancel_destroy(
	hx_os_cancel_token **token, hx_os_error *error)
{
	hx_os_cancel_token *value;
	hx_os_status status;
	size_t i;

	if (!token)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *token;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_mutex_try_lock(value->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (value->waiters || value->dispatching)
		status = HX_OS_BUSY;
	for (i = 0; i < value->capacity && status == HX_OS_OK; ++i)
		if (value->slots[i].id)
			status = HX_OS_BUSY;
	(void)hx_os_mutex_unlock(value->mutex, NULL);
	if (status == HX_OS_OK) {
		hx_os_condition_destroy(value->condition);
		hx_os_mutex_destroy(value->mutex);
		hx_os_memory_free(value->slots);
		hx_os_memory_free(value);
		*token = NULL;
	}
	return hx_os_i_error(error, status, 0, 0);
}

static void HX_OS_CALL hx_os_cancel_wake(void *user)
{
	hx_os_cancel_watch *watch = user;

	(void)hx_os_mutex_lock(watch->mutex, NULL);
	hx_os_condition_notify_all(watch->condition);
	(void)hx_os_mutex_unlock(watch->mutex, NULL);
}

hx_os_status hx_os_i_cancel_watch(hx_os_cancel_watch *watch,
	hx_os_cancel_token *token, hx_os_mutex *mutex,
	hx_os_condition *condition, hx_os_error *error)
{
	memset(watch, 0, sizeof(*watch));
	watch->token = token;
	watch->mutex = mutex;
	watch->condition = condition;
	return token ? hx_os_cancel_subscribe(token, hx_os_cancel_wake, watch,
			       &watch->id, error)
		     : HX_OS_OK;
}

void hx_os_i_cancel_unwatch(hx_os_cancel_watch *watch)
{
	if (watch->id)
		(void)hx_os_cancel_unsubscribe_until(
			watch->token, watch->id, HX_OS_DEADLINE_INFINITE, NULL);
}

#define HX_OS_CANCEL_BIND(name, type, enabled, retain, cancel, release)        \
	static void HX_OS_CALL hx_os_cancel_##name##_call(void *user)          \
	{                                                                      \
		cancel((type *)user);                                          \
	}                                                                      \
	static void HX_OS_CALL hx_os_cancel_##name##_drop(void *user)          \
	{                                                                      \
		release((type *)user);                                         \
	}                                                                      \
	hx_os_status hx_os_cancel_bind_##name(hx_os_cancel_token *token,       \
		type *target, uint64_t *id, hx_os_error *error)                \
	{                                                                      \
		hx_os_status status;                                           \
		if (id)                                                        \
			*id = 0;                                               \
		if (!(enabled))                                                \
			return hx_os_i_error(                                  \
				error, HX_OS_NOT_SUPPORTED, 0, 0);             \
		if (!target)                                                   \
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);      \
		retain(target);                                                \
		status = hx_os_cancel_register(token,                          \
			hx_os_cancel_##name##_call, target,                    \
			hx_os_cancel_##name##_drop, id, error);                \
		if (status != HX_OS_OK)                                        \
			release(target);                                       \
		return status;                                                 \
	}

HX_OS_CANCEL_BIND(task, hx_os_task, HX_OS_ENABLE_THREAD_POOL, hx_os_task_retain,
	hx_os_task_cancel, hx_os_task_release)
HX_OS_CANCEL_BIND(request, hx_os_request, HX_OS_ENABLE_LOOP,
	hx_os_request_retain, hx_os_request_cancel, hx_os_request_release)
HX_OS_CANCEL_BIND(coroutine, hx_os_coro, HX_OS_ENABLE_COROUTINES,
	hx_os_coro_retain, hx_os_coro_cancel, hx_os_coro_release)

#undef HX_OS_CANCEL_BIND
