#include "internal.h"
#include "hx_os_registry.h"
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
#include <windows.h>
#include <wchar.h>
struct hx_os_registry {
	hx_os_context *context;
	HKEY native;
	unsigned flags;
};
static HKEY hx_os_registry_root(int root)
{
	switch (root) {
	case HX_OS_REGISTRY_CURRENT_USER:
		return HKEY_CURRENT_USER;
	case HX_OS_REGISTRY_LOCAL_MACHINE:
		return HKEY_LOCAL_MACHINE;
	case HX_OS_REGISTRY_CLASSES_ROOT:
		return HKEY_CLASSES_ROOT;
	case HX_OS_REGISTRY_USERS:
		return HKEY_USERS;
	case HX_OS_REGISTRY_CURRENT_CONFIG:
		return HKEY_CURRENT_CONFIG;
	default:
		return NULL;
	}
}
static wchar_t *hx_os_registry_text(
	hx_os_context *context, const char *text, hx_os_error *error)
{
	size_t bytes, invalid;
	hx_os_status status;
	if (!text)
		text = "";
	bytes = strlen(text);
	if (bytes > 1024u * 1024u) {
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		return NULL;
	}
	status = hx_os_text_validate_utf8(text, bytes, &invalid);
	if (status != HX_OS_OK) {
		(void)hx_os_i_error(error, status, 0, 0);
		return NULL;
	}
	return hx_os_i_windows_utf16(context, text, error);
}
#endif
hx_os_status hx_os_registry_open(hx_os_context *context, int root,
	const char *path, unsigned flags, hx_os_registry **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_os_registry *key;
	hx_os_error local;
	HKEY hive = hx_os_registry_root(root);
	wchar_t *wide;
	REGSAM access = 0;
	DWORD disposition = 0;
	LSTATUS code;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!hive || !path || !path[0] || (flags & ~63u) || !(flags & 3u) ||
		((flags & HX_OS_REGISTRY_CREATE) &&
			!(flags & HX_OS_REGISTRY_WRITE)) ||
		((flags & HX_OS_REGISTRY_EXCLUSIVE) &&
			!(flags & HX_OS_REGISTRY_CREATE)) ||
		((flags & HX_OS_REGISTRY_VIEW32) &&
			(flags & HX_OS_REGISTRY_VIEW64)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!error)
		error = &local;
	wide = hx_os_registry_text(context, path, error);
	if (!wide)
		return error->status;
	key = hx_os_i_zero(context, sizeof(*key));
	if (!key) {
		hx_os_memory_free(wide);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (flags & HX_OS_REGISTRY_READ)
		access |= KEY_QUERY_VALUE;
	if (flags & HX_OS_REGISTRY_WRITE)
		access |= KEY_SET_VALUE;
	if (flags & HX_OS_REGISTRY_VIEW32)
		access |= KEY_WOW64_32KEY;
	if (flags & HX_OS_REGISTRY_VIEW64)
		access |= KEY_WOW64_64KEY;
	if (flags & HX_OS_REGISTRY_CREATE)
		code = RegCreateKeyExW(hive, wide, 0, NULL,
			REG_OPTION_NON_VOLATILE, access, NULL, &key->native,
			&disposition);
	else
		code = RegOpenKeyExW(hive, wide, 0, access, &key->native);
	hx_os_memory_free(wide);
	if (code ||
		((flags & HX_OS_REGISTRY_EXCLUSIVE) &&
			disposition != REG_CREATED_NEW_KEY)) {
		if (!code)
			(void)RegCloseKey(key->native);
		hx_os_memory_free(key);
		return code ? hx_os_i_win32(error, (DWORD)code)
			    : hx_os_i_error(error, HX_OS_EXISTS, 0, 0);
	}
	key->context = context;
	key->flags = flags;
	*out = key;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)root;
	(void)path;
	(void)flags;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_open_user(hx_os_context *context, const char *path,
	unsigned flags, hx_os_registry **out, hx_os_error *error)
{
	return hx_os_registry_open(
		context, HX_OS_REGISTRY_CURRENT_USER, path, flags, out, error);
}
hx_os_status hx_os_registry_read_string(hx_os_registry *key, const char *name,
	hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_os_error local;
	wchar_t *wide_name, *buffer = NULL;
	DWORD type = 0, bytes = 0;
	LSTATUS code;
	hx_os_status status = HX_OS_OK;
	int attempt;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!(key->flags & HX_OS_REGISTRY_READ))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (!error)
		error = &local;
	wide_name = hx_os_registry_text(out->context, name, error);
	if (!wide_name)
		return error->status;
	for (attempt = 0; attempt < 8; ++attempt) {
		DWORD capacity;
		size_t characters;
		code = RegQueryValueExW(
			key->native, wide_name, NULL, &type, NULL, &bytes);
		if (code) {
			status = hx_os_i_win32(error, (DWORD)code);
			break;
		}
		if (type != REG_SZ && type != REG_EXPAND_SZ) {
			status =
				hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
			break;
		}
		if (bytes > 1024u * 1024u) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}
		capacity = bytes;
		buffer = hx_os_i_zero(
			out->context, (size_t)bytes + sizeof(*buffer));
		if (!buffer) {
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
			break;
		}
		code = RegQueryValueExW(key->native, wide_name, NULL, &type,
			(BYTE *)buffer, &bytes);
		if (code == ERROR_MORE_DATA) {
			hx_os_memory_free(buffer);
			buffer = NULL;
			status = hx_os_i_error(
				error, HX_OS_BUSY, HX_OS_ERROR_WIN32, code);
			continue;
		}
		if (code) {
			status = hx_os_i_win32(error, (DWORD)code);
			break;
		}
		if (type != REG_SZ && type != REG_EXPAND_SZ) {
			status =
				hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
			break;
		}
		if (bytes > capacity || bytes % sizeof(*buffer)) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			break;
		}
		characters = bytes / sizeof(*buffer);
		if (characters && !buffer[characters - 1])
			--characters;
		if (wmemchr(buffer, 0, characters)) {
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			break;
		}
		status = hx_string_assign_encoded(out, HX_OS_UTF16LE, buffer,
			characters * sizeof(*buffer), error);
		break;
	}
	hx_os_memory_free(buffer);
	hx_os_memory_free(wide_name);
	return status;
#else
	(void)key;
	(void)name;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_read_string_buffer(hx_os_registry *key,
	const char *name, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_string text;
	hx_os_status status;
	if (!key || !required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&text, key->context);
	status = hx_os_registry_read_string(key, name, &text, error);
	if (status == HX_OS_OK) {
		*required = text.size + 1;
		if (capacity < *required)
			status = hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
		else
			memcpy(buffer, hx_string_c_str(&text), *required);
	}
	hx_string_destroy(&text);
	return status;
#else
	(void)key;
	(void)name;
	(void)buffer;
	(void)capacity;
	(void)required;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_read_u32(hx_os_registry *key, const char *name,
	uint32_t *out, hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_os_error local;
	wchar_t *wide;
	DWORD type = 0, bytes = sizeof(DWORD), value = 0;
	LSTATUS code;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!(key->flags & HX_OS_REGISTRY_READ))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (!error)
		error = &local;
	wide = hx_os_registry_text(key->context, name, error);
	if (!wide)
		return error->status;
	code = RegQueryValueExW(
		key->native, wide, NULL, &type, (BYTE *)&value, &bytes);
	hx_os_memory_free(wide);
	if (code && code != ERROR_MORE_DATA)
		return hx_os_i_win32(error, (DWORD)code);
	if (type != REG_DWORD)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (code == ERROR_MORE_DATA || bytes != sizeof(value))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	*out = (uint32_t)value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)key;
	(void)name;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_write_string(hx_os_registry *key, const char *name,
	const char *value, hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_os_error local;
	wchar_t *wide_name, *wide_value;
	size_t native_bytes;
	LSTATUS code;
	if (!key || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!(key->flags & HX_OS_REGISTRY_WRITE))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (!error)
		error = &local;
	wide_name = hx_os_registry_text(key->context, name, error);
	if (!wide_name)
		return error->status;
	wide_value = hx_os_registry_text(key->context, value, error);
	if (!wide_value) {
		hx_os_memory_free(wide_name);
		return error->status;
	}
	native_bytes = (wcslen(wide_value) + 1) * sizeof(*wide_value);
	if (native_bytes > 1024u * 1024u) {
		hx_os_memory_free(wide_name);
		hx_os_memory_free(wide_value);
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	code = RegSetValueExW(key->native, wide_name, 0, REG_SZ,
		(const BYTE *)wide_value, (DWORD)native_bytes);
	hx_os_memory_free(wide_name);
	hx_os_memory_free(wide_value);
	return hx_os_i_win32(error, (DWORD)code);
#else
	(void)key;
	(void)name;
	(void)value;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_write_u32(hx_os_registry *key, const char *name,
	uint32_t value, hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	hx_os_error local;
	wchar_t *wide;
	DWORD native = value;
	LSTATUS code;
	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!(key->flags & HX_OS_REGISTRY_WRITE))
		return hx_os_i_error(error, HX_OS_ACCESS_DENIED, 0, 0);
	if (!error)
		error = &local;
	wide = hx_os_registry_text(key->context, name, error);
	if (!wide)
		return error->status;
	code = RegSetValueExW(key->native, wide, 0, REG_DWORD,
		(const BYTE *)&native, sizeof(native));
	hx_os_memory_free(wide);
	return hx_os_i_win32(error, (DWORD)code);
#else
	(void)key;
	(void)name;
	(void)value;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_registry_close(hx_os_registry **key, hx_os_error *error)
{
#if HX_OS_ENABLE_REGISTRY && defined(_WIN32)
	LSTATUS code;
	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*key)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	code = RegCloseKey((*key)->native);
	hx_os_memory_free(*key);
	*key = NULL;
	return hx_os_i_win32(error, (DWORD)code);
#else
	(void)key;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
