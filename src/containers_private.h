#ifndef HX_C_PLATFORM_CONTAINERS_PRIVATE_H
#define HX_C_PLATFORM_CONTAINERS_PRIVATE_H

#include "internal.h"
#include "hx_container.h"

typedef struct hx_os_container_base {
	hx_os_context *context;
	hx_os_container_options options;
	size_t memory;
	uint64_t generation;
	hx_os_status allocation_status;
	int busy;
} hx_os_container_base;

hx_os_status hx_os_i_container_init(hx_os_container_base *base,
	hx_os_context *context, const hx_os_container_options *options,
	size_t object_size, int set_allowed);
void *hx_os_i_container_alloc(
	hx_os_container_base *base, size_t bytes, size_t alignment);
void hx_os_i_container_free(hx_os_container_base *base, void *memory);
hx_os_status hx_os_i_container_enter(hx_os_container_base *base);
hx_os_status hx_os_i_container_leave(
	hx_os_container_base *base, hx_os_status status, hx_os_error *error);
int hx_os_i_container_overlap(
	const void *a, size_t a_bytes, const void *b, size_t b_bytes);
hx_os_status hx_os_i_container_copy(hx_os_container_base *base,
	void *destination, const void *source, int take);
void hx_os_i_container_drop(hx_os_container_base *base, void *value);

#endif
