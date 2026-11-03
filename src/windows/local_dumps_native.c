#include "local_dumps_private.h"

#include <windows.h>
#include <wchar.h>

enum {
	HX_OS_LD_NAME_UNITS = 255,
	HX_OS_LD_PATH_UNITS = 512,
	HX_OS_LD_TEXT_UNITS = 32768
};

static const wchar_t hx_os_ld_prefix[] =
	L"Software\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\";
static const wchar_t hx_os_ld_marker[] = L"HxLocalDumpsVersion";
static const char hx_os_ld_default_folder[] = "%LOCALAPPDATA%\\CrashDumps";

static REGSAM hx_os_ld_view(unsigned view)
{
	return view == HX_OS_LOCAL_DUMPS_VIEW32	   ? KEY_WOW64_32KEY
		: view == HX_OS_LOCAL_DUMPS_VIEW64 ? KEY_WOW64_64KEY
						   : 0;
}

static wchar_t *hx_os_ld_text(
	const char *text, size_t maximum, hx_os_error *error)
{
	size_t bytes = strlen(text), invalid;
	hx_os_status status;

	if (bytes > maximum) {
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}

	status = hx_os_text_validate_utf8(text, bytes, &invalid);
	if (status != HX_OS_OK) {
		(void)hx_os_i_error(error, status, 0, 0);

		return NULL;
	}

	return hx_os_i_windows_utf16(NULL, text, error);
}

static hx_os_status hx_os_ld_path(const char *application,
	wchar_t path[HX_OS_LD_PATH_UNITS], hx_os_error *error)
{
	wchar_t *storage;
	const wchar_t *name;
	size_t length, i;
	size_t prefix = sizeof(hx_os_ld_prefix) / sizeof(wchar_t) - 1;

	if (application) {
		storage = hx_os_ld_text(
			application, HX_OS_LD_NAME_UNITS * 4u, error);
		if (!storage)
			return error->status;

		name = storage;
	}
	else {
		DWORD count, code;

		storage = hx_os_i_alloc(
			NULL, HX_OS_LD_TEXT_UNITS * sizeof(wchar_t), 0);
		if (!storage)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		count = GetModuleFileNameW(NULL, storage, HX_OS_LD_TEXT_UNITS);
		code = count ? ERROR_SUCCESS : GetLastError();
		if (!count || count >= HX_OS_LD_TEXT_UNITS) {
			hx_os_memory_free(storage);

			return count ? hx_os_i_error(error, HX_OS_LIMIT, 0, 0)
				     : hx_os_i_win32(error, code);
		}

		name = wcsrchr(storage, L'\\');
		name = name ? name + 1 : storage;
	}

	length = wcslen(name);
	if (length <= 4 || length > HX_OS_LD_NAME_UNITS || name[0] == L' ' ||
		CompareStringOrdinal(name + length - 4, 4, L".exe", 4, TRUE) !=
			CSTR_EQUAL) {
		hx_os_memory_free(storage);

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	for (i = 0; i < length; ++i) {
		if (name[i] < 32 || wcschr(L"\\/:*?\"<>|", name[i])) {
			hx_os_memory_free(storage);

			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		}
	}

	memcpy(path, hx_os_ld_prefix, prefix * sizeof(*path));
	memcpy(path + prefix, name, (length + 1) * sizeof(*path));
	hx_os_memory_free(storage);

	return HX_OS_OK;
}

static int hx_os_ld_separator(wchar_t c)
{
	return c == L'\\' || c == L'/';
}

static hx_os_status hx_os_ld_folder(
	const char *text, wchar_t **out, hx_os_error *error)
{
	wchar_t *wide;
	size_t length;
	int absolute = 0;

	wide = hx_os_ld_text(text, HX_OS_LD_TEXT_UNITS * 4u, error);
	if (!wide)
		return error->status;

	length = wcslen(wide);
	if (length >= HX_OS_LD_TEXT_UNITS) {
		hx_os_memory_free(wide);

		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	if (length >= 3 &&
		((wide[0] >= L'A' && wide[0] <= L'Z') ||
			(wide[0] >= L'a' && wide[0] <= L'z')) &&
		wide[1] == L':' && hx_os_ld_separator(wide[2]))
		absolute = 1;
	else if (length > 2 && hx_os_ld_separator(wide[0]) &&
		hx_os_ld_separator(wide[1]))
		absolute = 1;
	else if (length > 2 && wide[0] == L'%') {
		const wchar_t *end = wcschr(wide + 1, L'%');

		absolute = end && end > wide + 1 &&
			(!end[1] || hx_os_ld_separator(end[1]));
	}

	if (!absolute) {
		hx_os_memory_free(wide);

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	*out = wide;

	return HX_OS_OK;
}

static hx_os_status hx_os_ld_number(HKEY key, const wchar_t *name,
	uint32_t field, uint32_t *out, hx_os_local_dumps_info *info,
	hx_os_error *error)
{
	DWORD type = 0, value = 0, bytes = sizeof(value);
	LSTATUS code = RegQueryValueExW(
		key, name, NULL, &type, (BYTE *)&value, &bytes);

	if (code == ERROR_FILE_NOT_FOUND)
		return HX_OS_OK;
	if (code && code != ERROR_MORE_DATA)
		return hx_os_i_win32(error, (DWORD)code);
	if (type != REG_DWORD)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (code || bytes != sizeof(value))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	*out = value;
	info->fields |= field;

	return HX_OS_OK;
}

static hx_os_status hx_os_ld_read_folder(HKEY key, hx_string *out,
	hx_os_local_dumps_info *info, int *canonical, hx_os_error *error)
{
	unsigned attempt;

	*canonical = 0;
	for (attempt = 0; attempt < 8; ++attempt) {
		DWORD type = 0, bytes = 0, capacity;
		wchar_t *wide;
		size_t units;
		hx_os_status status;
		LSTATUS code = RegQueryValueExW(
			key, L"DumpFolder", NULL, &type, NULL, &bytes);

		if (code == ERROR_FILE_NOT_FOUND)
			return HX_OS_OK;
		if (code)
			return hx_os_i_win32(error, (DWORD)code);
		if (type != REG_SZ && type != REG_EXPAND_SZ)
			return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
		if (bytes > HX_OS_LD_TEXT_UNITS * sizeof(wchar_t))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		capacity = bytes;
		wide = hx_os_i_zero(
			out->context, (size_t)bytes + sizeof(*wide));
		if (!wide)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		code = RegQueryValueExW(
			key, L"DumpFolder", NULL, &type, (BYTE *)wide, &bytes);
		if (code == ERROR_MORE_DATA) {
			hx_os_memory_free(wide);
			continue;
		}

		if (code)
			status = hx_os_i_win32(error, (DWORD)code);
		else if (type != REG_SZ && type != REG_EXPAND_SZ)
			status =
				hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
		else if (bytes > capacity || bytes % sizeof(*wide))
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		else {
			units = bytes / sizeof(*wide);
			if (units && !wide[units - 1]) {
				--units;
				*canonical = units && type == REG_EXPAND_SZ;
			}

			if (wmemchr(wide, 0, units))
				status = hx_os_i_error(
					error, HX_OS_SYNTAX, 0, 0);
			else
				status = hx_string_assign_encoded(out,
					HX_OS_UTF16LE, wide,
					units * sizeof(*wide), error);
		}

		hx_os_memory_free(wide);
		if (status == HX_OS_OK)
			info->fields |= HX_OS_LOCAL_DUMPS_HAS_FOLDER;

		return status;
	}

	return hx_os_i_error(
		error, HX_OS_BUSY, HX_OS_ERROR_WIN32, ERROR_MORE_DATA);
}

static hx_os_status hx_os_ld_read(HKEY key, hx_os_local_dumps_info *info,
	hx_string *folder, hx_os_error *error)
{
	DWORD type = 0, marker = 0, bytes = sizeof(marker);
	DWORD values = 0, children = 0;
	int canonical = 0, owned;
	LSTATUS code;
	hx_os_status status;

	memset(info, 0, sizeof(*info));
	info->size = sizeof(*info);
	info->version = 1;
	status = hx_os_ld_number(key, L"DumpType", HX_OS_LOCAL_DUMPS_HAS_TYPE,
		&info->dump_type, info, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_ld_number(key, L"DumpCount", HX_OS_LOCAL_DUMPS_HAS_COUNT,
		&info->dump_count, info, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_ld_read_folder(key, folder, info, &canonical, error);
	if (status != HX_OS_OK)
		return status;

	code = RegQueryValueExW(
		key, hx_os_ld_marker, NULL, &type, (BYTE *)&marker, &bytes);
	if (code && code != ERROR_FILE_NOT_FOUND && code != ERROR_MORE_DATA)
		return hx_os_i_win32(error, (DWORD)code);

	owned = !code && type == REG_DWORD && bytes == sizeof(marker) &&
		marker == 1;
	code = RegQueryInfoKeyW(key, NULL, NULL, NULL, &children, NULL, NULL,
		&values, NULL, NULL, NULL, NULL);
	if (code)
		return hx_os_i_win32(error, (DWORD)code);

	info->managed = owned && !children && values == 4 &&
		info->fields ==
			(HX_OS_LOCAL_DUMPS_HAS_TYPE |
				HX_OS_LOCAL_DUMPS_HAS_COUNT |
				HX_OS_LOCAL_DUMPS_HAS_FOLDER) &&
		canonical && info->dump_count &&
		(info->dump_type == HX_OS_LOCAL_DUMPS_MINI ||
			info->dump_type == HX_OS_LOCAL_DUMPS_FULL);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_ld_matches(HKEY key,
	const hx_os_local_dumps_options *options, const char *expected_folder,
	int *matches, hx_os_error *error)
{
	hx_os_local_dumps_info info;
	hx_string folder = HX_STRING_INIT;
	hx_os_status status = hx_os_ld_read(key, &info, &folder, error);

	*matches = status == HX_OS_OK && info.managed &&
		info.dump_type == options->dump_type &&
		info.dump_count == options->dump_count &&
		!strcmp(hx_string_c_str(&folder), expected_folder);
	hx_string_destroy(&folder);

	// Malformed preexisting data is a conflict, never permission to
	// replace.
	if (status == HX_OS_TYPE_MISMATCH || status == HX_OS_SYNTAX ||
		status == HX_OS_INVALID_ENCODING ||
		status == HX_OS_TRUNCATED_INPUT || status == HX_OS_LIMIT)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	return status;
}

static hx_os_status hx_os_ld_close(
	HKEY key, hx_os_status status, hx_os_error *error)
{
	LSTATUS code = RegCloseKey(key);

	if (status == HX_OS_OK)
		return hx_os_i_win32(error, (DWORD)code);
	if (code && !error->cause_code)
		error->cause_code = code;

	return status;
}

static LSTATUS hx_os_ld_rollback(HKEY key, const wchar_t *path, REGSAM view)
{
	DWORD index;

	// Other writers must be excluded. Still refuse unexpected
	// values/children; never use a recursive registry deletion for
	// installation rollback.
	for (index = 0; index < 5; ++index) {
		wchar_t name[32];
		DWORD units = sizeof(name) / sizeof(*name);
		LSTATUS code = RegEnumValueW(
			key, index, name, &units, NULL, NULL, NULL, NULL);

		if (code == ERROR_NO_MORE_ITEMS)
			return RegDeleteKeyExW(
				HKEY_LOCAL_MACHINE, path, view, 0);
		if (code)
			return code;
		if (wcscmp(name, hx_os_ld_marker) &&
			wcscmp(name, L"DumpFolder") &&
			wcscmp(name, L"DumpCount") && wcscmp(name, L"DumpType"))
			return ERROR_BUSY;
	}

	return ERROR_BUSY;
}

hx_os_status hx_os_i_local_dumps_change(const char *application,
	const hx_os_local_dumps_options *options, int remove,
	hx_os_error *error)
{
	wchar_t path[HX_OS_LD_PATH_UNITS];
	wchar_t *folder = NULL;
	const char *text = options->dump_folder ? options->dump_folder
						: hx_os_ld_default_folder;
	HKEY key;
	DWORD disposition = 0, marker = 1;
	REGSAM view = hx_os_ld_view(options->registry_view);
	LSTATUS code;
	hx_os_error local;
	hx_os_status status;
	int matches = 0;

	if (!error)
		error = &local;

	status = hx_os_ld_path(application, path, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_ld_folder(text, &folder, error);
	if (status != HX_OS_OK)
		return status;

	if (remove)
		code = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0,
			KEY_QUERY_VALUE | DELETE | view, &key);
	else
		code = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL,
			REG_OPTION_NON_VOLATILE,
			KEY_QUERY_VALUE | KEY_SET_VALUE | view, NULL, &key,
			&disposition);

	if (code) {
		hx_os_memory_free(folder);
		if (remove &&
			(code == ERROR_FILE_NOT_FOUND ||
				code == ERROR_PATH_NOT_FOUND))
			return hx_os_i_error(error, HX_OS_OK, 0, 0);

		return hx_os_i_win32(error, (DWORD)code);
	}

	if (remove || disposition != REG_CREATED_NEW_KEY) {
		status = hx_os_ld_matches(key, options, text, &matches, error);
		if (status == HX_OS_OK && !matches)
			status = hx_os_i_error(error,
				remove ? HX_OS_BUSY : HX_OS_EXISTS, 0, 0);
		if (status == HX_OS_OK && remove) {
			code = RegDeleteKeyExW(
				HKEY_LOCAL_MACHINE, path, view, 0);
			status = hx_os_i_win32(error, (DWORD)code);
		}
	}
	else {
		DWORD count = options->dump_count, type = options->dump_type;
		DWORD bytes = (DWORD)((wcslen(folder) + 1) * sizeof(*folder));

		code = RegSetValueExW(key, hx_os_ld_marker, 0, REG_DWORD,
			(const BYTE *)&marker, sizeof(marker));
		if (!code)
			code = RegSetValueExW(key, L"DumpFolder", 0,
				REG_EXPAND_SZ, (const BYTE *)folder, bytes);
		if (!code)
			code = RegSetValueExW(key, L"DumpCount", 0, REG_DWORD,
				(const BYTE *)&count, sizeof(count));
		if (!code)
			code = RegSetValueExW(key, L"DumpType", 0, REG_DWORD,
				(const BYTE *)&type, sizeof(type));

		status = hx_os_i_win32(error, (DWORD)code);
		if (code)
			error->cause_code = hx_os_ld_rollback(key, path, view);
	}

	hx_os_memory_free(folder);

	return hx_os_ld_close(key, status, error);
}

hx_os_status hx_os_i_local_dumps_query(const char *application,
	unsigned registry_view, hx_os_local_dumps_info *out, hx_string *folder,
	hx_os_error *error)
{
	wchar_t path[HX_OS_LD_PATH_UNITS];
	HKEY key;
	LSTATUS code;
	hx_os_local_dumps_info info;
	hx_string temporary;
	hx_os_error local;
	hx_os_status status;

	if (!error)
		error = &local;

	status = hx_os_ld_path(application, path, error);
	if (status != HX_OS_OK)
		return status;

	code = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0,
		KEY_QUERY_VALUE | hx_os_ld_view(registry_view), &key);
	if (code)
		return hx_os_i_win32(error, (DWORD)code);

	hx_string_init(&temporary, folder->context);
	status = hx_os_ld_read(key, &info, &temporary, error);
	status = hx_os_ld_close(key, status, error);
	if (status == HX_OS_OK) {
		hx_string_move(folder, &temporary);
		*out = info;
	}

	hx_string_destroy(&temporary);

	return status;
}
