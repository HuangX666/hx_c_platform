#ifndef HX_C_PLATFORM_VM_PRIVATE_H
#define HX_C_PLATFORM_VM_PRIVATE_H
#include "file_private.h"
#include "hx_os_vm.h"

struct hx_os_pages {
	void *base;
	size_t bytes;
};
struct hx_os_file_map {
	void *base;
	size_t mapped_bytes;
	size_t delta;
	size_t bytes;
#if defined(_WIN32)
	HANDLE file;
	HANDLE mapping;
#else
	int descriptor;
#endif
	int mode;
};
hx_os_status hx_os_i_vm_sizes(
	size_t *page, size_t *granularity, hx_os_error *error);
hx_os_status hx_os_i_pages_alloc(
	hx_os_pages *pages, unsigned protection, hx_os_error *error);
hx_os_status hx_os_i_pages_protect(
	hx_os_pages *pages, unsigned protection, hx_os_error *error);
hx_os_status hx_os_i_pages_free(hx_os_pages *pages, hx_os_error *error);
hx_os_status hx_os_i_map_create(hx_os_file *file, uint64_t offset,
	hx_os_file_map *map, hx_os_error *error);
hx_os_status hx_os_i_map_flush(
	hx_os_file_map *map, int durable, hx_os_error *error);
hx_os_status hx_os_i_map_unmap(hx_os_file_map *map, hx_os_error *error);
hx_os_status hx_os_i_map_close(hx_os_file_map *map, hx_os_error *error);
#endif
