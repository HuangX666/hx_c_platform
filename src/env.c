#include "internal.h"
#include "hx_os_env.h"
#include "system_private.h"

#if HX_OS_ENABLE_ENV || HX_OS_ENABLE_PATHS || HX_OS_ENABLE_PROCESS
#if defined(_WIN32)
#include <windows.h>

static SRWLOCK hx_os_env_mutex = SRWLOCK_INIT;
#else
#include <pthread.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char **environ;
#endif
static pthread_mutex_t hx_os_env_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static hx_os_status hx_os_env_name(const char *name, hx_os_error *error)
{
	if (!name || !name[0] || strchr(name, '='))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_path_validate(name, error);
}

static hx_os_status hx_os_env_lock(hx_os_error *error)
{
#if defined(_WIN32)
	AcquireSRWLockExclusive(&hx_os_env_mutex);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	return hx_os_i_errno(error, pthread_mutex_lock(&hx_os_env_mutex));
#endif
}

static hx_os_status hx_os_env_unlock(hx_os_status status, hx_os_error *error)
{
#if defined(_WIN32)
	ReleaseSRWLockExclusive(&hx_os_env_mutex);
	(void)error;
#else
	int code = pthread_mutex_unlock(&hx_os_env_mutex);
	if (status == HX_OS_OK && code)
		return hx_os_i_errno(error, code);
#endif
	return status;
}

#if defined(_WIN32)
static hx_os_status hx_os_env_read_wide(const wchar_t *name, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	wchar_t *value;
	DWORD needed, length, code;
	int bytes;

	SetLastError(ERROR_SUCCESS);
	needed = GetEnvironmentVariableW(name, NULL, 0);
	if (!needed) {
		code = GetLastError();
		if (code == ERROR_ENVVAR_NOT_FOUND)
			return hx_os_i_error(error, HX_OS_NOT_FOUND,
				HX_OS_ERROR_WIN32, code);

		if (code)
			return hx_os_i_win32(error, code);

		return hx_os_i_text_copy("", buffer, capacity, required, error);
	}

	if (needed > INT_MAX || needed > SIZE_MAX / sizeof(*value))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	/* Native scratch allocation cannot invoke the injected output allocator
	 * while the environment lock is held. */
	value = HeapAlloc(GetProcessHeap(), 0, (size_t)needed * sizeof(*value));
	if (!value)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	SetLastError(ERROR_SUCCESS);
	length = GetEnvironmentVariableW(name, value, needed);
	code = !length ? GetLastError() : 0;
	if (length >= needed)
		code = ERROR_INSUFFICIENT_BUFFER;

	if (code) {
		(void)HeapFree(GetProcessHeap(), 0, value);

		return hx_os_i_win32(error, code);
	}

	bytes = length ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
				 value, (int)length, NULL, 0, NULL, NULL)
		       : 0;
	if (length && !bytes) {
		code = GetLastError();
		(void)HeapFree(GetProcessHeap(), 0, value);

		return hx_os_i_error(
			error, HX_OS_INVALID_ENCODING, HX_OS_ERROR_WIN32, code);
	}

	*required = (size_t)bytes + 1;
	if (capacity < *required) {
		(void)HeapFree(GetProcessHeap(), 0, value);
		if (capacity)
			buffer[0] = 0;

		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	}

	if (bytes &&
		!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
			(int)length, buffer, bytes, NULL, NULL))
		code = GetLastError();
	(void)HeapFree(GetProcessHeap(), 0, value);
	if (code)
		return hx_os_i_win32(error, code);

	buffer[bytes] = 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif
#endif

hx_os_status hx_os_i_env_snapshot(
	char **block, size_t *bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV || HX_OS_ENABLE_PATHS || HX_OS_ENABLE_PROCESS
	hx_os_status status;
	size_t total = 0;
	char *copy = NULL;

	*block = NULL;
	*bytes = 0;
#if defined(_WIN32)
	{
		wchar_t *wide, *cursor;
		size_t offset = 0, count = 0;
		DWORD code;

		status = hx_os_env_lock(error);
		if (status != HX_OS_OK)
			return status;

		wide = GetEnvironmentStringsW();
		code = wide ? 0 : GetLastError();

		status = hx_os_env_unlock(hx_os_i_win32(error, code), error);
		if (status != HX_OS_OK) {
			if (wide)
				FreeEnvironmentStringsW(wide);

			return status;
		}

		status = HX_OS_OK;

		for (cursor = wide; *cursor; cursor += wcslen(cursor) + 1) {
			int needed = WideCharToMultiByte(CP_UTF8,
				WC_ERR_INVALID_CHARS, cursor, -1, NULL, 0, NULL,
				NULL);
			if (!needed) {
				status = hx_os_i_error(error,
					HX_OS_INVALID_ENCODING,
					HX_OS_ERROR_WIN32, GetLastError());
				break;
			}

			if (++count > HX_OS_I_SYSTEM_ITEMS ||
				(size_t)needed > HX_OS_I_SYSTEM_BYTES - total) {
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
				break;
			}

			total += (size_t)needed;
		}

		if (status == HX_OS_OK) {
			copy = malloc(total ? total : 1);
			if (!copy)
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
		}

		for (cursor = wide; status == HX_OS_OK && *cursor;
			cursor += wcslen(cursor) + 1) {
			int written = WideCharToMultiByte(CP_UTF8,
				WC_ERR_INVALID_CHARS, cursor, -1, copy + offset,
				(int)(total - offset), NULL, NULL);
			if (!written)
				status = hx_os_i_error(error,
					HX_OS_INVALID_ENCODING,
					HX_OS_ERROR_WIN32, GetLastError());
			else
				offset += (size_t)written;
		}

		FreeEnvironmentStringsW(wide);
	}
#else
	status = hx_os_env_lock(error);
	if (status == HX_OS_OK) {
#if defined(__APPLE__)
		char **values = *_NSGetEnviron();
#else
		char **values = environ;
#endif
		size_t i, count = 0, offset = 0;

		for (; values && values[count]; ++count) {
			size_t length = strlen(values[count]) + 1;
			if (count == HX_OS_I_SYSTEM_ITEMS ||
				length > HX_OS_I_SYSTEM_BYTES - total) {
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
				break;
			}

			total += length;
		}

		if (status == HX_OS_OK) {
			/* No injected allocator callback while holding the
			 * lock. */
			copy = malloc(total ? total : 1);
			if (!copy)
				status = hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
			else
				for (i = 0; i < count; ++i) {
					size_t length = strlen(values[i]) + 1;
					memcpy(copy + offset, values[i],
						length);
					offset += length;
				}
		}

		status = hx_os_env_unlock(status, error);
	}
#endif
	if (status != HX_OS_OK) {
		free(copy);

		return status;
	}

	*block = copy;
	*bytes = total;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)block;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_env_enumerate(hx_os_context *context,
	hx_os_env_visit_fn function, void *user, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
	char *block = NULL;
	size_t bytes = 0, i;
	hx_os_string_list *snapshot = NULL;
	hx_os_status status;

	if (!function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_env_snapshot(&block, &bytes, error);
	if (status == HX_OS_OK)
		status = hx_os_i_strings_from_block(
			context, block, bytes, 1, &snapshot, error);

	free(block);
	if (status != HX_OS_OK)
		return status;

	for (i = 0; i < snapshot->count; ++i) {
		const char *entry = snapshot->items[i];
#if defined(_WIN32)
		if (entry[0] == '=')
			continue;
#endif
		if (!entry[0] || !strchr(entry, '=') || entry[0] == '=') {
			hx_os_string_list_destroy(&snapshot);

			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		}
	}

	for (i = 0; i < snapshot->count; ++i) {
		char *entry = (char *)snapshot->items[i];
		char *separator;
#if defined(_WIN32)
		if (entry[0] == '=')
			continue;
#endif
		separator = strchr(entry, '=');
		*separator = '\0';

		status = function(entry, separator + 1, user);
		if (status != HX_OS_OK)
			break;
	}

	hx_os_string_list_destroy(&snapshot);

	return hx_os_i_error(error, status, 0, 0);
#else
	(void)context;
	(void)function;
	(void)user;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_env_get(const char *name, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV || HX_OS_ENABLE_PATHS
	hx_os_status status = hx_os_env_name(name, error);
	if (status != HX_OS_OK)
		return status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if defined(_WIN32)
	{
		hx_os_error local;
		wchar_t *wide;

		if (!error)
			error = &local;

		wide = hx_os_i_windows_utf16(NULL, name, error);
		if (!wide)
			return error->status;

		status = hx_os_env_lock(error);
		if (status == HX_OS_OK)
			status = hx_os_env_unlock(
				hx_os_env_read_wide(wide, buffer, capacity,
					required, error),
				error);

		hx_os_memory_free(wide);
	}
#else
	status = hx_os_env_lock(error);
	if (status == HX_OS_OK) {
		const char *value = getenv(name);
		if (!value)
			status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		else {
			size_t invalid;

			status = hx_os_text_validate_utf8(
				value, strlen(value), &invalid);

			status = status == HX_OS_OK
				? hx_os_i_text_copy(value, buffer, capacity,
					  required, error)
				: hx_os_i_error(error, status, 0, 0);
		}

		status = hx_os_env_unlock(status, error);
	}
#endif
	return status;
#else
	(void)name;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_env_get_string(
	const char *name, hx_string *out, hx_os_error *error)
{
	hx_string value;
	hx_os_status status = HX_OS_BUSY;
	size_t required;
	unsigned attempt;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_init(&value, out->context);

	for (attempt = 0; attempt < 8; ++attempt) {
		status = hx_os_i_env_get(
			name, value.data, value.capacity, &required, error);
		if (status != HX_OS_OUTPUT_FULL)
			break;

		status = hx_string_reserve(&value, required - 1, error);
		if (status != HX_OS_OK)
			break;
	}

	if (attempt == 8)
		status = hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (status == HX_OS_OK) {
		value.size = required - 1;
		hx_string_move(out, &value);
	}

	hx_string_destroy(&value);

	return status;
}

hx_os_status hx_os_env_get(const char *name, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
	return hx_os_i_env_get(name, buffer, capacity, required, error);
#else
	(void)name;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_env_get_string(
	const char *name, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
	return hx_os_i_env_get_string(name, out, error);
#else
	(void)name;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_env_set(
	const char *name, const char *value, int replace, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
	hx_os_status status = hx_os_env_name(name, error);

	size_t invalid;

	if (status != HX_OS_OK)
		return status;

	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_text_validate_utf8(value, strlen(value), &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
#if defined(_WIN32)
	{
		hx_os_error local;
		wchar_t *wide_name, *wide_value;
		DWORD code = 0;

		if (!error)
			error = &local;

		wide_name = hx_os_i_windows_utf16(NULL, name, error);
		if (!wide_name)
			return error->status;

		wide_value = hx_os_i_windows_utf16(NULL, value, error);
		if (!wide_value) {
			hx_os_memory_free(wide_name);

			return error->status;
		}

		status = hx_os_env_lock(error);
		if (status == HX_OS_OK) {
			int exists;

			SetLastError(ERROR_SUCCESS);
			exists = GetEnvironmentVariableW(wide_name, NULL, 0) !=
					0 ||
				GetLastError() == ERROR_SUCCESS;
			if ((!exists || replace) &&
				!SetEnvironmentVariableW(wide_name, wide_value))
				code = GetLastError();
			status = hx_os_env_unlock(
				hx_os_i_win32(error, code), error);
		}

		hx_os_memory_free(wide_value);
		hx_os_memory_free(wide_name);
	}
#else
	status = hx_os_env_lock(error);
	if (status == HX_OS_OK)
		status = hx_os_env_unlock(
			hx_os_i_errno(error,
				setenv(name, value, replace != 0) ? errno : 0),
			error);
#endif
	return status;
#else
	(void)name;
	(void)value;
	(void)replace;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_env_remove(const char *name, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
	hx_os_status status = hx_os_env_name(name, error);
	if (status != HX_OS_OK)
		return status;
#if defined(_WIN32)
	{
		hx_os_error local;
		wchar_t *wide;
		DWORD code;

		if (!error)
			error = &local;

		wide = hx_os_i_windows_utf16(NULL, name, error);
		if (!wide)
			return error->status;

		status = hx_os_env_lock(error);
		if (status == HX_OS_OK) {
			code = SetEnvironmentVariableW(wide, NULL)
				? 0
				: GetLastError();
			if (code == ERROR_ENVVAR_NOT_FOUND)
				code = 0;

			status = hx_os_env_unlock(
				hx_os_i_win32(error, code), error);
		}

		hx_os_memory_free(wide);
	}
#else
	status = hx_os_env_lock(error);
	if (status == HX_OS_OK)
		status = hx_os_env_unlock(
			hx_os_i_errno(error, unsetenv(name) ? errno : 0),
			error);
#endif
	return status;
#else
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
