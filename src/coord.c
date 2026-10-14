#include "coord_private.h"

struct hx_os_wait_group {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	size_t count;
	size_t waiters;
};

struct hx_os_rwlock {
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	size_t readers;
	size_t writers_waiting;
	size_t waiters;
	uint64_t writer;
	int writing;
};

static hx_os_status hx_os_coord_init(hx_os_context *context,
	hx_os_mutex **mutex, hx_os_condition **condition, hx_os_error *error)
{
	hx_os_status status = hx_os_mutex_create(context, 0, mutex, error);

	if (status == HX_OS_OK)
		status = hx_os_condition_create(context, condition, error);
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(*mutex);
		*mutex = NULL;
	}
	return status;
}

hx_os_status hx_os_wait_group_create(hx_os_context *context, size_t initial,
	hx_os_wait_group **out, hx_os_error *error)
{
	hx_os_wait_group *group;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_THREAD_EX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	group = hx_os_i_zero(context, sizeof(*group));
	if (!group)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_coord_init(
		context, &group->mutex, &group->condition, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(group);
		return status;
	}
	group->count = initial;
	*out = group;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_wait_group_change(
	hx_os_wait_group *group, size_t count, int add, hx_os_error *error)
{
	hx_os_status status;

	if (!group)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(group->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if ((add && count > SIZE_MAX - group->count) ||
		(!add && count > group->count))
		status = HX_OS_RANGE;
	else if (add && count && !group->count && group->waiters)
		status = HX_OS_BUSY;
	else {
		if (add)
			group->count += count;
		else
			group->count -= count;
		if (!group->count)
			hx_os_condition_notify_all(group->condition);
	}
	(void)hx_os_mutex_unlock(group->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_wait_group_add(
	hx_os_wait_group *group, size_t count, hx_os_error *error)
{
	return hx_os_wait_group_change(group, count, 1, error);
}

hx_os_status hx_os_wait_group_done(
	hx_os_wait_group *group, size_t count, hx_os_error *error)
{
	return hx_os_wait_group_change(group, count, 0, error);
}

hx_os_status hx_os_wait_group_wait_until(hx_os_wait_group *group,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	hx_os_cancel_watch watch;
	hx_os_status status;

	if (!group)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_cancel_watch(
		&watch, token, group->mutex, group->condition, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_mutex_lock(group->mutex, error);
	if (status == HX_OS_OK) {
		++group->waiters;
		for (;;) {
			if (hx_os_cancel_requested(token)) {
				status = hx_os_i_error(
					error, HX_OS_CANCELLED, 0, 0);
				break;
			}
			if (!group->count)
				break;
			status = hx_os_condition_wait_until(group->condition,
				group->mutex, deadline, error);
			if (status != HX_OS_OK)
				break;
		}
		(void)hx_os_mutex_unlock(group->mutex, NULL);
		hx_os_i_cancel_unwatch(&watch);
		(void)hx_os_mutex_lock(group->mutex, NULL);
		--group->waiters;
		(void)hx_os_mutex_unlock(group->mutex, NULL);
	}
	else
		hx_os_i_cancel_unwatch(&watch);
	return status;
}

hx_os_status hx_os_wait_group_destroy(
	hx_os_wait_group **group, hx_os_error *error)
{
	hx_os_wait_group *value;
	hx_os_status status;

	if (!group)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *group;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_mutex_try_lock(value->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (value->count || value->waiters)
		status = HX_OS_BUSY;
	(void)hx_os_mutex_unlock(value->mutex, NULL);
	if (status == HX_OS_OK) {
		hx_os_condition_destroy(value->condition);
		hx_os_mutex_destroy(value->mutex);
		hx_os_memory_free(value);
		*group = NULL;
	}
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_rwlock_create(
	hx_os_context *context, hx_os_rwlock **out, hx_os_error *error)
{
	hx_os_rwlock *lock;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_THREAD_EX)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	lock = hx_os_i_zero(context, sizeof(*lock));
	if (!lock)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_os_coord_init(
		context, &lock->mutex, &lock->condition, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(lock);
		return status;
	}
	*out = lock;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_rwlock_acquire(hx_os_rwlock *lock, int write,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	hx_os_cancel_watch watch;
	hx_os_status status;

	if (!lock)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_cancel_watch(
		&watch, token, lock->mutex, lock->condition, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_mutex_lock(lock->mutex, error);
	if (status == HX_OS_OK) {
		++lock->waiters;
		if (write)
			++lock->writers_waiting;
		for (;;) {
			if (hx_os_cancel_requested(token)) {
				status = hx_os_i_error(
					error, HX_OS_CANCELLED, 0, 0);
				break;
			}
			if (lock->writing &&
				lock->writer == hx_os_thread_id()) {
				status = hx_os_i_error(
					error, HX_OS_DEADLOCK, 0, 0);
				break;
			}
			if (!lock->writing &&
				(write ? !lock->readers
				       : !lock->writers_waiting)) {
				if (write) {
					lock->writing = 1;
					lock->writer = hx_os_thread_id();
				}
				else if (lock->readers == SIZE_MAX)
					status = hx_os_i_error(
						error, HX_OS_RANGE, 0, 0);
				else
					++lock->readers;
				break;
			}
			status = hx_os_condition_wait_until(
				lock->condition, lock->mutex, deadline, error);
			if (status != HX_OS_OK)
				break;
		}
		if (write) {
			--lock->writers_waiting;
			if (!lock->writers_waiting)
				hx_os_condition_notify_all(lock->condition);
		}
		(void)hx_os_mutex_unlock(lock->mutex, NULL);
		hx_os_i_cancel_unwatch(&watch);
		(void)hx_os_mutex_lock(lock->mutex, NULL);
		--lock->waiters;
		(void)hx_os_mutex_unlock(lock->mutex, NULL);
	}
	else
		hx_os_i_cancel_unwatch(&watch);
	return status;
}

hx_os_status hx_os_rwlock_read_until(hx_os_rwlock *lock,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	return hx_os_rwlock_acquire(lock, 0, deadline, token, error);
}

hx_os_status hx_os_rwlock_write_until(hx_os_rwlock *lock,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	return hx_os_rwlock_acquire(lock, 1, deadline, token, error);
}

static hx_os_status hx_os_rwlock_unlock(
	hx_os_rwlock *lock, int write, hx_os_error *error)
{
	hx_os_status status;

	if (!lock)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(lock->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (write ? (!lock->writing || lock->writer != hx_os_thread_id())
		  : !lock->readers)
		status = HX_OS_INVALID;
	else {
		if (write)
			lock->writing = 0;
		else
			--lock->readers;
		hx_os_condition_notify_all(lock->condition);
	}
	(void)hx_os_mutex_unlock(lock->mutex, NULL);
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_rwlock_read_unlock(hx_os_rwlock *lock, hx_os_error *error)
{
	return hx_os_rwlock_unlock(lock, 0, error);
}

hx_os_status hx_os_rwlock_write_unlock(hx_os_rwlock *lock, hx_os_error *error)
{
	return hx_os_rwlock_unlock(lock, 1, error);
}

hx_os_status hx_os_rwlock_destroy(hx_os_rwlock **lock, hx_os_error *error)
{
	hx_os_rwlock *value;
	hx_os_status status;

	if (!lock)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *lock;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_mutex_try_lock(value->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (value->readers || value->writing || value->waiters)
		status = HX_OS_BUSY;
	(void)hx_os_mutex_unlock(value->mutex, NULL);
	if (status == HX_OS_OK) {
		hx_os_condition_destroy(value->condition);
		hx_os_mutex_destroy(value->mutex);
		hx_os_memory_free(value);
		*lock = NULL;
	}
	return hx_os_i_error(error, status, 0, 0);
}
