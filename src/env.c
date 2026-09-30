#include "internal.h"
#include "hx_os_env.h"

#if HX_OS_ENABLE_ENV
#if defined(_WIN32)
#include <windows.h>
static SRWLOCK hx_os_env_mutex = SRWLOCK_INIT;
#else
#include <pthread.h>
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

hx_os_status hx_os_env_get(const char *name, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error)
{
#if HX_OS_ENABLE_ENV
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

hx_os_status hx_os_env_get_string(
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
		status = hx_os_env_get(
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
