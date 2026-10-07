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

hx_os_status hx_os_i_parameter_name(
	const char *name, size_t *bytes, hx_os_error *error);

struct hx_os_params;
hx_os_status hx_os_i_params_hold(
	struct hx_os_params *params, hx_os_error *error);
void hx_os_i_params_release(struct hx_os_params *params);

void hx_os_i_crypto_error_text(uint64_t code, char *out, size_t capacity);

struct hx_os_library;
/* Order finalizers of concurrently used native modules. Only the unload
 * itself is serialized; allocation and diagnostic callbacks stay outside.
 * Used for pinned native backends whose finalizers do not reenter hx. */
hx_os_status hx_os_i_library_close_ordered(struct hx_os_library **library,
	hx_string *diagnostic, hx_os_error *error);

size_t hx_os_i_allocation_bytes(const void *memory);

void hx_os_i_context_retain(hx_os_context *context);

void hx_os_i_context_release(hx_os_context *context);
#if HX_OS_ENABLE_COROUTINES
/* Bounded execution-local storage, owned by one coroutine. */
void *hx_os_i_coro_local_get(void *key);

hx_os_status hx_os_i_coro_local_set(void *key, void *value,
	hx_os_tls_destructor cleanup, hx_os_error *error);

void hx_os_i_coro_local_clear(void *key);
#endif
uint32_t hx_os_i_wait_ms(hx_os_deadline deadline);

int hx_os_i_add_size(size_t a, size_t b, size_t *out);

int hx_os_i_alignment_valid(size_t alignment);

hx_os_status hx_os_i_path_validate(const char *path, hx_os_error *error);

hx_os_status hx_os_i_path_stat_extended(const char *path, int follow_links,
	hx_os_file_info_extended *info, hx_os_error *error);

hx_os_status hx_os_i_text_copy(const char *text, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error);

hx_os_status hx_os_i_env_get(const char *name, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error);

hx_os_status hx_os_i_env_get_string(
	const char *name, hx_string *out, hx_os_error *error);

hx_os_status hx_os_i_utf_decode(hx_os_encoding encoding,
	const unsigned char *input, size_t bytes, uint32_t *point,
	size_t *used);

hx_os_status hx_os_i_utf_encode(hx_os_encoding encoding, uint32_t point,
	unsigned char *output, size_t capacity, size_t *used);

struct tm;
hx_os_status hx_os_i_calendar_tm_fields(
	const struct tm *local, unsigned fields[8], hx_os_error *error);

#if defined(_WIN32)
#include <wchar.h>

hx_os_status hx_os_i_windows_path_text(
	hx_string *out, wchar_t *wide, size_t length, hx_os_error *error);

wchar_t *hx_os_i_windows_utf16(
	hx_os_context *context, const char *text, hx_os_error *error);

wchar_t *hx_os_i_windows_path(
	hx_os_context *context, const char *path, hx_os_error *error);
#endif
#endif
