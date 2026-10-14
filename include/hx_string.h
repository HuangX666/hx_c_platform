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

/**
 * \brief Owning UTF-8 value.
 *
 * \details Initialize exactly once, destroy when finished. Do not copy by
 * assignment; use copy/move. Members are implementation storage and must not be
 * modified directly. Embedded U+0000 is allowed; c_str consumers still stop at
 * the first NUL. Views borrow storage and must not outlive the owner or
 * subsequent mutation. No concurrent mutation.
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

/**
 * \brief Offsets/counts are bytes and must lie on UTF-8 codepoint boundaries.
 */
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

/**
 * \brief Locale-independent ASCII case-insensitive operations, without
 * allocation.
 *
 * \details A-Z compare as a-z; non-ASCII UTF-8 bytes remain exact (no Unicode
 * folding). compare requires valid views and returns -1/0/1. find uses byte
 * offsets; invalid needles/starts return NOT_FOUND, like the case-sensitive
 * version.
 */
HX_OS_API int HX_OS_CALL hx_string_compare_ascii_nocase(
	const hx_string *string, hx_string_view other);

HX_OS_API size_t HX_OS_CALL hx_string_find_ascii_nocase(
	const hx_string *string, hx_string_view needle, size_t start);

/**
 * \brief printf formatting uses the caller's locale; resulting bytes must be
 * UTF-8.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_vformat(hx_string *string,
	hx_os_error *error, const char *format, va_list arguments);

HX_OS_API hx_os_status HX_OS_CALL hx_string_format(
	hx_string *string, hx_os_error *error, const char *format, ...);

HX_OS_API hx_os_status HX_OS_CALL hx_string_append_format(
	hx_string *string, hx_os_error *error, const char *format, ...);

/**
 * \brief Strict decoding is transactional: failure leaves the string unchanged.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_assign_encoded(hx_string *string,
	hx_os_encoding encoding, const void *input, size_t bytes,
	hx_os_error *error);

/**
 * \brief units counts wchar_t elements, excluding an optional trailing NUL.
 *
 * \details Strict native-wide conversion. Failure preserves the owning string.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_assign_wide(hx_string *string,
	const wchar_t *input, size_t units, hx_os_error *error);

/**
 * \brief Encoding adds no terminator; the result reports exact byte progress.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_encode(const hx_string *string,
	hx_os_encoding encoding, void *output, size_t capacity,
	hx_os_text_result *result, hx_os_error *error);

/**
 * \brief Common operations preserve UTF-8 and use the string's own context.
 *
 * \details Trimming/case conversion affects ASCII only (Unicode bytes stay
 * intact). Prefix/suffix predicates return false for an invalid view.
 */
HX_OS_API int HX_OS_CALL hx_string_starts_with(
	const hx_string *string, hx_string_view prefix);

HX_OS_API int HX_OS_CALL hx_string_ends_with(
	const hx_string *string, hx_string_view suffix);

HX_OS_API hx_os_status HX_OS_CALL hx_string_trim_ascii(
	hx_string *string, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_string_lower_ascii(
	hx_string *string, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_string_upper_ascii(
	hx_string *string, hx_os_error *error);

/**
 * \brief Allocation-free edits on a valid initialized string.
 *
 * \details No Unicode folding. string must be non-NULL; borrowed views may
 * change after these calls.
 */
HX_OS_API void HX_OS_CALL hx_string_trim_ascii_in_place(hx_string *string);

HX_OS_API void HX_OS_CALL hx_string_lower_ascii_in_place(hx_string *string);

HX_OS_API void HX_OS_CALL hx_string_upper_ascii_in_place(hx_string *string);

/**
 * \brief Left-to-right nonoverlapping replacement; empty needle is INVALID.
 *
 * \details Inputs may borrow from string. Failure preserves string and
 * replaced.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_replace_all(hx_string *string,
	hx_string_view needle, hx_string_view replacement, size_t *replaced,
	hx_os_error *error);

/**
 * \brief Owns a copy of the input; views remain valid until parts_destroy.
 *
 * \details Embedded NUL is supported. Default: retain empty pieces, at most
 * 4096. ex max_parts is 1..65536; excess pieces fail with LIMIT, never
 * truncate. Empty separator is INVALID. Failure sets *out=NULL.
 */
typedef struct hx_string_parts hx_string_parts;

HX_OS_API hx_os_status HX_OS_CALL hx_string_split(const hx_string *string,
	hx_string_view separator, hx_string_parts **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_string_split_ex(const hx_string *string,
	hx_string_view separator, size_t max_parts, int keep_empty,
	hx_string_parts **out, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_string_parts_count(const hx_string_parts *parts);

HX_OS_API hx_os_status HX_OS_CALL hx_string_parts_at(
	const hx_string_parts *parts, size_t index, hx_string_view *out);

HX_OS_API void HX_OS_CALL hx_string_parts_destroy(hx_string_parts **parts);

/**
 * \brief Transactional, validates each UTF-8 view.
 *
 * \details Parts may borrow from out. count=0 produces an empty string. Parts
 * array and output object may not overlap. No locale collation, Unicode
 * normalization or case folding.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_string_join(hx_string *out,
	const hx_string_view *parts, size_t count, hx_string_view separator,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
