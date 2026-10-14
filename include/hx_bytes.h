#ifndef HX_C_PLATFORM_BYTES_H
#define HX_C_PLATFORM_BYTES_H

#include "hx_os_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Owned arbitrary bytes, no encoding/terminator.
 *
 * \details Same ownership rules as hx_string: initialize once, use copy/move,
 * exclude concurrent mutation. Members are internal storage. Each capacity is
 * at most 64 MiB; growth temporarily retains old+new allocations. Failure
 * preserves the value.
 */
typedef struct hx_bytes {
	unsigned char *data;
	size_t size, capacity;
	hx_os_context *context;
} hx_bytes;

#define HX_BYTES_INIT                                                          \
	{                                                                      \
		NULL, 0, 0, NULL                                               \
	}
#define HX_BYTES_MAX_SIZE ((size_t)64 * 1024 * 1024)

HX_OS_API void HX_OS_CALL hx_bytes_init(
	hx_bytes *bytes, hx_os_context *context);
HX_OS_API void HX_OS_CALL hx_bytes_destroy(hx_bytes *bytes);
HX_OS_API hx_os_bytes_view HX_OS_CALL hx_bytes_view(const hx_bytes *bytes);
HX_OS_API hx_os_status HX_OS_CALL hx_bytes_reserve(
	hx_bytes *bytes, size_t capacity, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_bytes_resize(
	hx_bytes *bytes, size_t size, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_bytes_assign(
	hx_bytes *bytes, hx_os_bytes_view input, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_bytes_append(
	hx_bytes *bytes, hx_os_bytes_view input, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_bytes_move(
	hx_bytes *destination, hx_bytes *source);

#ifdef __cplusplus
}
#endif
#endif
