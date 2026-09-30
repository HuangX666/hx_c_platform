#include "thread_private.h"

#include <process.h>

hx_os_status hx_os_i_mutex_init(hx_os_mutex *mutex, hx_os_error *error)
{
	if (mutex->recursive) {
		if (!InitializeCriticalSectionEx(
			    &mutex->native.recursive, 0, 0))
			return hx_os_i_win32(error, GetLastError());
	}
	else
		InitializeSRWLock(&mutex->native.normal);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_mutex_lock(hx_os_mutex *mutex, hx_os_error *error)
{
	if (mutex->recursive)
		EnterCriticalSection(&mutex->native.recursive);
	else
		AcquireSRWLockExclusive(&mutex->native.normal);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_mutex_try_lock(hx_os_mutex *mutex, hx_os_error *error)
{
	int acquired = mutex->recursive
		? TryEnterCriticalSection(&mutex->native.recursive)
		: TryAcquireSRWLockExclusive(&mutex->native.normal);
	return hx_os_i_error(error, acquired ? HX_OS_OK : HX_OS_BUSY, 0, 0);
}

hx_os_status hx_os_mutex_unlock(hx_os_mutex *mutex, hx_os_error *error)
{
	if (mutex->recursive)
		LeaveCriticalSection(&mutex->native.recursive);
	else
		ReleaseSRWLockExclusive(&mutex->native.normal);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_mutex_destroy(hx_os_mutex *mutex)
{
	if (!mutex)
		return;
	if (mutex->recursive)
		DeleteCriticalSection(&mutex->native.recursive);
	hx_os_memory_free(mutex);
}

hx_os_status hx_os_i_condition_init(
	hx_os_condition *condition, hx_os_error *error)
{
	InitializeConditionVariable(&condition->native);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_condition_wait_until(hx_os_condition *condition,
	hx_os_mutex *mutex, hx_os_deadline deadline, hx_os_error *error)
{
	DWORD code;
	if (mutex->recursive)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_deadline_expired(deadline))
		return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	if (SleepConditionVariableSRW(&condition->native, &mutex->native.normal,
		    hx_os_i_wait_ms(deadline), 0))
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	code = GetLastError();
	if (code == ERROR_TIMEOUT) {
		/* Early OS timeout (including suspend) is a spurious wakeup. */
		return hx_os_i_error(error,
			hx_os_deadline_expired(deadline) ? HX_OS_TIMEOUT
							 : HX_OS_OK,
			0, 0);
	}
	return hx_os_i_win32(error, code);
}

void hx_os_condition_notify_one(hx_os_condition *condition)
{
	WakeConditionVariable(&condition->native);
}

void hx_os_condition_notify_all(hx_os_condition *condition)
{
	WakeAllConditionVariable(&condition->native);
}

void hx_os_condition_destroy(hx_os_condition *condition)
{
	hx_os_memory_free(condition);
}

static unsigned int __stdcall hx_os_native_thread_entry(void *argument)
{
	hx_os_thread *thread = argument;
	hx_os_atomic_store(&thread->started, 1, HX_OS_RELEASE);
	thread->result = thread->function(thread->user);
	return 0;
}

hx_os_status hx_os_i_thread_start(hx_os_thread *thread, hx_os_error *error)
{
	thread->native.handle = (HANDLE)_beginthreadex(NULL, 0,
		hx_os_native_thread_entry, thread, 0, &thread->native.id);
	if (!thread->native.handle)
		return hx_os_i_errno(error, errno);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_thread_join(
	hx_os_thread *thread, hx_os_deadline deadline, hx_os_error *error)
{
	for (;;) {
		DWORD result = WaitForSingleObject(
			thread->native.handle, hx_os_i_wait_ms(deadline));
		if (result == WAIT_OBJECT_0)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (result == WAIT_FAILED)
			return hx_os_i_win32(error, GetLastError());
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	}
}

hx_os_status hx_os_i_thread_dispose(hx_os_thread *thread, hx_os_error *error)
{
	if (!CloseHandle(thread->native.handle))
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_thread_is_current(hx_os_thread *thread)
{
	return GetCurrentThreadId() == thread->native.id;
}

uint64_t hx_os_thread_id(void)
{
	return GetCurrentThreadId();
}

static VOID CALLBACK hx_os_native_tls_cleanup(void *value)
{
	hx_os_i_tls_cleanup(value);
}

hx_os_status hx_os_i_tls_init(hx_os_tls_key *key, hx_os_error *error)
{
	key->native = FlsAlloc(hx_os_native_tls_cleanup);
	if (key->native == FLS_OUT_OF_INDEXES)
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void *hx_os_i_tls_get_native(hx_os_tls_key *key)
{
	return FlsGetValue(key->native);
}

hx_os_status hx_os_i_tls_set_native(
	hx_os_tls_key *key, void *value, hx_os_error *error)
{
	if (!FlsSetValue(key->native, value))
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_tls_dispose(hx_os_tls_key *key, hx_os_error *error)
{
	if (!FlsFree(key->native))
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
