#include "thread_private.h"

#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

hx_os_status hx_os_i_mutex_init(hx_os_mutex *mutex, hx_os_error *error)
{
	pthread_mutexattr_t attributes;
	int result = pthread_mutexattr_init(&attributes);
	if (result)
		return hx_os_i_errno(error, result);
	if (mutex->recursive)
		result = pthread_mutexattr_settype(
			&attributes, PTHREAD_MUTEX_RECURSIVE);
	if (!result)
		result = pthread_mutex_init(&mutex->native, &attributes);
	(void)pthread_mutexattr_destroy(&attributes);
	return hx_os_i_errno(error, result);
}

hx_os_status hx_os_mutex_lock(hx_os_mutex *mutex, hx_os_error *error)
{
	return hx_os_i_errno(error, pthread_mutex_lock(&mutex->native));
}

hx_os_status hx_os_mutex_try_lock(hx_os_mutex *mutex, hx_os_error *error)
{
	return hx_os_i_errno(error, pthread_mutex_trylock(&mutex->native));
}

hx_os_status hx_os_mutex_unlock(hx_os_mutex *mutex, hx_os_error *error)
{
	return hx_os_i_errno(error, pthread_mutex_unlock(&mutex->native));
}

void hx_os_mutex_destroy(hx_os_mutex *mutex)
{
	if (!mutex)
		return;
	(void)pthread_mutex_destroy(&mutex->native);
	hx_os_memory_free(mutex);
}

hx_os_status hx_os_i_condition_init(
	hx_os_condition *condition, hx_os_error *error)
{
	pthread_condattr_t attributes;
	int result = pthread_condattr_init(&attributes);
	if (result)
		return hx_os_i_errno(error, result);
#if !defined(__APPLE__)
	result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
#endif
	if (!result)
		result = pthread_cond_init(&condition->native, &attributes);
	(void)pthread_condattr_destroy(&attributes);
	return hx_os_i_errno(error, result);
}

hx_os_status hx_os_condition_wait_until(hx_os_condition *condition,
	hx_os_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	int result;
	struct timespec time_value;
	uint64_t now;
	uint64_t left;
	if (mutex->recursive)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (deadline == HX_OS_DEADLINE_INFINITE)
		return hx_os_i_errno(error,
			pthread_cond_wait(&condition->native, &mutex->native));
	now = hx_os_monotonic_ns();
	if (deadline <= now)
		return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	left = deadline - now;
	if (left > UINT64_C(3600000000000))
		left = UINT64_C(3600000000000);
#if defined(__APPLE__)
	time_value.tv_sec = (time_t)(left / 1000000000);
	time_value.tv_nsec = (long)(left % 1000000000);
	result = pthread_cond_timedwait_relative_np(
		&condition->native, &mutex->native, &time_value);
#else
	now += left;
	if (sizeof(time_t) <= 4 && now / 1000000000 > INT32_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	time_value.tv_sec = (time_t)(now / 1000000000);
	time_value.tv_nsec = (long)(now % 1000000000);
	result = pthread_cond_timedwait(
		&condition->native, &mutex->native, &time_value);
#endif
	if (result == ETIMEDOUT)
		return hx_os_i_error(error,
			hx_os_deadline_expired(deadline) ? HX_OS_TIMEOUT
							 : HX_OS_OK,
			0, 0);
	return hx_os_i_errno(error, result);
}

void hx_os_condition_notify_one(hx_os_condition *condition)
{
	(void)pthread_cond_signal(&condition->native);
}

void hx_os_condition_notify_all(hx_os_condition *condition)
{
	(void)pthread_cond_broadcast(&condition->native);
}

void hx_os_condition_destroy(hx_os_condition *condition)
{
	if (!condition)
		return;
	(void)pthread_cond_destroy(&condition->native);
	hx_os_memory_free(condition);
}

static void *hx_os_native_thread_entry(void *argument)
{
	hx_os_thread *thread = argument;
#if defined(__APPLE__)
	thread->native.termination = mach_thread_self();
#endif
	hx_os_atomic_store(&thread->started, 1, HX_OS_RELEASE);
	thread->result = thread->function(thread->user);
	return thread->result;
}

hx_os_status hx_os_i_thread_start(hx_os_thread *thread, hx_os_error *error)
{
	return hx_os_i_errno(error,
		pthread_create(&thread->native.id, NULL,
			hx_os_native_thread_entry, thread));
}

hx_os_status hx_os_i_thread_join(
	hx_os_thread *thread, hx_os_deadline deadline, hx_os_error *error)
{
	if (deadline == HX_OS_DEADLINE_INFINITE)
		return hx_os_i_errno(
			error, pthread_join(thread->native.id, NULL));
#if HX_OS_HAVE_PTHREAD_TRYJOIN
	for (;;) {
		int result = pthread_tryjoin_np(thread->native.id, NULL);
		if (!result)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (result != EBUSY)
			return hx_os_i_errno(error, result);
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(1), NULL);
	}
#elif defined(__APPLE__)
	for (;;) {
		if (hx_os_atomic_load(&thread->started, HX_OS_ACQUIRE)) {
			mach_port_type_t type;
			kern_return_t result = mach_port_type(mach_task_self(),
				thread->native.termination, &type);
			if (result != KERN_SUCCESS)
				return hx_os_i_error(error, HX_OS_IO_ERROR,
					HX_OS_ERROR_MACH, result);
			if (type & MACH_PORT_TYPE_DEAD_NAME)
				return hx_os_i_errno(error,
					pthread_join(thread->native.id, NULL));
		}
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(1), NULL);
	}
#else
	(void)thread;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_thread_dispose(hx_os_thread *thread, hx_os_error *error)
{
#if defined(__APPLE__)
	if (thread->native.termination)
		(void)mach_port_deallocate(
			mach_task_self(), thread->native.termination);
#else
	(void)thread;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_thread_is_current(hx_os_thread *thread)
{
	return pthread_equal(pthread_self(), thread->native.id);
}

uint64_t hx_os_thread_id(void)
{
#if defined(__APPLE__)
	uint64_t value = 0;
	(void)pthread_threadid_np(NULL, &value);
	return value;
#else
	return (uint64_t)syscall(SYS_gettid);
#endif
}

hx_os_status hx_os_i_tls_init(hx_os_tls_key *key, hx_os_error *error)
{
	return hx_os_i_errno(
		error, pthread_key_create(&key->native, hx_os_i_tls_cleanup));
}

void *hx_os_i_tls_get_native(hx_os_tls_key *key)
{
	return pthread_getspecific(key->native);
}

hx_os_status hx_os_i_tls_set_native(
	hx_os_tls_key *key, void *value, hx_os_error *error)
{
	return hx_os_i_errno(error, pthread_setspecific(key->native, value));
}

hx_os_status hx_os_i_tls_dispose(hx_os_tls_key *key, hx_os_error *error)
{
	return hx_os_i_errno(error, pthread_key_delete(key->native));
}
