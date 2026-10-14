#ifndef HX_C_PLATFORM_SHARED_MEMORY_PRIVATE_H
#define HX_C_PLATFORM_SHARED_MEMORY_PRIVATE_H

#include "internal.h"
#include "hx_os_shared_memory.h"

typedef struct hx_os_shm_header {
	hx_os_atomic32 ready;
	unsigned char fields[60];
} hx_os_shm_header;

typedef char
	hx_os_shm_header_size_check[sizeof(hx_os_shm_header) == 64 ? 1 : -1];

struct hx_os_shared_memory {
	hx_os_context *context;
	hx_os_file *file;
	hx_os_file_map *file_map;
	void *mapping;
	intptr_t native;
	size_t mapped_bytes, bytes;
};

hx_os_status hx_os_i_shm_native_open(hx_os_shared_memory *memory,
	const char *name, size_t bytes, int create, hx_os_error *error);
hx_os_status hx_os_i_shm_native_remap(
	hx_os_shared_memory *memory, size_t bytes, hx_os_error *error);
hx_os_status hx_os_i_shm_native_close(
	hx_os_shared_memory *memory, hx_os_error *error);
hx_os_status hx_os_i_shm_native_unlink(const char *name, hx_os_error *error);
#endif
