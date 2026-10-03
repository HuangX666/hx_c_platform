#ifndef HX_C_PLATFORM_FORMAT_H
#define HX_C_PLATFORM_FORMAT_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Arguments borrow their name, text and custom object through the call.
 * Custom callbacks construct UTF-8 in out, honor limit, and return errors;
 * they never throw or retain the output. No global formatter registry exists.
 */
typedef hx_os_status(HX_OS_CALL *hx_format_custom_fn)(const void *object,
	hx_string_view specification, hx_string *out, size_t limit,
	hx_os_error *error);
enum {
	HX_FORMAT_I64,
	HX_FORMAT_U64,
	HX_FORMAT_REAL,
	HX_FORMAT_BOOL,
	HX_FORMAT_CHAR,
	HX_FORMAT_TEXT,
	HX_FORMAT_POINTER,
	HX_FORMAT_CUSTOM,
	HX_FORMAT_F32,
	HX_FORMAT_F64
};
typedef struct hx_format_arg {
	union {
		int64_t integer;
		uint64_t natural;
		long double real;
		hx_string_view text;
		const void *pointer;
		struct {
			const void *object;
			hx_format_custom_fn format;
		} custom;
	} value;
	const char *name;
	int kind;
} hx_format_arg;

typedef struct hx_format_options {
	size_t max_output_bytes;
	size_t max_arguments;
	uint32_t size;
	uint32_t version;
} hx_format_options;
#define HX_FORMAT_OPTIONS_INIT                                                 \
	{                                                                      \
		16 * 1024 * 1024, 1024, sizeof(hx_format_options), 1           \
	}

HX_OS_API hx_os_status HX_OS_CALL hx_format_options_init(
	hx_format_options *options, size_t capacity);
/* {} / {0} / {name}, escapes {{ }}, alignment/fill/sign/width/precision,
 * integer bases, real e/f/g/a/% and !s/!r. Width and text precision count
 * Unicode scalar values, not terminal cells or grapheme clusters.
 * Named and positional references may coexist; auto/manual indices may not.
 * Dynamic width/precision use nested argument references. Locale is invariant.
 * Strings are transactional: any failure leaves out unchanged. Output bytes
 * exclude the trailing NUL. Arguments and format may borrow the old out.
 * error.cause_code is the byte offset of an invalid format field when known.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_format(hx_string *out,
	hx_string_view format, const hx_format_arg *arguments, size_t count,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_format_with_options(hx_string *out,
	hx_string_view format, const hx_format_arg *arguments, size_t count,
	const hx_format_options *options, hx_os_error *error);
/* required includes NUL. OUTPUT_FULL preserves buffer; NULL/0 can query size.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_format_buffer(char *buffer,
	size_t capacity, size_t *required, hx_string_view format,
	const hx_format_arg *arguments, size_t count, hx_os_error *error);
enum { HX_PRINT_STDOUT, HX_PRINT_STDERR };
/* Format fully before output. Console UTF-16 is private to Windows; redirected
 * output is UTF-8 without BOM. Calls through the same C stream are serialized,
 * flush preceding stdio bytes, and write synchronously. I/O can block and fail
 * after partial output; no rollback, persistence or hard deadline is implied.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_print_args(int stream, int newline,
	hx_string_view format, const hx_format_arg *arguments, size_t count,
	hx_os_error *error);

static inline hx_format_arg hx_fmt_i64(int64_t value)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_I64;
	arg.value.integer = value;
	return arg;
}
static inline hx_format_arg hx_fmt_u64(uint64_t value)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_U64;
	arg.value.natural = value;
	return arg;
}
static inline hx_format_arg hx_fmt_real(long double value)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_REAL;
	arg.value.real = value;
	return arg;
}
static inline hx_format_arg hx_fmt_f32(float value)
{
	hx_format_arg arg = hx_fmt_real(value);
	arg.kind = HX_FORMAT_F32;
	return arg;
}
static inline hx_format_arg hx_fmt_f64(double value)
{
	hx_format_arg arg = hx_fmt_real(value);
	arg.kind = HX_FORMAT_F64;
	return arg;
}
static inline hx_format_arg hx_fmt_bool(int value)
{
	hx_format_arg arg = hx_fmt_i64(value != 0);
	arg.kind = HX_FORMAT_BOOL;
	return arg;
}
static inline hx_format_arg hx_fmt_char(uint32_t value)
{
	hx_format_arg arg = hx_fmt_u64(value);
	arg.kind = HX_FORMAT_CHAR;
	return arg;
}
static inline hx_format_arg hx_fmt_view(hx_string_view value)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_TEXT;
	arg.value.text = value;
	return arg;
}
static inline hx_format_arg hx_fmt_str(const char *value)
{
	return hx_fmt_view(hx_string_view_from_cstr(value));
}
static inline hx_format_arg hx_fmt_pointer(const void *value)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_POINTER;
	arg.value.pointer = value;
	return arg;
}
static inline hx_format_arg hx_fmt_custom(
	const void *object, hx_format_custom_fn function)
{
	hx_format_arg arg = {0};
	arg.kind = HX_FORMAT_CUSTOM;
	arg.value.custom.object = object;
	arg.value.custom.format = function;
	return arg;
}
static inline hx_format_arg hx_fmt_named(const char *name, hx_format_arg arg)
{
	arg.name = name;
	return arg;
}

#ifdef __cplusplus
}
#endif
#endif
