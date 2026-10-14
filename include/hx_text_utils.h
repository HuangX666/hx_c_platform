#ifndef HX_C_PLATFORM_TEXT_UTILS_H
#define HX_C_PLATFORM_TEXT_UTILS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Borrowed cursor, with fixed implementation layout.
 *
 * \details Initialize before use; do not edit fields. Copying a cursor creates
 * an independent position over the same input. Both input and delimiter must
 * remain alive and unchanged until iteration ends. Returned views borrow input,
 * not the cursor.
 */
typedef struct hx_os_text_split {
	hx_string_view input;
	hx_string_view delimiter;
	size_t position;
	int keep_empty;
	int finished;
} hx_os_text_split;

/**
 * \brief Allocation-free exact-delimiter splitting.
 *
 * \details The delimiter is one nonempty byte sequence, not a set of
 * characters. Both views are validated as UTF-8 once at init; embedded NUL is
 * supported. Delimiters match left to right without overlap. Cursor/output
 * objects must not overlap the borrowed data. keep_empty must be 0 or 1. When
 * 1, empty input yields one empty part and a trailing delimiter yields a
 * trailing empty part; when 0, these are skipped. Successful init replaces
 * cursor state; failed init leaves it unchanged.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_split_init(
	hx_os_text_split *split, hx_string_view input, hx_string_view delimiter,
	int keep_empty, hx_os_error *error);

/**
 * \brief OK returns one view, EOF means exhaustion.
 *
 * \details Output is unchanged on failure or EOF. next never allocates or
 * revalidates the entire UTF-8 input. A view need not be NUL-terminated; use
 * its explicit size. Output must not overlap the cursor. No concurrent cursor
 * use.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_split_next(
	hx_os_text_split *split, hx_string_view *out, hx_os_error *error);

/**
 * \brief Whole-token ASCII integers, with base exactly 2, 8, 10 or 16.
 *
 * \details No prefix, whitespace, underscore, decimal point, exponent or
 * trailing NUL. Optional leading '+' is accepted; i64 also accepts '-', while
 * u64 rejects even -0. Invalid arguments/base return INVALID, malformed tokens
 * TYPE_MISMATCH and numeric overflow RANGE. All failures preserve *out. No
 * allocation/locale. These two functions follow HX_OS_ENABLE_TEXT_NUMBER;
 * disabled builds return NOT_SUPPORTED. They do not change the existing
 * parse_double grammar.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_parse_i64(hx_string_view text,
	unsigned base, int64_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_parse_u64(hx_string_view text,
	unsigned base, uint64_t *out, hx_os_error *error);

/**
 * \brief Total natural ordering, returning -1/0/1.
 *
 * \details Views must reference valid byte ranges (NULL is allowed only with
 * size=0). No encoding validation, locale, case folding or allocation.
 * Non-digit UTF-8 bytes compare as unsigned bytes. When both positions start
 * ASCII decimal runs, compare their numeric value without converting to an
 * integer. Equal values compare by shorter run first: file2 < file02 < file002.
 * This tie-break precedes following text. Signs and decimal separators have no
 * special meaning. Equality means the entire byte sequences are equal,
 * including embedded NULs and leading zeros.
 */
HX_OS_API int HX_OS_CALL hx_os_text_compare_natural(
	hx_string_view left, hx_string_view right);

#ifdef __cplusplus
}
#endif
#endif
