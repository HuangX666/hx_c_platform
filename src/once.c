#include "coord_private.h"
#include "hx_os_once.h"

struct hx_os_once {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	uint64_t owner;
	size_t users;
	int state;
};

int hx_os_once_available(void)
{
	return HX_OS_ENABLE_ONCE;
}

hx_os_status hx_os_once_create(
	hx_os_context *context, hx_os_once **out, hx_os_error *error)
{
	hx_os_once *once;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_ONCE)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	once = hx_os_i_zero(context, sizeof(*once));
	if (!once)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_mutex_create(context, 0, &once->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &once->condition, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(once->mutex);
		hx_os_memory_free(once);
		return status;
	}
	*out = once;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_once_run(hx_os_once *once, hx_os_once_fn function,
	void *user, hx_os_error *error)
{
	return hx_os_once_run_until(
		once, function, user, HX_OS_DEADLINE_INFINITE, NULL, error);
}

hx_os_status hx_os_once_run_until(hx_os_once *once, hx_os_once_fn function,
	void *user, hx_os_deadline deadline, hx_os_cancel_token *cancel,
	hx_os_error *error)
{
	hx_os_cancel_watch watch;
	uint64_t thread = hx_os_thread_id();
	hx_os_status status;
	int initialize = 0;

	if (!HX_OS_ENABLE_ONCE)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!once || !function || !thread)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(once->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (once->state == 2) {
		(void)hx_os_mutex_unlock(once->mutex, NULL);
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	status = hx_os_i_cancel_watch(
		&watch, cancel, once->mutex, once->condition, error);
	if (status != HX_OS_OK) {
		(void)hx_os_mutex_unlock(once->mutex, NULL);
		return status;
	}
	++once->users;
	for (;;) {
		if (once->state == 2) {
			status = hx_os_i_error(error, HX_OS_OK, 0, 0);
			break;
		}
		if (once->state == 1 && once->owner == thread) {
			status = hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
			break;
		}
		if (hx_os_cancel_requested(cancel)) {
			status = hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
			break;
		}
		if (!once->state) {
			once->state = 1;
			once->owner = thread;
			initialize = 1;
			break;
		}
		status = hx_os_condition_wait_until(
			once->condition, once->mutex, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	(void)hx_os_mutex_unlock(once->mutex, NULL);
	if (initialize) {
		hx_os_error_clear(error);
		status = function(user, error);
		if (error)
			error->status = status;
		(void)hx_os_mutex_lock(once->mutex, NULL);
		once->owner = 0;
		once->state = status == HX_OS_OK ? 2 : 0;
		hx_os_condition_notify_all(once->condition);
		(void)hx_os_mutex_unlock(once->mutex, NULL);
	}
	hx_os_i_cancel_unwatch(&watch);
	(void)hx_os_mutex_lock(once->mutex, NULL);
	--once->users;
	(void)hx_os_mutex_unlock(once->mutex, NULL);
	return status;
}

hx_os_status hx_os_once_destroy(hx_os_once **once, hx_os_error *error)
{
	hx_os_once *value;
	hx_os_status status;

	if (!once)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *once;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_mutex_try_lock(value->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (value->users || value->state == 1)
		status = HX_OS_BUSY;
	(void)hx_os_mutex_unlock(value->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	*once = NULL;
	hx_os_condition_destroy(value->condition);
	hx_os_mutex_destroy(value->mutex);
	hx_os_memory_free(value);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
