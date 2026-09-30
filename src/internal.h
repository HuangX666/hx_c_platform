#ifndef HX_C_PLATFORM_INTERNAL_H
#define HX_C_PLATFORM_INTERNAL_H

#include "hx_os_atomic.h"
#include "hx_os_file.h"
#include "hx_os_memory.h"
#include "hx_os_pool.h"
#include "hx_os_text.h"
#include "hx_os_thread.h"
#include "hx_os_config.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct hx_os_context {
	hx_os_allocator allocator;
	hx_os_atomic allocations;
	hx_os_atomic leases;
};

typedef union hx_os_i_alignment {
	long double number;
	void *pointer;
	int64_t integer;
} hx_os_i_alignment;

typedef struct hx_os_i_alignment_probe {
	char prefix;
	hx_os_i_alignment value;
} hx_os_i_alignment_probe;

#define HX_OS_I_ALIGNMENT offsetof(hx_os_i_alignment_probe, value)

hx_os_status hx_os_i_error(
	hx_os_error *error, hx_os_status status, uint32_t domain, int64_t code);
hx_os_status hx_os_i_errno(hx_os_error *error, int code);
hx_os_status hx_os_i_win32(hx_os_error *error, uint32_t code);
void *hx_os_i_alloc(hx_os_context *context, size_t bytes, size_t alignment);
void *hx_os_i_zero(hx_os_context *context, size_t bytes);
size_t hx_os_i_allocation_cost(size_t bytes, size_t alignment);
size_t hx_os_i_allocation_bytes(const void *memory);
void hx_os_i_context_retain(hx_os_context *context);
void hx_os_i_context_release(hx_os_context *context);
uint32_t hx_os_i_wait_ms(hx_os_deadline deadline);
int hx_os_i_add_size(size_t a, size_t b, size_t *out);
int hx_os_i_alignment_valid(size_t alignment);
hx_os_status hx_os_i_path_validate(const char *path, hx_os_error *error);
hx_os_status hx_os_i_utf_decode(hx_os_encoding encoding,
	const unsigned char *input, size_t bytes, uint32_t *point,
	size_t *used);
hx_os_status hx_os_i_utf_encode(hx_os_encoding encoding, uint32_t point,
	unsigned char *output, size_t capacity, size_t *used);

#if defined(_WIN32)
#include <wchar.h>
wchar_t *hx_os_i_windows_path(
	hx_os_context *context, const char *path, hx_os_error *error);
#endif
#endif
