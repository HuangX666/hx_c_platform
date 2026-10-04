#include "file_private.h"

static HANDLE hx_os_file_positioned(hx_os_file *file, hx_os_error *error)
{
	HANDLE handle = InterlockedCompareExchangePointer(
		&file->positioned, NULL, NULL);

	HANDLE previous;
	DWORD access = 0;

	if (handle)
		return handle;

	if (file->flags & HX_OS_FILE_READ)
		access |= GENERIC_READ;

	if (file->flags & HX_OS_FILE_WRITE)
		access |= GENERIC_WRITE;

	handle = ReOpenFile(file->handle, access,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		FILE_FLAG_OVERLAPPED);
	if (handle == INVALID_HANDLE_VALUE) {
		(void)hx_os_i_win32(error, GetLastError());

		return NULL;
	}

	previous = InterlockedCompareExchangePointer(
		&file->positioned, handle, NULL);
	if (previous) {
		(void)CloseHandle(handle);
		handle = previous;
	}

	return handle;
}

hx_os_status hx_os_i_file_at(hx_os_file *file, int writing, uint64_t offset,
	void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	hx_os_error local;
	HANDLE handle;
	OVERLAPPED operation;
	DWORD transferred = 0, code, close_code;
	BOOL result;

	if (!error)
		error = &local;

	handle = hx_os_file_positioned(file, error);
	if (!handle)
		return error->status;

	memset(&operation, 0, sizeof(operation));
	operation.Offset = (DWORD)offset;
	operation.OffsetHigh = (DWORD)(offset >> 32);
	operation.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!operation.hEvent)
		return hx_os_i_win32(error, GetLastError());

	result = writing ? WriteFile(handle, data, (DWORD)bytes, &transferred,
				   &operation)
			 : ReadFile(handle, data, (DWORD)bytes, &transferred,
				   &operation);
	code = result ? 0 : GetLastError();
	if (code == ERROR_IO_PENDING)
		code = GetOverlappedResult(
			       handle, &operation, &transferred, TRUE)
			? 0
			: GetLastError();

	*completed = transferred;
	close_code = CloseHandle(operation.hEvent) ? 0 : GetLastError();
	if (!writing && code == ERROR_HANDLE_EOF)
		return hx_os_i_error(error, HX_OS_EOF, HX_OS_ERROR_WIN32, code);

	if (code || close_code)
		return hx_os_i_win32(error, code ? code : close_code);

	return hx_os_i_error(error,
		transferred	  ? HX_OS_OK
			: writing ? HX_OS_IO_ERROR
				  : HX_OS_EOF,
		0, 0);
}
