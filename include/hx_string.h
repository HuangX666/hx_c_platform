#ifndef HX_C_PLATFORM_STRING_H
#define HX_C_PLATFORM_STRING_H

#include "hx_os_text.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_string_view {
	const char *data;
	size_t size;
} hx_string_view;

/*
 * Owning UTF-8 value. Initialize exactly once, destroy when finished.
 * Do not copy by assignment; use copy/move. Members are implementation
 * storage and must not be modified directly. Embedded U+0000 is allowed;
 * c_str consumers still stop at the first NUL. Views borrow storage and
 * must not outlive the owner or subsequent mutation. No concurrent mutation.
 */
typedef struct hx_string {
	char *data;
	size_t size;
	size_t capacity;
	hx_os_context *context;
} hx_string;

#define HX_STRING_INIT                                                         \
	{                                                                      \
		NULL, 0, 0, NULL                                               \
	}
#define HX_STRING_NOT_FOUND SIZE_MAX

HX_OS_API void HX_OS_CALL hx_string_init(
	hx_string *string, hx_os_context *context);
HX_OS_API void HX_OS_CALL hx_string_destroy(hx_string *string);
HX_OS_API const char *HX_OS_CALL hx_string_c_str(const hx_string *string);
HX_OS_API size_t HX_OS_CALL hx_string_size_bytes(const hx_string *string);
HX_OS_API size_t HX_OS_CALL hx_string_codepoint_count(const hx_string *string);
HX_OS_API hx_string_view HX_OS_CALL hx_string_view_of(const hx_string *string);
HX_OS_API hx_string_view HX_OS_CALL hx_string_view_from_cstr(const char *text);
HX_OS_API hx_os_status HX_OS_CALL hx_string_reserve(
	hx_string *string, size_t bytes, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_string_clear(hx_string *string);
HX_OS_API hx_os_status HX_OS_CALL hx_string_assign(
	hx_string *string, hx_string_view text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_assign_cstr(
	hx_string *string, const char *text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_append(
	hx_string *string, hx_string_view text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_append_cstr(
	hx_string *string, const char *text, hx_os_error *error);
/* Offsets/counts are bytes and must lie on UTF-8 codepoint boundaries. */
HX_OS_API hx_os_status HX_OS_CALL hx_string_insert(hx_string *string,
	size_t offset, hx_string_view text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_erase(
	hx_string *string, size_t offset, size_t bytes, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_replace(hx_string *string,
	size_t offset, size_t bytes, hx_string_view text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_substring(hx_string *destination,
	const hx_string *source, size_t offset, size_t bytes,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_string_copy(
	hx_string *destination, const hx_string *source, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_string_move(
	hx_string *destination, hx_string *source);
HX_OS_API size_t HX_OS_CALL hx_string_find(
	const hx_string *string, hx_string_view needle, size_t start);
HX_OS_API int HX_OS_CALL hx_string_compare(
	const hx_string *string, hx_string_view other);
/* printf formatting uses the caller's locale; resulting bytes must be UTF-8. */
HX_OS_API hx_os_status HX_OS_CALL hx_string_vformat(hx_string *string,
	hx_os_error *error, const char *format, va_list arguments);
HX_OS_API hx_os_status HX_OS_CALL hx_string_format(
	hx_string *string, hx_os_error *error, const char *format, ...);
HX_OS_API hx_os_status HX_OS_CALL hx_string_append_format(
	hx_string *string, hx_os_error *error, const char *format, ...);

/* Strict decoding is transactional: failure leaves the string unchanged. */
HX_OS_API hx_os_status HX_OS_CALL hx_string_assign_encoded(hx_string *string,
	hx_os_encoding encoding, const void *input, size_t bytes,
	hx_os_error *error);
/* Encoding adds no terminator; the result reports exact byte progress. */
HX_OS_API hx_os_status HX_OS_CALL hx_string_encode(const hx_string *string,
	hx_os_encoding encoding, void *output, size_t capacity,
	hx_os_text_result *result, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
