#include "windows_util_private.h"

#include <stdio.h>
#include <winver.h>

typedef struct hx_os_version_resource {
	void *data;
	size_t bytes;
} hx_os_version_resource;

static hx_os_status hx_os_version_error(hx_os_error *error, DWORD code)
{
	if (code >= ERROR_RESOURCE_DATA_NOT_FOUND &&
		code <= ERROR_RESOURCE_LANG_NOT_FOUND)
		return hx_os_i_error(
			error, HX_OS_NOT_FOUND, HX_OS_ERROR_WIN32, code);

	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
}

static hx_os_status hx_os_version_load(hx_os_context *context, const char *path,
	hx_os_version_resource *resource, hx_os_error *error)
{
	wchar_t *wide = hx_os_i_windows_path(context, path, error);
	DWORD bytes, ignored = 0, code;
	BOOL loaded;

	if (!wide)
		return error->status;

	SetLastError(0);
	bytes = GetFileVersionInfoSizeW(wide, &ignored);
	code = GetLastError();
	if (!bytes || bytes > 16u * 1024u * 1024u) {
		hx_os_memory_free(wide);

		return bytes ? hx_os_i_error(error, HX_OS_LIMIT, 0, 0)
			     : hx_os_version_error(error, code);
	}

	resource->data = hx_os_i_alloc(context, bytes, 0);
	resource->bytes = bytes;
	if (!resource->data) {
		hx_os_memory_free(wide);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	SetLastError(0);
	loaded = GetFileVersionInfoW(wide, 0, bytes, resource->data);
	code = GetLastError();
	hx_os_memory_free(wide);
	if (!loaded) {
		hx_os_memory_free(resource->data);
		resource->data = NULL;

		return hx_os_version_error(error, code);
	}

	return HX_OS_OK;
}

static int hx_os_version_contains(const hx_os_version_resource *resource,
	const void *pointer, size_t bytes)
{
	uintptr_t begin = (uintptr_t)resource->data;
	uintptr_t position = (uintptr_t)pointer;

	return position >= begin && position - begin <= resource->bytes &&
		bytes <= resource->bytes - (position - begin);
}

hx_os_status hx_os_i_windows_version_query(
	const char *path, hx_os_windows_version_info *out, hx_os_error *error)
{
	hx_os_error local;
	hx_os_version_resource resource = {NULL, 0};
	hx_os_windows_version_info value = {0};
	VS_FIXEDFILEINFO native;
	void *data = NULL;
	UINT bytes = 0;
	hx_os_status status;

	if (!error)
		error = &local;

	status = hx_os_version_load(NULL, path, &resource, error);
	if (status != HX_OS_OK)
		return status;

	if (!VerQueryValueW(resource.data, L"\\", &data, &bytes))
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	else if (bytes < sizeof(native) ||
		!hx_os_version_contains(&resource, data, bytes))
		status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	else {
		memcpy(&native, data, sizeof(native));
		if (native.dwSignature != UINT32_C(0xfeef04bd))
			status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		else {
			value.size = sizeof(value);
			value.version = 1;
			value.file[0] = HIWORD(native.dwFileVersionMS);
			value.file[1] = LOWORD(native.dwFileVersionMS);
			value.file[2] = HIWORD(native.dwFileVersionLS);
			value.file[3] = LOWORD(native.dwFileVersionLS);
			value.product[0] = HIWORD(native.dwProductVersionMS);
			value.product[1] = LOWORD(native.dwProductVersionMS);
			value.product[2] = HIWORD(native.dwProductVersionLS);
			value.product[3] = LOWORD(native.dwProductVersionLS);
			value.flags =
				native.dwFileFlags & native.dwFileFlagsMask;
			value.type = native.dwFileType;
			value.subtype = native.dwFileSubtype;
			*out = value;
			status = hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}

	hx_os_memory_free(resource.data);

	return status;
}

static hx_os_status hx_os_version_field(const hx_os_version_resource *resource,
	const char *field, uint16_t language, uint16_t code_page,
	hx_string *out, hx_os_error *error)
{
	wchar_t block[128], name[65];
	void *data = NULL;
	UINT units = 0;
	size_t i, bytes;

	for (i = 0; field[i]; ++i)
		name[i] = (wchar_t)(unsigned char)field[i];
	name[i] = 0;
	if (swprintf(block, 128, L"\\StringFileInfo\\%04x%04x\\%ls",
		    (unsigned)language, (unsigned)code_page, name) < 0)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
	if (!VerQueryValueW(resource->data, block, &data, &units))
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (!units || units > resource->bytes / sizeof(wchar_t))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	bytes = (size_t)units * sizeof(wchar_t);
	if (!hx_os_version_contains(resource, data, bytes) ||
		((const wchar_t *)data)[units - 1] != 0)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	for (i = 0; i + 1 < units; ++i)
		if (!((const wchar_t *)data)[i])
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	return hx_string_assign_wide(out, data, units - 1, error);
}

hx_os_status hx_os_i_windows_version_string(const char *path, const char *field,
	uint16_t language, uint16_t code_page, hx_string *out,
	hx_os_error *error)
{
	hx_os_error local;
	hx_os_version_resource resource = {NULL, 0};
	void *translations = NULL;
	UINT bytes = 0, offset;
	hx_os_status status;

	if (!error)
		error = &local;

	status = hx_os_version_load(out->context, path, &resource, error);
	if (status != HX_OS_OK)
		return status;

	if (language || code_page)
		status = hx_os_version_field(
			&resource, field, language, code_page, out, error);
	else if (!VerQueryValueW(resource.data, L"\\VarFileInfo\\Translation",
			 &translations, &bytes))
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	else if (bytes % 4 ||
		!hx_os_version_contains(&resource, translations, bytes))
		status = hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	else if (bytes > 256 * 4)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else {
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		for (offset = 0; offset < bytes; offset += 4) {
			uint16_t pair[2];

			memcpy(pair, (const char *)translations + offset, 4);
			status = hx_os_version_field(
				&resource, field, pair[0], pair[1], out, error);
			if (status != HX_OS_NOT_FOUND)
				break;
		}
	}

	hx_os_memory_free(resource.data);

	return status;
}
