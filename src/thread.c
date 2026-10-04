#include "thread_private.h"

struct hx_os_event {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	int manual;
	int signaled;
};

typedef struct hx_os_i_tls_value {
	hx_os_tls_key *key;
	void *value;
} hx_os_i_tls_value;

hx_os_status hx_os_mutex_create(hx_os_context *context, int recursive,
	hx_os_mutex **out, hx_os_error *error)
{
	hx_os_mutex *mutex;
	hx_os_status status;

	if (!out || (recursive != 0 && recursive != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	mutex = hx_os_i_zero(context, sizeof(*mutex));
	if (!mutex)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	mutex->recursive = recursive;

	status = hx_os_i_mutex_init(mutex, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(mutex);

		return status;
	}

	*out = mutex;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_condition_create(
	hx_os_context *context, hx_os_condition **out, hx_os_error *error)
{
	hx_os_condition *condition;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	condition = hx_os_i_zero(context, sizeof(*condition));
	if (!condition)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_i_condition_init(condition, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(condition);

		return status;
	}

	*out = condition;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_event_create(hx_os_context *context, int manual_reset,
	int signaled, hx_os_event **out, hx_os_error *error)
{
	hx_os_event *event;
	hx_os_status status;

	if (!out || (manual_reset != 0 && manual_reset != 1) ||
		(signaled != 0 && signaled != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	event = hx_os_i_zero(context, sizeof(*event));
	if (!event)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_mutex_create(context, 0, &event->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &event->condition, error);

	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(event->mutex);
		hx_os_memory_free(event);

		return status;
	}

	event->manual = manual_reset;
	event->signaled = signaled;
	*out = event;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_event_set(hx_os_event *event)
{
	(void)hx_os_mutex_lock(event->mutex, NULL);
	event->signaled = 1;
	if (event->manual)
		hx_os_condition_notify_all(event->condition);
	else
		hx_os_condition_notify_one(event->condition);

	(void)hx_os_mutex_unlock(event->mutex, NULL);
}

void hx_os_event_reset(hx_os_event *event)
{
	(void)hx_os_mutex_lock(event->mutex, NULL);
	event->signaled = 0;
	(void)hx_os_mutex_unlock(event->mutex, NULL);
}

hx_os_status hx_os_event_wait_until(
	hx_os_event *event, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!event)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_mutex_lock(event->mutex, error);
	if (status != HX_OS_OK)
		return status;

	while (!event->signaled) {
		status = hx_os_condition_wait_until(
			event->condition, event->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}

	if (event->signaled) {
		if (!event->manual)
			event->signaled = 0;

		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	(void)hx_os_mutex_unlock(event->mutex, NULL);

	return status;
}

void hx_os_event_destroy(hx_os_event *event)
{
	if (!event)
		return;

	hx_os_condition_destroy(event->condition);
	hx_os_mutex_destroy(event->mutex);
	hx_os_memory_free(event);
}

static hx_os_status hx_os_thread_create_internal(hx_os_context *context,
	size_t stack_size, hx_os_thread_fn function, void *user,
	hx_os_thread **out, hx_os_error *error)
{
	hx_os_thread *thread;
	hx_os_status status;

	if (!out || !function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	thread = hx_os_i_zero(context, sizeof(*thread));
	if (!thread)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	thread->function = function;
	thread->user = user;

	status = hx_os_i_thread_start(thread, stack_size, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(thread);

		return status;
	}

	*out = thread;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_thread_create(hx_os_context *context,
	hx_os_thread_fn function, void *user, hx_os_thread **out,
	hx_os_error *error)
{
	return hx_os_thread_create_internal(
		context, 0, function, user, out, error);
}

hx_os_status hx_os_thread_options_init(
	hx_os_thread_options *options, size_t capacity)
{
	hx_os_thread_options value = HX_OS_THREAD_OPTIONS_INIT;

	if (!options || capacity < sizeof(value))
		return HX_OS_INVALID;

	memcpy(options, &value, sizeof(value));

	return HX_OS_OK;
}

hx_os_status hx_os_thread_create_with_options(hx_os_context *context,
	const hx_os_thread_options *options, hx_os_thread_fn function,
	void *user, hx_os_thread **out, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	if (out)
		*out = NULL;

	if (options &&
		(options->size < sizeof(*options) || options->version != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_thread_create_internal(context,
		options ? options->stack_size : 0, function, user, out, error);
#else
	(void)context;
	(void)options;
	(void)function;
	(void)user;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_thread_join_until(hx_os_thread *thread,
	hx_os_deadline deadline, void **result, hx_os_error *error)
{
	int64_t expected = 0;
	hx_os_status status;

	if (!thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_atomic_load(&thread->joined, HX_OS_ACQUIRE)) {
		if (result)
			*result = thread->result;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	if (hx_os_thread_is_current(thread))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	if (!hx_os_atomic_compare_exchange(&thread->joining, &expected, 1,
		    HX_OS_ACQUIRE, HX_OS_RELAXED))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	/*
	 * Recheck after claiming: another joiner may have finished between
	 * our first check and this claim. Native join must happen only once.
	 */
	if (hx_os_atomic_load(&thread->joined, HX_OS_ACQUIRE))
		status = HX_OS_OK;
	else
		status = hx_os_i_thread_join(thread, deadline, error);

	if (status == HX_OS_OK) {
		hx_os_atomic_store(&thread->joined, 1, HX_OS_RELEASE);
		if (result)
			*result = thread->result;

		hx_os_error_clear(error);
	}

	hx_os_atomic_store(&thread->joining, 0, HX_OS_RELEASE);

	return status;
}

hx_os_status hx_os_thread_destroy(hx_os_thread **thread, hx_os_error *error)
{
	hx_os_status status;

	if (!thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*thread)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (!hx_os_atomic_load(&(*thread)->joined, HX_OS_ACQUIRE) ||
		hx_os_atomic_load(&(*thread)->joining, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	status = hx_os_i_thread_dispose(*thread, error);
	if (status != HX_OS_OK)
		return status;

	hx_os_memory_free(*thread);
	*thread = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_tls_cleanup(void *value)
{
	hx_os_i_tls_value *slot = value;
	hx_os_tls_key *key;

	if (!slot)
		return;

	key = slot->key;
	(void)hx_os_i_tls_set_native(key, NULL, NULL);
	if (key->destructor)
		key->destructor(slot->value, key->user);

	hx_os_memory_free(slot);
	(void)hx_os_atomic_fetch_add(&key->values, -1, HX_OS_RELEASE);
}

hx_os_status hx_os_tls_key_create(hx_os_context *context,
	hx_os_tls_destructor destructor, void *user, hx_os_tls_key **out,
	hx_os_error *error)
{
	hx_os_tls_key *key;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	key = hx_os_i_zero(context, sizeof(*key));
	if (!key)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	key->context = context;
	key->destructor = destructor;
	key->user = user;

	status = hx_os_i_tls_init(key, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(key);

		return status;
	}

	*out = key;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void *hx_os_tls_get(hx_os_tls_key *key)
{
	hx_os_i_tls_value *slot = hx_os_i_tls_get_native(key);

	return slot ? slot->value : NULL;
}

hx_os_status hx_os_tls_set(hx_os_tls_key *key, void *value, hx_os_error *error)
{
	hx_os_i_tls_value *slot;
	hx_os_status status;

	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	slot = hx_os_i_tls_get_native(key);
	if (slot && value) {
		slot->value = value;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	if (!value) {
		status = hx_os_i_tls_set_native(key, NULL, error);
		if (status == HX_OS_OK && slot) {
			hx_os_memory_free(slot);
			(void)hx_os_atomic_fetch_add(
				&key->values, -1, HX_OS_RELEASE);
		}

		return status;
	}

	slot = hx_os_i_zero(key->context, sizeof(*slot));
	if (!slot)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	slot->key = key;
	slot->value = value;

	status = hx_os_i_tls_set_native(key, slot, error);
	if (status != HX_OS_OK)
		hx_os_memory_free(slot);
	else
		(void)hx_os_atomic_fetch_add(&key->values, 1, HX_OS_RELAXED);

	return status;
}

void hx_os_tls_clear(hx_os_tls_key *key)
{
	hx_os_i_tls_cleanup(hx_os_i_tls_get_native(key));
}

hx_os_status hx_os_tls_key_destroy(hx_os_tls_key **key, hx_os_error *error)
{
	hx_os_status status;

	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*key)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_atomic_load(&(*key)->values, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_i_tls_dispose(*key, error);
	if (status != HX_OS_OK)
		return status;

	hx_os_memory_free(*key);
	*key = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
