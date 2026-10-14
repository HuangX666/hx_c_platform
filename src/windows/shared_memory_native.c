#include "shared_memory_private.h"
#include <windows.h>

hx_os_status hx_os_i_shm_native_open(hx_os_shared_memory *memory,
	const char *name, size_t bytes, int create, hx_os_error *error)
{
	hx_string text;
	wchar_t *wide;
	HANDLE handle;
	void *mapping;
	DWORD code;
	hx_os_status status;
	size_t size = create ? bytes + 64 : 64;

	hx_string_init(&text, memory->context);
	status = hx_string_format(&text, error, "Local\\hxcp_%s", name);
	if (status != HX_OS_OK) {
		hx_string_destroy(&text);
		return status;
	}
	wide = hx_os_i_windows_utf16(
		memory->context, hx_string_c_str(&text), error);
	hx_string_destroy(&text);
	if (!wide)
		return error ? error->status : HX_OS_NO_MEMORY;
	if (create) {
		handle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL,
			PAGE_READWRITE, 0, (DWORD)size, wide);
		code = GetLastError();
		if (handle && code == ERROR_ALREADY_EXISTS) {
			CloseHandle(handle);
			hx_os_memory_free(wide);
			return hx_os_i_error(
				error, HX_OS_EXISTS, HX_OS_ERROR_WIN32, code);
		}
	}
	else {
		handle = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide);
		code = GetLastError();
	}
	hx_os_memory_free(wide);
	if (!handle)
		return hx_os_i_win32(error, code);
	mapping = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
	if (!mapping) {
		code = GetLastError();
		CloseHandle(handle);
		return hx_os_i_win32(error, code);
	}
	memory->native = (intptr_t)handle;
	memory->mapping = mapping;
	memory->mapped_bytes = size;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_shm_native_remap(
	hx_os_shared_memory *memory, size_t bytes, hx_os_error *error)
{
	void *mapping = MapViewOfFile(
		(HANDLE)memory->native, FILE_MAP_ALL_ACCESS, 0, 0, bytes);

	if (!mapping)
		return hx_os_i_win32(error, GetLastError());
	if (!UnmapViewOfFile(memory->mapping)) {
		DWORD code = GetLastError();

		UnmapViewOfFile(mapping);
		return hx_os_i_win32(error, code);
	}
	memory->mapping = mapping;
	memory->mapped_bytes = bytes;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_shm_native_close(
	hx_os_shared_memory *memory, hx_os_error *error)
{
	if (!memory->mapping)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (!UnmapViewOfFile(memory->mapping))
		return hx_os_i_win32(error, GetLastError());
	memory->mapping = NULL;
	return CloseHandle((HANDLE)memory->native)
		? hx_os_i_error(error, HX_OS_OK, 0, 0)
		: hx_os_i_win32(error, GetLastError());
}

hx_os_status hx_os_i_shm_native_unlink(const char *name, hx_os_error *error)
{
	(void)name;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
