#include "paths_private.h"

#include <windows.h>
#include <shlobj.h>

static const char hx_os_paths_module_anchor = 0;

static hx_os_status hx_os_path_known(
	const KNOWNFOLDERID *id, hx_string *out, hx_os_error *error)
{
	PWSTR wide = NULL;
	HRESULT result =
		SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, NULL, &wide);
	hx_os_status status;

	if (FAILED(result)) {
		CoTaskMemFree(wide);
		if (HRESULT_FACILITY(result) == FACILITY_WIN32)
			return hx_os_i_win32(error, HRESULT_CODE(result));

		return hx_os_i_error(
			error, HX_OS_IO_ERROR, HX_OS_ERROR_HRESULT, result);
	}

	status = hx_os_i_windows_path_text(out, wide, wcslen(wide), error);
	CoTaskMemFree(wide);

	return status;
}

static hx_os_status hx_os_path_native_name(
	int kind, hx_string *out, hx_os_error *error)
{
	HMODULE module = NULL;
	wchar_t *wide;
	DWORD capacity, length, code;
	hx_os_status status = HX_OS_RANGE;

	if (kind == HX_OS_I_PATH_MODULE &&
		!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
				GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&hx_os_paths_module_anchor, &module))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
			HX_OS_ERROR_WIN32, GetLastError());
	for (capacity = 256; capacity <= 32768; capacity *= 2) {
		wide = hx_os_i_alloc(
			out->context, (size_t)capacity * sizeof(*wide), 0);
		if (!wide)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		length = kind == HX_OS_I_PATH_TEMP
			? GetTempPathW(capacity, wide)
			: GetModuleFileNameW(module, wide, capacity);
		code = length ? 0 : GetLastError();
		if (length && length < capacity) {
			if (kind == HX_OS_I_PATH_MODULE) {
				while (length && wide[length - 1] != L'\\' &&
					wide[length - 1] != L'/')
					--length;
				wide[length] = 0;
			}

			status = hx_os_i_windows_path_text(
				out, wide, length, error);

			hx_os_memory_free(wide);

			return status;
		}

		hx_os_memory_free(wide);
		if (code)
			return hx_os_i_win32(error, code);
	}

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_i_special_path(int kind, hx_string *out, hx_os_error *error)
{
	switch (kind) {
	case HX_OS_I_PATH_HOME: {
		hx_os_status status =
			hx_os_i_env_get_string("USERPROFILE", out, error);

		if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
			return status;

		if (status == HX_OS_OK && out->size > 2 &&
			((out->data[1] == ':' &&
				 (out->data[2] == '\\' ||
					 out->data[2] == '/')) ||
				(out->data[0] == '\\' && out->data[1] == '\\')))
			return status;
	}

		return hx_os_path_known(&FOLDERID_Profile, out, error);
	case HX_OS_I_PATH_CONFIG_HOME:
		return hx_os_path_known(&FOLDERID_RoamingAppData, out, error);
	case HX_OS_I_PATH_DATA_HOME:
		return hx_os_path_known(&FOLDERID_LocalAppData, out, error);
	case HX_OS_I_PATH_PROGRAM_DATA:
		return hx_os_path_known(&FOLDERID_ProgramData, out, error);
	default:
		return hx_os_path_native_name(kind, out, error);
	}
}
