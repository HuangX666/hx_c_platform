#include "format_private.h"
#include <windows.h>
#include <io.h>
#include <stdio.h>

hx_os_status hx_os_i_format_print(
	int stream, const char *text, size_t bytes, hx_os_error *error)
{
	FILE *output = stream == HX_PRINT_STDERR ? stderr : stdout;
	HANDLE handle;
	DWORD mode, code = 0;
	wchar_t *wide = NULL;
	int descriptor, units = 0;
	size_t position = 0;
	hx_os_status status = HX_OS_OK;
	_lock_file(output);
	descriptor = _fileno(output);
	if (descriptor < 0 || fflush(output)) {
		status = hx_os_i_errno(error, descriptor < 0 ? EBADF : errno);
		goto done;
	}
	handle = (HANDLE)_get_osfhandle(descriptor);
	if (GetConsoleMode(handle, &mode)) {
		if (bytes > INT_MAX) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			goto done;
		}
		if (!bytes)
			goto done;
		units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
			(int)bytes, NULL, 0);
		if (!units) {
			code = GetLastError();
			goto done;
		}
		wide = hx_os_i_alloc(NULL, (size_t)units * sizeof(*wide), 0);
		if (!wide) {
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
			goto done;
		}
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
			    (int)bytes, wide, units)) {
			code = GetLastError();
			goto done;
		}
		while (position < (size_t)units) {
			DWORD written, part = (DWORD)((size_t)units - position);
			if (part > 16384)
				part = 16384;
			if (position + part < (size_t)units &&
				wide[position + part - 1] >= 0xd800 &&
				wide[position + part - 1] <= 0xdbff)
				--part;
			if (!WriteConsoleW(handle, wide + position, part,
				    &written, NULL) ||
				!written) {
				code = GetLastError();
				if (!code)
					code = ERROR_WRITE_FAULT;
				break;
			}
			position += written;
		}
	}
	else {
		while (position < bytes) {
			DWORD written;
			DWORD part = bytes - position > MAXDWORD
				? MAXDWORD
				: (DWORD)(bytes - position);
			if (!WriteFile(handle, text + position, part, &written,
				    NULL) ||
				!written) {
				code = GetLastError();
				if (!code)
					code = ERROR_WRITE_FAULT;
				break;
			}
			position += written;
		}
	}
done:
	hx_os_memory_free(wide);
	_unlock_file(output);
	return status != HX_OS_OK ? status : hx_os_i_win32(error, code);
}
