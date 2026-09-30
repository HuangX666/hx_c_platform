#include "internal.h"

#include <windows.h>

struct hx_os_file {
	HANDLE handle;
	int append;
};
struct hx_os_directory {
	HANDLE handle;
	WIN32_FIND_DATAW entry;
	int pending;
	int ended;
};
int hx_os_i_file_flags_valid(uint32_t flags);

wchar_t *hx_os_i_windows_utf16(
	hx_os_context *context, const char *text, hx_os_error *error)
{
	int count;
	wchar_t *input;
	DWORD code;
	count = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
	if (!count) {
		(void)hx_os_i_win32(error, GetLastError());
		return NULL;
	}
	input = hx_os_i_alloc(context, (size_t)count * sizeof(*input), 0);
	if (!input) {
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		return NULL;
	}
	if (!MultiByteToWideChar(
		    CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, input, count)) {
		code = GetLastError();
		hx_os_memory_free(input);
		(void)hx_os_i_win32(error, code);
		return NULL;
	}
	return input;
}

wchar_t *hx_os_i_windows_path(
	hx_os_context *context, const char *path, hx_os_error *error)
{
	wchar_t *input;
	wchar_t *full;
	wchar_t *result;
	DWORD needed;
	DWORD written;
	DWORD code;
	size_t prefix;
	if (hx_os_i_path_validate(path, error) != HX_OS_OK)
		return NULL;
	input = hx_os_i_windows_utf16(context, path, error);
	if (!input)
		return NULL;
	if (!wcsncmp(input, L"\\\\?\\", 4) || !wcsncmp(input, L"\\\\.\\", 4))
		return input;
	needed = GetFullPathNameW(input, 0, NULL, NULL);
	if (!needed || needed > 32750) {
		code = needed ? ERROR_FILENAME_EXCED_RANGE : GetLastError();
		hx_os_memory_free(input);
		(void)hx_os_i_win32(error, code);
		return NULL;
	}
	full = hx_os_i_alloc(context, ((size_t)needed + 1) * sizeof(*full), 0);
	result = hx_os_i_alloc(
		context, ((size_t)needed + 9) * sizeof(*result), 0);
	if (!full || !result) {
		hx_os_memory_free(input);
		hx_os_memory_free(full);
		hx_os_memory_free(result);
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		return NULL;
	}
	written = GetFullPathNameW(input, needed + 1, full, NULL);
	code = !written ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;
	hx_os_memory_free(input);
	if (!written || written > needed) {
		hx_os_memory_free(full);
		hx_os_memory_free(result);
		(void)hx_os_i_win32(error, code);
		return NULL;
	}
	if (!wcsncmp(full, L"\\\\?\\", 4) || !wcsncmp(full, L"\\\\.\\", 4)) {
		hx_os_memory_free(result);
		return full;
	}
	prefix = !wcsncmp(full, L"\\\\", 2) ? 8 : 4;
	memcpy(result, prefix == 8 ? L"\\\\?\\UNC\\" : L"\\\\?\\",
		prefix * sizeof(*result));
	memcpy(result + prefix, full + (prefix == 8 ? 2 : 0),
		(wcslen(full + (prefix == 8 ? 2 : 0)) + 1) * sizeof(*result));
	hx_os_memory_free(full);
	return result;
}

hx_os_status hx_os_file_open(hx_os_context *context, const char *path,
	uint32_t flags, hx_os_file **out, hx_os_error *error)
{
	hx_os_file *file;
	hx_os_error local;
	wchar_t *wide;
	DWORD access = 0;
	DWORD creation = OPEN_EXISTING;
	DWORD code;
	if (!out || !hx_os_i_file_flags_valid(flags))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(context, path, error);
	if (!wide)
		return error->status;
	file = hx_os_i_zero(context, sizeof(*file));
	if (!file) {
		hx_os_memory_free(wide);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (flags & HX_OS_FILE_READ)
		access |= GENERIC_READ;
	if (flags & HX_OS_FILE_WRITE)
		access |= GENERIC_WRITE;
	if (flags & HX_OS_FILE_EXCLUSIVE)
		creation = CREATE_NEW;
	else if (flags & HX_OS_FILE_TRUNCATE)
		creation = flags & HX_OS_FILE_CREATE ? CREATE_ALWAYS
						     : TRUNCATE_EXISTING;
	else if (flags & HX_OS_FILE_CREATE)
		creation = OPEN_ALWAYS;
	file->handle = CreateFileW(wide, access,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		creation, FILE_ATTRIBUTE_NORMAL, NULL);
	code = file->handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(wide);
	if (!code && GetFileType(file->handle) != FILE_TYPE_DISK) {
		(void)CloseHandle(file->handle);
		code = ERROR_NOT_SUPPORTED;
	}
	if (code) {
		hx_os_memory_free(file);
		return hx_os_i_win32(error, code);
	}
	file->append = !!(flags & HX_OS_FILE_APPEND);
	*out = file;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_read_some(hx_os_file *file, void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	DWORD count = 0;
	DWORD requested = bytes > INT_MAX ? INT_MAX : (DWORD)bytes;
	if (!file || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (!ReadFile(file->handle, data, requested, &count, NULL))
		return hx_os_i_win32(error, GetLastError());
	*completed = count;
	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_EOF, 0, 0);
}

hx_os_status hx_os_file_write_some(hx_os_file *file, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	OVERLAPPED position;
	DWORD count = 0;
	DWORD requested = bytes > INT_MAX ? INT_MAX : (DWORD)bytes;
	if (!file || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	memset(&position, 0, sizeof(position));
	position.Offset = UINT32_MAX;
	position.OffsetHigh = UINT32_MAX;
	/*
	 * Synchronous handles keep OVERLAPPED alive until WriteFile returns.
	 * End-of-file offsets preserve append while GENERIC_WRITE permits sync.
	 */
	if (!WriteFile(file->handle, data, requested, &count,
		    file->append ? &position : NULL))
		return hx_os_i_win32(error, GetLastError());
	*completed = count;
	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_IO_ERROR, 0, 0);
}

hx_os_status hx_os_file_sync(hx_os_file *file, hx_os_error *error)
{
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_win32(
		error, FlushFileBuffers(file->handle) ? 0 : GetLastError());
}

hx_os_status hx_os_file_close(hx_os_file **file, hx_os_error *error)
{
	DWORD code;
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*file)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	code = CloseHandle((*file)->handle) ? 0 : GetLastError();
	hx_os_memory_free(*file);
	*file = NULL;
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_file_size(
	hx_os_file *file, uint64_t *size, hx_os_error *error)
{
	LARGE_INTEGER value;
	if (!file || !size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!GetFileSizeEx(file->handle, &value))
		return hx_os_i_win32(error, GetLastError());
	*size = (uint64_t)value.QuadPart;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_seek(hx_os_file *file, int64_t offset, int origin,
	uint64_t *position, hx_os_error *error)
{
	LARGE_INTEGER distance;
	LARGE_INTEGER result;
	DWORD method = origin == 0 ? FILE_BEGIN
		: origin == 1	   ? FILE_CURRENT
				   : FILE_END;
	if (!file || !position || origin < 0 || origin > 2)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	distance.QuadPart = offset;
	if (!SetFilePointerEx(file->handle, distance, &result, method))
		return hx_os_i_win32(error, GetLastError());
	*position = (uint64_t)result.QuadPart;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_file_truncate(
	hx_os_file *file, uint64_t size, hx_os_error *error)
{
	FILE_END_OF_FILE_INFO info;
	if (!file)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (size > INT64_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	info.EndOfFile.QuadPart = (LONGLONG)size;
	if (!SetFileInformationByHandle(
		    file->handle, FileEndOfFileInfo, &info, sizeof(info)))
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_path_stat(const char *path, int follow_links,
	hx_os_file_info *info, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *wide;
	HANDLE handle;
	BY_HANDLE_FILE_INFORMATION native;
	ULARGE_INTEGER ticks;
	DWORD code;
	if (!info)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(NULL, path, error);
	if (!wide)
		return error->status;
	handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS |
			(follow_links ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
		NULL);
	code = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(wide);
	if (code)
		return hx_os_i_win32(error, code);
	code = GetFileInformationByHandle(handle, &native) ? 0 : GetLastError();
	(void)CloseHandle(handle);
	if (code)
		return hx_os_i_win32(error, code);
	memset(info, 0, sizeof(*info));
	info->size =
		((uint64_t)native.nFileSizeHigh << 32) | native.nFileSizeLow;
	info->kind = !follow_links &&
			(native.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		? HX_OS_FILE_LINK
		: (native.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		? HX_OS_FILE_DIRECTORY
		: HX_OS_FILE_REGULAR;
	ticks.LowPart = native.ftLastWriteTime.dwLowDateTime;
	ticks.HighPart = native.ftLastWriteTime.dwHighDateTime;
	info->modified_seconds =
		(int64_t)(ticks.QuadPart / 10000000) - INT64_C(11644473600);
	info->modified_nanoseconds =
		(uint32_t)(ticks.QuadPart % 10000000) * 100;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_path_operation(
	const char *path, int operation, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *wide;
	BOOL result;
	DWORD code;
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(NULL, path, error);
	if (!wide)
		return error->status;
	if (operation == 0)
		result = DeleteFileW(wide);
	else if (operation == 1)
		result = CreateDirectoryW(wide, NULL);
	else
		result = RemoveDirectoryW(wide);
	code = result ? 0 : GetLastError();
	hx_os_memory_free(wide);
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_file_remove(const char *path, hx_os_error *error)
{
	return hx_os_path_operation(path, 0, error);
}

hx_os_status hx_os_i_directory_create_one(const char *path, hx_os_error *error)
{
	return hx_os_path_operation(path, 1, error);
}

hx_os_status hx_os_directory_remove(const char *path, hx_os_error *error)
{
	return hx_os_path_operation(path, 2, error);
}

hx_os_status hx_os_file_move(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *first;
	wchar_t *second;
	DWORD code;
	if (!error)
		error = &local;
	first = hx_os_i_windows_path(NULL, from, error);
	if (!first)
		return error->status;
	second = hx_os_i_windows_path(NULL, to, error);
	if (!second) {
		hx_os_memory_free(first);
		return error->status;
	}
	code = MoveFileExW(
		       first, second, replace ? MOVEFILE_REPLACE_EXISTING : 0)
		? 0
		: GetLastError();
	hx_os_memory_free(first);
	hx_os_memory_free(second);
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_directory_open(hx_os_context *context, const char *path,
	hx_os_directory **out, hx_os_error *error)
{
	hx_os_directory *directory;
	hx_os_error local;
	hx_os_file_info info;
	wchar_t *wide;
	wchar_t *pattern;
	size_t length;
	DWORD code;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!error)
		error = &local;
	status = hx_os_path_stat(path, 1, &info, error);
	if (status != HX_OS_OK)
		return status;
	if (info.kind != HX_OS_FILE_DIRECTORY)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	wide = hx_os_i_windows_path(context, path, error);
	if (!wide)
		return error->status;
	length = wcslen(wide);
	pattern = hx_os_i_alloc(context, (length + 3) * sizeof(*pattern), 0);
	directory = hx_os_i_zero(context, sizeof(*directory));
	if (!pattern || !directory) {
		hx_os_memory_free(wide);
		hx_os_memory_free(pattern);
		hx_os_memory_free(directory);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	memcpy(pattern, wide, length * sizeof(*pattern));
	pattern[length] = L'\\';
	pattern[length + 1] = L'*';
	pattern[length + 2] = 0;
	directory->handle = FindFirstFileW(pattern, &directory->entry);
	code = directory->handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(wide);
	hx_os_memory_free(pattern);
	if (code && code != ERROR_FILE_NOT_FOUND) {
		hx_os_memory_free(directory);
		return hx_os_i_win32(error, code);
	}
	directory->pending = !code;
	directory->ended = !!code;
	*out = directory;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_directory_next(hx_os_directory *directory, char *name,
	size_t capacity, size_t *required, hx_os_error *error)
{
	int bytes;
	DWORD code;
	if (!directory || !required || (!name && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (;;) {
		if (directory->ended)
			return hx_os_i_error(error, HX_OS_EOF, 0, 0);
		if (!directory->pending) {
			if (!FindNextFileW(
				    directory->handle, &directory->entry)) {
				code = GetLastError();
				directory->ended = 1;
				return code == ERROR_NO_MORE_FILES
					? hx_os_i_error(error, HX_OS_EOF, 0, 0)
					: hx_os_i_win32(error, code);
			}
		}
		directory->pending = 1;
		if (wcscmp(directory->entry.cFileName, L".") &&
			wcscmp(directory->entry.cFileName, L".."))
			break;
		directory->pending = 0;
	}
	bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		directory->entry.cFileName, -1, NULL, 0, NULL, NULL);
	if (!bytes) {
		code = GetLastError();
		directory->pending = 0;
		return hx_os_i_error(
			error, HX_OS_INVALID_ENCODING, HX_OS_ERROR_WIN32, code);
	}
	*required = (size_t)bytes;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		    directory->entry.cFileName, -1, name, bytes, NULL, NULL))
		return hx_os_i_win32(error, GetLastError());
	directory->pending = 0;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_directory_close(hx_os_directory *directory)
{
	if (!directory)
		return;
	if (directory->handle != INVALID_HANDLE_VALUE)
		(void)FindClose(directory->handle);
	hx_os_memory_free(directory);
}
