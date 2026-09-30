#include "file_extra_private.h"

hx_os_status hx_os_i_file_lock(hx_os_file *file, uint64_t offset,
	uint64_t bytes, int shared, int unlock, hx_os_error *error)
{
	OVERLAPPED overlapped;
	BOOL result;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = (DWORD)offset;
	overlapped.OffsetHigh = (DWORD)(offset >> 32);
	if (unlock)
		result = UnlockFileEx(file->handle, 0, (DWORD)bytes,
			(DWORD)(bytes >> 32), &overlapped);
	else
		result = LockFileEx(file->handle,
			LOCKFILE_FAIL_IMMEDIATELY |
				(shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK),
			0, (DWORD)bytes, (DWORD)(bytes >> 32), &overlapped);
	if (!result) {
		DWORD code = GetLastError();
		if (!unlock && code == ERROR_LOCK_VIOLATION)
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK,
				HX_OS_ERROR_WIN32, code);
		return hx_os_i_win32(error, code);
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static HANDLE hx_os_file_attributes_open(
	const char *path, int follow, hx_os_error *error)
{
	wchar_t *wide = hx_os_i_windows_path(NULL, path, error);
	HANDLE file;
	DWORD code;
	if (!wide)
		return INVALID_HANDLE_VALUE;
	file = CreateFileW(wide, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			(follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
		NULL);
	code = GetLastError();
	hx_os_memory_free(wide);
	if (file == INVALID_HANDLE_VALUE)
		(void)hx_os_i_win32(error, code);
	return file;
}

static hx_os_status hx_os_file_native_time(int64_t seconds,
	uint32_t nanoseconds, FILETIME *out, hx_os_error *error)
{
	uint64_t biased, ticks;
	if (seconds < -INT64_C(11644473600))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	biased = seconds < 0 ? (uint64_t)(seconds + INT64_C(11644473600))
			     : (uint64_t)seconds + UINT64_C(11644473600);
	if (biased > (UINT64_MAX - 1 - nanoseconds / 100) / 10000000)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	ticks = biased * 10000000 + nanoseconds / 100;
	out->dwLowDateTime = (DWORD)ticks;
	out->dwHighDateTime = (DWORD)(ticks >> 32);
	return HX_OS_OK;
}

hx_os_status hx_os_i_file_set_times(const char *path,
	const hx_os_file_times *times, int follow, hx_os_error *error)
{
	hx_os_error local;
	FILETIME accessed, modified;
	HANDLE file;
	hx_os_status status = HX_OS_OK;
	DWORD code = 0;
	if (!error)
		error = &local;
	if (times->fields & HX_OS_TIME_ACCESSED)
		status = hx_os_file_native_time(times->accessed_seconds,
			times->accessed_nanoseconds, &accessed, error);
	if (status == HX_OS_OK && (times->fields & HX_OS_TIME_MODIFIED))
		status = hx_os_file_native_time(times->modified_seconds,
			times->modified_nanoseconds, &modified, error);
	if (status != HX_OS_OK)
		return status;
	file = hx_os_file_attributes_open(path, follow, error);
	if (file == INVALID_HANDLE_VALUE)
		return error ? error->status : HX_OS_IO_ERROR;
	if (!SetFileTime(file, NULL,
		    times->fields & HX_OS_TIME_ACCESSED ? &accessed : NULL,
		    times->fields & HX_OS_TIME_MODIFIED ? &modified : NULL))
		code = GetLastError();
	if (!CloseHandle(file) && !code)
		code = GetLastError();
	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_path_permissions(
	const char *path, unsigned permissions, int follow, hx_os_error *error)
{
	hx_os_error local;
	FILE_BASIC_INFO info, update;
	HANDLE file;
	DWORD code = 0;
	if (!error)
		error = &local;
	if (!(permissions & HX_OS_PERMISSION_OWNER_READ) ||
		(permissions & HX_OS_PERMISSION_OWNER_EXECUTE))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	file = hx_os_file_attributes_open(path, follow, error);
	if (file == INVALID_HANDLE_VALUE)
		return error ? error->status : HX_OS_IO_ERROR;
	if (!GetFileInformationByHandleEx(
		    file, FileBasicInfo, &info, sizeof(info)))
		code = GetLastError();
	else {
		memset(&update, 0, sizeof(update));
		update.FileAttributes = info.FileAttributes &
			~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_NORMAL);
		if (!(permissions & HX_OS_PERMISSION_OWNER_WRITE))
			update.FileAttributes |= FILE_ATTRIBUTE_READONLY;
		if (!update.FileAttributes)
			update.FileAttributes = FILE_ATTRIBUTE_NORMAL;
		if (!SetFileInformationByHandle(
			    file, FileBasicInfo, &update, sizeof(update)))
			code = GetLastError();
	}
	if (!CloseHandle(file) && !code)
		code = GetLastError();
	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, HX_OS_OK, 0, 0);
}
