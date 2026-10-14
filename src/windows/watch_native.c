#include "watch_private.h"
#include <windows.h>

typedef struct hx_os_watch_windows {
	HANDLE directory;
	HANDLE stopped;
	HANDLE completed;
	OVERLAPPED operation;
	unsigned char *buffer;
	char *old_name;
	DWORD received;
	int pending;
} hx_os_watch_windows;

static hx_os_status hx_os_watch_arm(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_windows *native = watch->native;
	BOOL result;
	DWORD code;

	memset(&native->operation, 0, sizeof(native->operation));
	native->operation.hEvent = native->completed;
	ResetEvent(native->completed);
	native->received = 0;

	result = ReadDirectoryChangesW(native->directory, native->buffer,
		(DWORD)watch->options.buffer_bytes, watch->recursive,
		FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_ATTRIBUTES |
			FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
		&native->received, &native->operation, NULL);
	if (!result) {
		code = GetLastError();
		if (code != ERROR_IO_PENDING)
			return hx_os_i_win32(error, code);
	}

	native->pending = 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_watch_old_name(hx_os_watch *watch)
{
	hx_os_watch_windows *native = watch->native;

	if (native->old_name) {
		hx_os_i_watch_emit(watch, HX_OS_WATCH_REMOVED, native->old_name,
			NULL, 0, NULL);

		hx_os_i_watch_free(watch, native->old_name);
		native->old_name = NULL;
	}
}

static hx_os_status hx_os_watch_records(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_windows *native = watch->native;
	size_t offset = 0;

	while (offset < native->received && !hx_os_i_watch_stopping(watch)) {
		FILE_NOTIFY_INFORMATION *entry;
		size_t remaining = (size_t)native->received - offset;

		size_t header = offsetof(FILE_NOTIFY_INFORMATION, FileName), i;

		int needed;
		char *path;
		uint32_t kind;

		if (remaining < header)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		entry = (FILE_NOTIFY_INFORMATION *)(native->buffer + offset);
		if ((entry->FileNameLength & 1) ||
			entry->FileNameLength > remaining - header)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			entry->FileName,
			(int)(entry->FileNameLength / sizeof(wchar_t)), NULL, 0,
			NULL, NULL);
		if (needed <= 0) {
			hx_os_error invalid;

			(void)hx_os_i_error(&invalid, HX_OS_INVALID_ENCODING,
				HX_OS_ERROR_WIN32, GetLastError());
			hx_os_i_watch_emit(watch, HX_OS_WATCH_OVERFLOW, NULL,
				NULL, 0, &invalid);
			goto next;
		}

		path = hx_os_i_watch_scratch(watch, (size_t)needed + 1, error);
		if (!path)
			return error->status;

		if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			    entry->FileName,
			    (int)(entry->FileNameLength / sizeof(wchar_t)),
			    path, needed, NULL, NULL))
			return hx_os_i_win32(error, GetLastError());
		path[needed] = '\0';

		for (i = 0; i < (size_t)needed; ++i)
			if (path[i] == '\\')
				path[i] = '/';

		if (entry->Action != FILE_ACTION_RENAMED_NEW_NAME)
			hx_os_watch_old_name(watch);

		if (entry->Action == FILE_ACTION_RENAMED_OLD_NAME) {
			native->old_name = hx_os_i_watch_alloc(
				watch, (size_t)needed + 1, error);
			if (!native->old_name)
				return error->status;

			memcpy(native->old_name, path, (size_t)needed + 1);
		}
		else {
			kind = entry->Action == FILE_ACTION_ADDED
				? HX_OS_WATCH_ADDED
				: entry->Action == FILE_ACTION_REMOVED
				? HX_OS_WATCH_REMOVED
				: entry->Action == FILE_ACTION_RENAMED_NEW_NAME
				? (native->old_name ? HX_OS_WATCH_RENAMED
						    : HX_OS_WATCH_ADDED)
				: HX_OS_WATCH_CHANGED;
			hx_os_i_watch_emit(
				watch, kind, path, native->old_name, 0, NULL);

			hx_os_i_watch_free(watch, native->old_name);
			native->old_name = NULL;
		}

	next:
		if (!entry->NextEntryOffset)
			break;

		if (entry->NextEntryOffset < header + entry->FileNameLength ||
			entry->NextEntryOffset > remaining ||
			(entry->NextEntryOffset & 3))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		offset += entry->NextEntryOffset;
	}

	hx_os_watch_old_name(watch);

	return HX_OS_OK;
}

hx_os_status hx_os_i_watch_native_init(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_windows *native;
	wchar_t *path;
	DWORD code;

	native = hx_os_i_watch_alloc(watch, sizeof(*native), error);
	if (!native)
		return error->status;

	watch->native = native;
	native->directory = INVALID_HANDLE_VALUE;
	native->buffer =
		hx_os_i_watch_alloc(watch, watch->options.buffer_bytes, error);
	if (!native->buffer)
		return error->status;

	path = hx_os_i_windows_path(watch->context, watch->root, error);
	if (!path)
		return error->status;

	native->directory = CreateFileW(path, FILE_LIST_DIRECTORY,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	code = GetLastError();

	hx_os_memory_free(path);
	if (native->directory == INVALID_HANDLE_VALUE)
		return hx_os_i_win32(error, code);

	native->stopped = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!native->stopped)
		return hx_os_i_win32(error, GetLastError());

	native->completed = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!native->completed)
		return hx_os_i_win32(error, GetLastError());

	return hx_os_watch_arm(watch, error);
}

hx_os_status hx_os_i_watch_native_start(hx_os_watch *watch, hx_os_error *error)
{
	// Initial native read is armed before the worker exists.
	(void)watch;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_watch_native_run(hx_os_watch *watch, hx_os_error *error)
{
	hx_os_watch_windows *native = watch->native;
	HANDLE waits[2] = {native->stopped, native->completed};
	hx_os_status status = HX_OS_OK;

	while (!hx_os_i_watch_stopping(watch)) {
		DWORD result =
			WaitForMultipleObjects(2, waits, FALSE, INFINITE);

		if (result == WAIT_OBJECT_0)
			break;

		if (result != WAIT_OBJECT_0 + 1) {
			status = hx_os_i_error(error, HX_OS_IO_ERROR,
				HX_OS_ERROR_WIN32,
				result == WAIT_FAILED ? GetLastError() : 0);
			break;
		}

		if (!GetOverlappedResult(native->directory, &native->operation,
			    &native->received, FALSE)) {
			DWORD code = GetLastError();
			native->pending = code == ERROR_IO_INCOMPLETE;
			if (code != ERROR_NOTIFY_ENUM_DIR) {
				status = hx_os_i_win32(error, code);
				break;
			}

			native->received = 0;
		}

		native->pending = 0;
		if (!native->received)
			hx_os_i_watch_emit(watch, HX_OS_WATCH_OVERFLOW, NULL,
				NULL, 0, NULL);
		else {
			status = hx_os_watch_records(watch, error);
			if (status != HX_OS_OK)
				break;
		}

		if (!hx_os_i_watch_stopping(watch)) {
			status = hx_os_watch_arm(watch, error);
			if (status != HX_OS_OK)
				break;
		}
	}

	// Cancellation must retire before releasing OVERLAPPED/buffer storage.
	if (native->pending) {
		(void)CancelIoEx(native->directory, &native->operation);
		(void)GetOverlappedResult(native->directory, &native->operation,
			&native->received, TRUE);
		native->pending = 0;
	}

	return status == HX_OS_OK ? hx_os_i_error(error, HX_OS_OK, 0, 0)
				  : status;
}

void hx_os_i_watch_native_stop(hx_os_watch *watch)
{
	hx_os_watch_windows *native = watch->native;

	if (native && native->stopped)
		SetEvent(native->stopped);
}

void hx_os_i_watch_native_destroy(hx_os_watch *watch)
{
	hx_os_watch_windows *native = watch->native;

	if (!native)
		return;

	if (native->pending) {
		(void)CancelIoEx(native->directory, &native->operation);
		(void)GetOverlappedResult(native->directory, &native->operation,
			&native->received, TRUE);
	}

	if (native->directory != INVALID_HANDLE_VALUE)
		CloseHandle(native->directory);

	if (native->completed)
		CloseHandle(native->completed);

	if (native->stopped)
		CloseHandle(native->stopped);

	hx_os_i_watch_free(watch, native->old_name);
	hx_os_i_watch_free(watch, native->buffer);
	hx_os_i_watch_free(watch, native);
}
