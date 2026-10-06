#include "net_private.h"
#include "file_private.h"
#include "process_private.h"
#include "thread_private.h"
#include "hx_os_native.h"

int hx_os_native_interop_available(void)
{
	return HX_OS_ENABLE_NATIVE_INTEROP;
}

hx_os_status hx_os_file_native_borrow(
	const hx_os_file *file, uintptr_t *value, hx_os_error *error)
{
#if HX_OS_ENABLE_NATIVE_INTEROP
	if (!file || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

#if defined(_WIN32)
	*value = (uintptr_t)file->handle;
#else
	*value = (uintptr_t)file->descriptor;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)file;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_native_borrow(
	const hx_os_socket *socket, uintptr_t *value, hx_os_error *error)
{
#if HX_OS_ENABLE_NATIVE_INTEROP && HX_OS_ENABLE_NETWORK
	if (!socket || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (socket->loop || hx_os_atomic_load(&socket->pending, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	*value = (uintptr_t)socket->native;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)socket;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_process_native_borrow(
	const hx_os_process *process, uintptr_t *value, hx_os_error *error)
{
#if HX_OS_ENABLE_NATIVE_INTEROP && HX_OS_ENABLE_PROCESS &&                     \
	HX_OS_HAVE_NATIVE_PROCESS
	if (!process || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (process->finished)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

	*value = (uintptr_t)process->native;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)process;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

size_t hx_os_thread_native_size(void)
{
#if HX_OS_ENABLE_NATIVE_INTEROP
#if defined(_WIN32)
	return sizeof(HANDLE);
#else
	return sizeof(pthread_t);
#endif
#else
	return 0;
#endif
}

hx_os_status hx_os_thread_native_borrow(const hx_os_thread *thread,
	void *storage, size_t capacity, hx_os_error *error)
{
#if HX_OS_ENABLE_NATIVE_INTEROP
	if (!thread || !storage)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity < hx_os_thread_native_size())
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	if (hx_os_atomic_load(&thread->joining, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (hx_os_atomic_load(&thread->joined, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);

#if defined(_WIN32)
	memcpy(storage, &thread->native.handle, sizeof(thread->native.handle));
#else
	memcpy(storage, &thread->native.id, sizeof(thread->native.id));
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)thread;
	(void)storage;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
