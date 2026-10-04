#include "vm_private.h"

static DWORD hx_os_vm_native_protection(unsigned flags)
{
	if (flags & HX_OS_PAGE_EXECUTE)
		return flags & HX_OS_PAGE_WRITE ? PAGE_EXECUTE_READWRITE
						: PAGE_EXECUTE_READ;

	if (flags & HX_OS_PAGE_WRITE)
		return PAGE_READWRITE;

	return flags & HX_OS_PAGE_READ ? PAGE_READONLY : PAGE_NOACCESS;
}

hx_os_status hx_os_i_vm_sizes(
	size_t *page, size_t *granularity, hx_os_error *error)
{
	SYSTEM_INFO info;

	GetSystemInfo(&info);
	if (!info.dwPageSize || !info.dwAllocationGranularity)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	*page = info.dwPageSize;
	*granularity = info.dwAllocationGranularity;

	return HX_OS_OK;
}

hx_os_status hx_os_i_pages_alloc(
	hx_os_pages *pages, unsigned protection, hx_os_error *error)
{
	pages->base = VirtualAlloc(NULL, pages->bytes, MEM_RESERVE | MEM_COMMIT,
		hx_os_vm_native_protection(protection));

	return pages->base ? hx_os_i_error(error, HX_OS_OK, 0, 0)
			   : hx_os_i_win32(error, GetLastError());
}

hx_os_status hx_os_i_pages_protect(
	hx_os_pages *pages, unsigned protection, hx_os_error *error)
{
	DWORD previous;

	if (!VirtualProtect(pages->base, pages->bytes,
		    hx_os_vm_native_protection(protection), &previous))
		return hx_os_i_win32(error, GetLastError());
	if ((protection & HX_OS_PAGE_EXECUTE) &&
		!FlushInstructionCache(
			GetCurrentProcess(), pages->base, pages->bytes))
		return hx_os_i_win32(error, GetLastError());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pages_free(hx_os_pages *pages, hx_os_error *error)
{
	return VirtualFree(pages->base, 0, MEM_RELEASE)
		? hx_os_i_error(error, HX_OS_OK, 0, 0)
		: hx_os_i_win32(error, GetLastError());
}

hx_os_status hx_os_i_map_create(hx_os_file *file, uint64_t offset,
	hx_os_file_map *map, hx_os_error *error)
{
	DWORD code, protection = PAGE_READONLY, access = FILE_MAP_READ;

	if (map->mode == HX_OS_MAP_SHARED_WRITE) {
		protection = PAGE_READWRITE;
		access = FILE_MAP_WRITE;
	}
	else if (map->mode == HX_OS_MAP_PRIVATE_COPY) {
		protection = PAGE_WRITECOPY;
		access = FILE_MAP_COPY;
	}

	if (!DuplicateHandle(GetCurrentProcess(), file->handle,
		    GetCurrentProcess(), &map->file, 0, FALSE,
		    DUPLICATE_SAME_ACCESS))
		return hx_os_i_win32(error, GetLastError());
	map->mapping =
		CreateFileMappingW(map->file, NULL, protection, 0, 0, NULL);
	if (!map->mapping) {
		code = GetLastError();
		CloseHandle(map->file);

		return hx_os_i_win32(error, code);
	}

	map->base = MapViewOfFile(map->mapping, access, (DWORD)(offset >> 32),
		(DWORD)offset, map->mapped_bytes);
	if (!map->base) {
		code = GetLastError();
		CloseHandle(map->mapping);
		CloseHandle(map->file);

		return hx_os_i_win32(error, code);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_flush(
	hx_os_file_map *map, int durable, hx_os_error *error)
{
	if (!FlushViewOfFile(map->base, map->mapped_bytes))
		return hx_os_i_win32(error, GetLastError());

	if (durable && !FlushFileBuffers(map->file))
		return hx_os_i_win32(error, GetLastError());

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_map_unmap(hx_os_file_map *map, hx_os_error *error)
{
	return UnmapViewOfFile(map->base)
		? hx_os_i_error(error, HX_OS_OK, 0, 0)
		: hx_os_i_win32(error, GetLastError());
}

hx_os_status hx_os_i_map_close(hx_os_file_map *map, hx_os_error *error)
{
	DWORD code = 0;

	if (!CloseHandle(map->mapping))
		code = GetLastError();

	if (!CloseHandle(map->file) && !code)
		code = GetLastError();

	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, HX_OS_OK, 0, 0);
}
