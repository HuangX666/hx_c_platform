#include "thread_private.h"

struct hx_os_semaphore {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	uint32_t count;
	uint32_t maximum;
};

static hx_os_status hx_os_thread_schedule(hx_os_thread *thread, int priority,
	int level, uint64_t mask, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	if ((priority &&
		    (level < HX_OS_THREAD_PRIORITY_IDLE ||
			    level > HX_OS_THREAD_PRIORITY_TIME_CRITICAL)) ||
		(!priority && !mask))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (thread && hx_os_atomic_load(&thread->joined, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	return priority ? hx_os_i_thread_priority(thread, level, error)
			: hx_os_i_thread_affinity(thread, mask, error);
#else
	(void)thread;
	(void)priority;
	(void)level;
	(void)mask;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_thread_set_priority(
	hx_os_thread *thread, int level, hx_os_error *error)
{
	if (!thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_thread_schedule(thread, 1, level, 0, error);
}

hx_os_status hx_os_thread_set_current_priority(int level, hx_os_error *error)
{
	return hx_os_thread_schedule(NULL, 1, level, 0, error);
}

hx_os_status hx_os_thread_set_affinity(
	hx_os_thread *thread, uint64_t mask, hx_os_error *error)
{
	if (!thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_thread_schedule(thread, 0, 0, mask, error);
}

hx_os_status hx_os_thread_set_current_affinity(
	uint64_t mask, hx_os_error *error)
{
	return hx_os_thread_schedule(NULL, 0, 0, mask, error);
}

static hx_os_status hx_os_thread_name(
	hx_os_thread *thread, const char *name, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	size_t invalid;
	hx_os_status status;

	if (!name)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_validate_utf8(name, strlen(name), &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (thread && hx_os_atomic_load(&thread->joined, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	return hx_os_i_thread_name(thread, name, error);
#else
	(void)thread;
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_thread_set_name(
	hx_os_thread *thread, const char *name, hx_os_error *error)
{
	if (!thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_thread_name(thread, name, error);
}

hx_os_status hx_os_thread_set_current_name(const char *name, hx_os_error *error)
{
	return hx_os_thread_name(NULL, name, error);
}

hx_os_status hx_os_semaphore_create(hx_os_context *context, uint32_t initial,
	uint32_t maximum, hx_os_semaphore **out, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	hx_os_semaphore *semaphore;
	hx_os_status status;

	if (!out || !maximum || initial > maximum)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	semaphore = hx_os_i_zero(context, sizeof(*semaphore));
	if (!semaphore)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_mutex_create(context, 0, &semaphore->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &semaphore->condition, error);

	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(semaphore->mutex);
		hx_os_memory_free(semaphore);

		return status;
	}

	semaphore->count = initial;
	semaphore->maximum = maximum;
	*out = semaphore;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)initial;
	(void)maximum;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_semaphore_post(
	hx_os_semaphore *semaphore, uint32_t count, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	hx_os_status status;

	if (!semaphore || !count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_mutex_lock(semaphore->mutex, error);
	if (status != HX_OS_OK)
		return status;

	if (count > semaphore->maximum - semaphore->count)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else {
		semaphore->count += count;
		if (count > 1)
			hx_os_condition_notify_all(semaphore->condition);
		else
			hx_os_condition_notify_one(semaphore->condition);
	}

	(void)hx_os_mutex_unlock(semaphore->mutex, NULL);

	return status;
#else
	(void)semaphore;
	(void)count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_semaphore_wait_until(
	hx_os_semaphore *semaphore, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_THREAD_EX
	hx_os_status status;

	if (!semaphore)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_mutex_lock(semaphore->mutex, error);
	if (status != HX_OS_OK)
		return status;

	while (!semaphore->count) {
		status = hx_os_condition_wait_until(semaphore->condition,
			semaphore->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}

	if (semaphore->count) {
		--semaphore->count;

		status = hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	(void)hx_os_mutex_unlock(semaphore->mutex, NULL);

	return status;
#else
	(void)semaphore;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_semaphore_destroy(hx_os_semaphore *semaphore)
{
	if (!semaphore)
		return;

	hx_os_condition_destroy(semaphore->condition);
	hx_os_mutex_destroy(semaphore->mutex);
	hx_os_memory_free(semaphore);
}
