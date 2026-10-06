#ifndef HX_C_PLATFORM_TEXT_H
#define HX_C_PLATFORM_TEXT_H

#include "hx_os_base.h"
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_text_converter hx_os_text_converter;

typedef enum hx_os_encoding {
	HX_OS_UTF8,
	HX_OS_UTF16LE,
	HX_OS_UTF16BE,
	HX_OS_UTF32LE,
	HX_OS_UTF32BE,
	HX_OS_ASCII,
	HX_OS_GB2312,
	HX_OS_GBK,
	HX_OS_GB18030_2005,
	HX_OS_GB18030_2022,
	HX_OS_BIG5,
	HX_OS_SHIFT_JIS,
	HX_OS_EUC_JP,
	HX_OS_EUC_KR,
	HX_OS_WINDOWS_1252,
	HX_OS_WINDOWS_932,
	HX_OS_WINDOWS_936,
	HX_OS_WINDOWS_949,
	HX_OS_WINDOWS_950,
	HX_OS_ISO_8859_1
} hx_os_encoding;

enum { HX_OS_TEXT_STRICT, HX_OS_TEXT_REPLACE };

typedef struct hx_os_text_result {
	size_t input_used;
	size_t output_used;
	size_t replacements;
	uint64_t error_offset;
} hx_os_text_result;

/*
 * All lengths are bytes, including UTF-16/32. No NUL or BOM is added/removed.
 * No normalization, transliteration, or encoding guessing. Strict is the
 * default. Replacement uses U+FFFD when representable, otherwise '?'.
 * Output may contain a valid prefix on error. Input/output cannot overlap.
 */
HX_OS_API const char *HX_OS_CALL hx_os_encoding_name(hx_os_encoding value);

HX_OS_API int HX_OS_CALL hx_os_encoding_supported(hx_os_encoding value);

/*
 * Detect the longest complete BOM in this complete prefix buffer (not a
 * streaming decoder). Recognizes UTF-8/16LE/16BE/32LE/32BE. No BOM returns
 * NOT_FOUND, prefix_bytes=0, and leaves encoding unchanged. BOM presence
 * does not validate the remaining text. UTF-32 is checked before UTF-16.
 * bom_bytes writes at most 4 bytes, no terminator. Other encodings return
 * NOT_SUPPORTED; output bytes remain unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_bom_detect(const void *data,
	size_t bytes, hx_os_encoding *encoding, size_t *prefix_bytes);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_bom_bytes(
	hx_os_encoding encoding, unsigned char out[4], size_t *bytes);

/*
 * Locale-independent complete numeric token conversion. Parsing accepts the
 * existing configuration numeric grammar: decimal/exponent, underscores,
 * unsigned 0x/0o/0b integers, and signed inf/nan. No surrounding whitespace
 * or trailing NUL is included in bytes. Failure leaves *out unchanged.
 * Overflow/underflow is RANGE. Formatting preserves round trips for finite
 * IEEE binary64 values, canonicalizes infinities/NaNs and includes NUL in
 * required. No process locale changes; POSIX format restores thread locale.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_parse_double(
	const char *text, size_t bytes, double *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_format_double(
	double value, char *buffer, size_t capacity, size_t *required);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_validate_utf8(
	const void *input, size_t bytes, size_t *error_offset);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_converter_create(
	hx_os_context *context, hx_os_encoding source, hx_os_encoding target,
	int policy, hx_os_text_converter **out, hx_os_error *error);

/*
 * A converter is serialized by its caller. Unconsumed input must be
 * resubmitted, including an incomplete trailing sequence on NEED_INPUT.
 * final_input distinguishes incomplete input from truncated final input.
 * After final success or a strict error, reset before a new conversion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_converter_update(
	hx_os_text_converter *converter, const void *input, size_t bytes,
	void *output, size_t capacity, int final_input,
	hx_os_text_result *result, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_text_converter_reset(
	hx_os_text_converter *converter);

HX_OS_API void HX_OS_CALL hx_os_text_converter_destroy(
	hx_os_text_converter *converter);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_convert(hx_os_encoding source,
	hx_os_encoding target, const void *input, size_t bytes, void *output,
	size_t capacity, hx_os_text_result *result, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_measure(hx_os_encoding source,
	hx_os_encoding target, const void *input, size_t bytes,
	size_t *required, hx_os_error *error);

/* Native wchar_t (UTF-16 on Windows, UTF-32 on supported POSIX targets).
 * Strict, locale-independent, with no BOM handling. Lengths exclude the
 * terminator; capacity/required count destination units and include NUL.
 * NULL output with capacity=0 measures and returns OUTPUT_FULL. Embedded
 * NUL is preserved. Failure leaves output unchanged; required is zero on
 * invalid input. Input/output/required must not overlap. No allocation is
 * transferred to the caller. Unsupported wchar_t widths return NOT_SUPPORTED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_to_wide(const char *input,
	size_t bytes, wchar_t *output, size_t capacity, size_t *required,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_from_wide(const wchar_t *input,
	size_t units, char *output, size_t capacity, size_t *required,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
