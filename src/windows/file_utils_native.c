#include "file_utils_private.h"

#include <windows.h>

static hx_os_status hx_os_file_wide_pair(const char *first, const char *second,
	wchar_t **a, wchar_t **b, hx_os_error *error)
{
	*a = hx_os_i_windows_path(NULL, first, error);
	if (!*a)
		return error->status;
	*b = hx_os_i_windows_path(NULL, second, error);
	if (!*b) {
		hx_os_memory_free(*a);
		*a = NULL;
		return error->status;
	}
	return HX_OS_OK;
}

static DWORD hx_os_file_identity(
	const wchar_t *path, BY_HANDLE_FILE_INFORMATION *info)
{
	HANDLE handle = CreateFileW(path, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	DWORD code;
	if (handle == INVALID_HANDLE_VALUE)
		return GetLastError();
	code = GetFileType(handle) != FILE_TYPE_DISK	   ? ERROR_NOT_SUPPORTED
		: GetFileInformationByHandle(handle, info) ? 0
							   : GetLastError();
	(void)CloseHandle(handle);
	return code;
}

static int hx_os_file_same(const BY_HANDLE_FILE_INFORMATION *first,
	const BY_HANDLE_FILE_INFORMATION *second)
{
	return first->dwVolumeSerialNumber == second->dwVolumeSerialNumber &&
		first->nFileIndexHigh == second->nFileIndexHigh &&
		first->nFileIndexLow == second->nFileIndexLow;
}

hx_os_status hx_os_i_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *first, *second;
	BY_HANDLE_FILE_INFORMATION source, destination;
	DWORD code;
	hx_os_status status;
	if (!error)
		error = &local;
	status = hx_os_file_wide_pair(from, to, &first, &second, error);
	if (status != HX_OS_OK)
		return status;
	code = hx_os_file_identity(first, &source);
	if (!code && (source.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		code = ERROR_NOT_SUPPORTED;
	if (!code) {
		code = hx_os_file_identity(second, &destination);
		if (!code && hx_os_file_same(&source, &destination))
			code = ERROR_INVALID_PARAMETER;
		else if (code == ERROR_FILE_NOT_FOUND ||
			code == ERROR_PATH_NOT_FOUND)
			code = 0;
	}
	if (!code)
		code = CopyFileW(first, second, !replace) ? 0 : GetLastError();
	hx_os_memory_free(first);
	hx_os_memory_free(second);
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_i_file_replace(const char *target, const char *source,
	const char *backup, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *first, *second, *saved = NULL;
	BY_HANDLE_FILE_INFORMATION source_info, target_info;
	DWORD code, target_code;
	hx_os_status status;
	if (!error)
		error = &local;
	status = hx_os_file_wide_pair(target, source, &first, &second, error);
	if (status != HX_OS_OK)
		return status;
	if (backup) {
		saved = hx_os_i_windows_path(NULL, backup, error);
		if (!saved) {
			hx_os_memory_free(first);
			hx_os_memory_free(second);
			return error->status;
		}
	}
	code = hx_os_file_identity(second, &source_info);
	if (!code) {
		DWORD attributes = GetFileAttributesW(second);
		if (attributes == INVALID_FILE_ATTRIBUTES)
			code = GetLastError();
		else if (attributes &
			(FILE_ATTRIBUTE_DIRECTORY |
				FILE_ATTRIBUTE_REPARSE_POINT))
			code = ERROR_NOT_SUPPORTED;
	}
	target_code = hx_os_file_identity(first, &target_info);
	if (!code && target_code != ERROR_FILE_NOT_FOUND &&
		target_code != ERROR_PATH_NOT_FOUND) {
		code = target_code;
		if (!code && hx_os_file_same(&source_info, &target_info))
			code = ERROR_INVALID_PARAMETER;
		if (!code &&
			source_info.dwVolumeSerialNumber !=
				target_info.dwVolumeSerialNumber)
			code = ERROR_NOT_SAME_DEVICE;
	}
	if (!code && saved) {
		DWORD attributes = GetFileAttributesW(saved);
		if (attributes != INVALID_FILE_ATTRIBUTES)
			code = ERROR_FILE_EXISTS;
		else {
			DWORD missing = GetLastError();
			if (missing != ERROR_FILE_NOT_FOUND &&
				missing != ERROR_PATH_NOT_FOUND)
				code = missing;
		}
	}
	if (!code) {
		if (target_code == ERROR_FILE_NOT_FOUND)
			code = MoveFileExW(
				       second, first, MOVEFILE_REPLACE_EXISTING)
				? 0
				: GetLastError();
		else
			code = ReplaceFileW(first, second, saved, 0, NULL, NULL)
				? 0
				: GetLastError();
	}
	hx_os_memory_free(first);
	hx_os_memory_free(second);
	hx_os_memory_free(saved);
	return hx_os_i_win32(error, code);
}

hx_os_status hx_os_i_file_sync_parent(const char *path, hx_os_error *error)
{
	(void)path;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

static hx_os_status hx_os_file_path_text(
	hx_string *out, wchar_t *wide, size_t length, hx_os_error *error)
{
	if (!wcsncmp(wide, L"\\\\?\\UNC\\", 8)) {
		wide[6] = L'\\';
		wide += 6;
		length -= 6;
	}
	else if (!wcsncmp(wide, L"\\\\?\\", 4)) {
		wide += 4;
		length -= 4;
	}
	return hx_string_assign_encoded(
		out, HX_OS_UTF16LE, wide, length * sizeof(*wide), error);
}

hx_os_status hx_os_i_path_current(hx_string *out, hx_os_error *error)
{
	wchar_t *wide;
	DWORD length, code;
	hx_os_status status;
	wide = hx_os_i_alloc(out->context, 32768 * sizeof(*wide), 0);
	if (!wide)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	length = GetCurrentDirectoryW(32768, wide);
	code = length ? 0 : GetLastError();
	status = code ? hx_os_i_win32(error, code)
		: length >= 32768
		? hx_os_i_error(error, HX_OS_LIMIT, 0, 0)
		: hx_os_file_path_text(out, wide, length, error);
	hx_os_memory_free(wide);
	return status;
}

hx_os_status hx_os_i_path_absolute(
	const char *path, hx_string *out, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *wide, *resolved;
	HANDLE handle;
	DWORD code, length;
	hx_os_status status;
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(out->context, path, error);
	if (!wide)
		return error->status;
	handle = CreateFileW(wide, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	code = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(wide);
	if (code)
		return hx_os_i_win32(error, code);
	resolved = hx_os_i_alloc(out->context, 32768 * sizeof(*resolved), 0);
	if (!resolved) {
		(void)CloseHandle(handle);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	length = GetFinalPathNameByHandleW(handle, resolved, 32768,
		FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	code = length ? 0 : GetLastError();
	(void)CloseHandle(handle);
	if (code)
		status = hx_os_i_win32(error, code);
	else if (length >= 32768)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else
		status = hx_os_file_path_text(out, resolved, length, error);
	hx_os_memory_free(resolved);
	return status;
}

hx_os_status hx_os_i_path_change_current(const char *path, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *wide;
	DWORD code;
	if (!error)
		error = &local;
	wide = hx_os_i_windows_path(NULL, path, error);
	if (!wide)
		return error->status;
	code = SetCurrentDirectoryW(wide) ? 0 : GetLastError();
	hx_os_memory_free(wide);
	return hx_os_i_win32(error, code);
}

typedef struct hx_os_remove_frame {
	HANDLE directory;
	HANDLE enumeration;
	wchar_t *path;
	WIN32_FIND_DATAW entry;
	DWORD attributes;
	int pending;
} hx_os_remove_frame;

static DWORD hx_os_remove_open(hx_os_remove_frame *frame)
{
	BY_HANDLE_FILE_INFORMATION info;
	wchar_t *pattern;
	size_t length;
	DWORD code;
	/* Deny rename/deletion while walking this directory. Never open its
	 * target if this entry is a reparse point. */
	frame->directory = CreateFileW(frame->path, FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
		NULL);
	frame->enumeration = INVALID_HANDLE_VALUE;
	if (frame->directory == INVALID_HANDLE_VALUE)
		return GetLastError();
	if (!GetFileInformationByHandle(frame->directory, &info))
		return GetLastError();
	frame->attributes = info.dwFileAttributes;
	if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
		(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
		return 0;
	length = wcslen(frame->path);
	if (length > 32760)
		return ERROR_FILENAME_EXCED_RANGE;
	pattern = hx_os_i_alloc(NULL, (length + 3) * sizeof(*pattern), 0);
	if (!pattern)
		return ERROR_NOT_ENOUGH_MEMORY;
	memcpy(pattern, frame->path, length * sizeof(*pattern));
	memcpy(pattern + length, L"\\*", 3 * sizeof(*pattern));
	frame->enumeration = FindFirstFileW(pattern, &frame->entry);
	code = frame->enumeration == INVALID_HANDLE_VALUE ? GetLastError() : 0;
	hx_os_memory_free(pattern);
	if (code == ERROR_FILE_NOT_FOUND)
		return 0;
	frame->pending = !code;
	return code;
}

static void hx_os_remove_close(hx_os_remove_frame *frame)
{
	if (frame->enumeration != INVALID_HANDLE_VALUE)
		(void)FindClose(frame->enumeration);
	if (frame->directory != INVALID_HANDLE_VALUE)
		(void)CloseHandle(frame->directory);
}

static DWORD hx_os_remove_entry(const wchar_t *path, DWORD attributes)
{
	BOOL result = attributes & FILE_ATTRIBUTE_DIRECTORY
		? RemoveDirectoryW(path)
		: DeleteFileW(path);
	return result ? 0 : GetLastError();
}

hx_os_status hx_os_i_remove_tree(
	const char *path, const hx_os_walk_options *options, hx_os_error *error)
{
	hx_os_error local;
	hx_os_remove_frame *frames;
	size_t depth = 1, entries = 0, memory;
	DWORD code;
	hx_os_status status = HX_OS_OK;
	if (!error)
		error = &local;
	memory = hx_os_i_allocation_cost(
		options->max_depth * sizeof(*frames), 0);
	if (memory > options->max_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	frames = hx_os_i_zero(NULL, options->max_depth * sizeof(*frames));
	if (!frames)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	frames[0].path = hx_os_i_windows_path(NULL, path, error);
	if (!frames[0].path) {
		hx_os_memory_free(frames);
		return error->status;
	}
	memory += hx_os_i_allocation_bytes(frames[0].path);
	if (memory > options->max_bytes) {
		hx_os_memory_free(frames[0].path);
		hx_os_memory_free(frames);
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	code = hx_os_remove_open(&frames[0]);
	while (depth && !code && status == HX_OS_OK) {
		hx_os_remove_frame *frame = &frames[depth - 1];
		wchar_t *child;
		size_t length, name_bytes, cost;
		int ended = frame->enumeration == INVALID_HANDLE_VALUE;
		if (!ended && !frame->pending &&
			!FindNextFileW(frame->enumeration, &frame->entry)) {
			code = GetLastError();
			if (code != ERROR_NO_MORE_FILES)
				break;
			code = 0;
			ended = 1;
		}
		frame->pending = 0;
		if (ended) {
			hx_os_remove_close(frame);
			code = hx_os_remove_entry(
				frame->path, frame->attributes);
			memory -= hx_os_i_allocation_bytes(frame->path);
			hx_os_memory_free(frame->path);
			--depth;
			continue;
		}
		if (!wcscmp(frame->entry.cFileName, L".") ||
			!wcscmp(frame->entry.cFileName, L".."))
			continue;
		if (entries++ == options->max_entries) {
			status = HX_OS_LIMIT;
			break;
		}
		if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			    frame->entry.cFileName, -1, NULL, 0, NULL, NULL)) {
			code = GetLastError();
			break;
		}
		length = wcslen(frame->path);
		name_bytes =
			(wcslen(frame->entry.cFileName) + 1) * sizeof(wchar_t);
		if (length + name_bytes / sizeof(wchar_t) + 1 > 32767) {
			status = HX_OS_LIMIT;
			break;
		}
		cost = hx_os_i_allocation_cost(
			(length + 1) * sizeof(wchar_t) + name_bytes, 0);
		if (cost > options->max_bytes - memory) {
			status = HX_OS_LIMIT;
			break;
		}
		child = hx_os_i_alloc(
			NULL, (length + 1) * sizeof(wchar_t) + name_bytes, 0);
		if (!child) {
			status = HX_OS_NO_MEMORY;
			break;
		}
		memcpy(child, frame->path, length * sizeof(wchar_t));
		child[length] = L'\\';
		memcpy(child + length + 1, frame->entry.cFileName, name_bytes);
		if (!(frame->entry.dwFileAttributes &
			    FILE_ATTRIBUTE_DIRECTORY) ||
			(frame->entry.dwFileAttributes &
				FILE_ATTRIBUTE_REPARSE_POINT)) {
			code = hx_os_remove_entry(
				child, frame->entry.dwFileAttributes);
			hx_os_memory_free(child);
			continue;
		}
		if (depth == options->max_depth) {
			hx_os_memory_free(child);
			status = HX_OS_LIMIT;
			break;
		}
		memset(&frames[depth], 0, sizeof(frames[depth]));
		frames[depth].path = child;
		memory += cost;
		code = hx_os_remove_open(&frames[depth++]);
	}
	while (depth) {
		hx_os_remove_frame *frame = &frames[--depth];
		hx_os_remove_close(frame);
		hx_os_memory_free(frame->path);
	}
	hx_os_memory_free(frames);
	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, status, 0, 0);
}
