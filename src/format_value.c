#include "format_private.h"
#include <float.h>
#include <locale.h>
#include <stdio.h>

static size_t hx_os_format_points(const char *text, size_t bytes)
{
	size_t i, count = 0;
	for (i = 0; i < bytes; ++i)
		count += ((unsigned char)text[i] & 0xc0) != 0x80;
	return count;
}

static hx_os_status hx_os_format_pad(hx_string *out, hx_string_view fill,
	size_t count, size_t limit, hx_os_error *error)
{
	char block[64];
	size_t i, per = sizeof(block) / fill.size;
	hx_os_status status;
	if (count > (limit - out->size) / fill.size)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (i = 0; i < per; ++i)
		memcpy(block + i * fill.size, fill.data, fill.size);
	while (count) {
		size_t part = count < per ? count : per;
		status = hx_os_i_format_append(
			out, block, part * fill.size, limit, error);
		if (status != HX_OS_OK)
			return status;
		count -= part;
	}
	return HX_OS_OK;
}

static hx_os_status hx_os_format_quote(
	hx_string *out, hx_string_view text, size_t limit, hx_os_error *error)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;
	hx_os_status status = hx_os_i_format_append(out, "\"", 1, limit, error);
	for (i = 0; i < text.size && status == HX_OS_OK;) {
		unsigned char c = (unsigned char)text.data[i];
		char escaped[6] = {'\\', 'u', '0', '0', '0', '0'};
		const char *piece = text.data + i;
		size_t bytes = 1;
		if (c == '"' || c == '\\') {
			escaped[1] = (char)c;
			piece = escaped;
			bytes = 2;
			++i;
		}
		else if (c < 0x20 || c == 0x7f) {
			escaped[4] = hex[c >> 4];
			escaped[5] = hex[c & 15];
			piece = escaped;
			bytes = 6;
			++i;
		}
		else {
			uint32_t point;
			status = hx_os_i_utf_decode(HX_OS_UTF8,
				(const unsigned char *)piece, text.size - i,
				&point, &bytes);
			if (status != HX_OS_OK)
				break;
			i += bytes;
		}
		status = hx_os_i_format_append(out, piece, bytes, limit, error);
	}
	return status == HX_OS_OK
		? hx_os_i_format_append(out, "\"", 1, limit, error)
		: status;
}

static hx_os_status hx_os_format_integer(hx_string *out,
	const hx_format_arg *arg, const hx_os_format_spec *spec, size_t limit,
	hx_os_error *error)
{
	char digits[66], text[72];
	const char *alphabet = spec->type == 'X' || spec->type == 'B'
		? "0123456789ABCDEF"
		: "0123456789abcdef";
	uint64_t value;
	size_t count = 0, prefix = 0;
	unsigned base = 10;
	int negative = 0;
	char type = spec->type;
	if (spec->has_precision)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (arg->kind == HX_FORMAT_POINTER) {
		if (type && type != 'p')
			return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
		value = (uintptr_t)arg->value.pointer;
		type = 'x';
	}
	else if (arg->kind == HX_FORMAT_I64 || arg->kind == HX_FORMAT_BOOL) {
		negative = arg->value.integer < 0;
		value = negative ? UINT64_C(0) - (uint64_t)arg->value.integer
				 : (uint64_t)arg->value.integer;
	}
	else
		value = arg->value.natural;
	if (type == 'c' || (arg->kind == HX_FORMAT_CHAR && !type)) {
		size_t bytes;
		hx_os_status status;
		if (negative || value > UINT32_MAX || spec->sign ||
			spec->zero || spec->alternate || spec->grouping)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		status = hx_os_i_utf_encode(HX_OS_UTF8, (uint32_t)value,
			(unsigned char *)text, sizeof(text), &bytes);
		return status == HX_OS_OK
			? hx_os_i_format_append(out, text, bytes, limit, error)
			: hx_os_i_error(error, status, 0, 0);
	}
	if (type == 'x' || type == 'X')
		base = 16;
	else if (type == 'b' || type == 'B')
		base = 2;
	else if (type == 'o')
		base = 8;
	else if (type && type != 'd')
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (negative)
		text[prefix++] = '-';
	else if (spec->sign == '+' || spec->sign == ' ')
		text[prefix++] = spec->sign;
	if ((spec->alternate || arg->kind == HX_FORMAT_POINTER) && base != 10) {
		text[prefix++] = '0';
		text[prefix++] = type;
	}
	do {
		digits[count++] = alphabet[value % base];
		value /= base;
	} while (value);
	while (count)
		text[prefix++] = digits[--count];
	return hx_os_i_format_append(out, text, prefix, limit, error);
}

static hx_os_status hx_os_format_real(hx_string *out, const hx_format_arg *arg,
	const hx_os_format_spec *spec, size_t limit, hx_os_error *error)
{
#if HX_OS_HAVE_LOCAL_PRINTF
	char format[12], *text;
	size_t capacity, p = 0;
	int precision, count = -1;
	long double value = arg->value.real;
	char type = spec->type ? spec->type : 'g';
#if defined(_WIN32)
	_locale_t locale;
#else
	locale_t locale, previous;
#endif
	if (!strchr("eEfFgGaA%", type) ||
		(spec->has_precision && spec->precision > 4096))
		return hx_os_i_error(error,
			spec->precision > 4096 ? HX_OS_LIMIT
					       : HX_OS_TYPE_MISMATCH,
			0, 0);
	if (type == '%') {
		value *= 100;
		type = 'f';
	}
	precision = spec->has_precision ? (int)spec->precision : 6;
	capacity = (size_t)LDBL_MAX_10_EXP + 4096 + 64;
	text = hx_os_i_alloc(out->context, capacity, 0);
	if (!text)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
#if defined(_WIN32)
	locale = _create_locale(LC_NUMERIC, "C");
#else
	locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
	if (!locale) {
		hx_os_memory_free(text);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
#if !defined(_WIN32)
	previous = uselocale(locale);
	if (!previous) {
		int code = errno;
		freelocale(locale);
		hx_os_memory_free(text);
		return hx_os_i_errno(error, code);
	}
#endif
	format[p++] = '%';
	if (spec->sign)
		format[p++] = spec->sign;
	if (spec->alternate)
		format[p++] = '#';
	format[p++] = '.';
	format[p++] = '*';
	format[p++] = 'L';
	format[p++] = type;
	format[p] = 0;
	if (!spec->type && !spec->has_precision) {
		/* Find the first significant-digit count that round-trips to
		 * the original source type. This bounded libc path favors
		 * portability; it is not advertised as a Dragonbox-class fast
		 * converter.
		 */
		int max_precision = arg->kind == HX_FORMAT_F32 ? 9
			: arg->kind == HX_FORMAT_F64	       ? 17
							       : DECIMAL_DIG;
		for (precision = 1; precision <= max_precision; ++precision) {
			long double parsed;
#if defined(_WIN32)
			count = _snprintf_l(text, capacity, format, locale,
				precision, value);
			parsed = _strtod_l(text, NULL, locale);
#else
			count = snprintf(
				text, capacity, format, precision, value);
			parsed = strtold(text, NULL);
#endif
			if (arg->kind == HX_FORMAT_F32)
				parsed = (float)parsed;
			else if (arg->kind == HX_FORMAT_F64)
				parsed = (double)parsed;
			if (parsed == value || value != value)
				break;
		}
	}
	else {
#if defined(_WIN32)
		count = _snprintf_l(
			text, capacity, format, locale, precision, value);
#else
		count = snprintf(text, capacity, format, precision, value);
#endif
	}
#if defined(_WIN32)
	_free_locale(locale);
#else
	(void)uselocale(previous);
	freelocale(locale);
#endif
	if (count < 0 || (size_t)count >= capacity) {
		hx_os_memory_free(text);
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}
	if (spec->type == '%')
		text[count++] = '%';
	{
		hx_os_status status = hx_os_i_format_append(
			out, text, (size_t)count, limit, error);
		hx_os_memory_free(text);
		return status;
	}
#else
	(void)out;
	(void)arg;
	(void)spec;
	(void)limit;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_format_group(hx_string *text, char separator,
	unsigned group, size_t limit, hx_os_error *error)
{
	hx_string grouped;
	size_t start = 0, end, i;
	hx_os_status status;
	if (text->size && strchr("+- ", text->data[0]))
		++start;
	if (start + 1 < text->size && text->data[start] == '0' &&
		strchr("xXbBo", text->data[start + 1]))
		start += 2;
	for (end = start; end < text->size; ++end) {
		char c = text->data[end];
		if (group == 3 ? !(c >= '0' && c <= '9')
			       : !strchr("0123456789abcdefABCDEF", c))
			break;
	}
	hx_string_init(&grouped, text->context);
	status = hx_os_i_format_append(
		&grouped, text->data, start, limit, error);
	for (i = start; i < end && status == HX_OS_OK; ++i) {
		if (i > start && (end - i) % group == 0)
			status = hx_os_i_format_append(
				&grouped, &separator, 1, limit, error);
		if (status == HX_OS_OK)
			status = hx_os_i_format_append(
				&grouped, text->data + i, 1, limit, error);
	}
	if (status == HX_OS_OK)
		status = hx_os_i_format_append(&grouped, text->data + end,
			text->size - end, limit, error);
	if (status == HX_OS_OK)
		hx_string_move(text, &grouped);
	hx_string_destroy(&grouped);
	return status;
}

hx_os_status hx_os_i_format_value(hx_string *out, const hx_format_arg *arg,
	const hx_os_format_spec *spec, size_t limit, hx_os_error *error)
{
	hx_string text;
	hx_os_status status = HX_OS_OK;
	size_t points, padding, left = 0, right = 0, prefix = 0;
	int numeric = 1;
	char align = spec->align;
	hx_string_view fill = spec->fill;
	hx_string_init(&text, out->context);
	if (arg->kind == HX_FORMAT_TEXT ||
		(arg->kind == HX_FORMAT_BOOL &&
			(!spec->type || spec->type == 's' ||
				spec->type == '?'))) {
		hx_string_view view = arg->kind == HX_FORMAT_TEXT
			? arg->value.text
			: hx_string_view_from_cstr(
				  arg->value.integer ? "true" : "false");
		size_t invalid;
		numeric = 0;
		if ((spec->type && spec->type != 's' && spec->type != '?') ||
			spec->sign || spec->alternate || spec->zero ||
			spec->grouping || align == '=')
			status = HX_OS_TYPE_MISMATCH;
		else
			status = hx_os_text_validate_utf8(
				view.data, view.size, &invalid);
		if (status == HX_OS_OK && spec->has_precision) {
			size_t i = 0, count = 0;
			while (i < view.size) {
				if (((unsigned char)view.data[i] & 0xc0) !=
						0x80 &&
					count++ == spec->precision)
					break;
				++i;
			}
			view.size = i;
		}
		if (status == HX_OS_OK)
			status = spec->type == '?'
				? hx_os_format_quote(&text, view, limit, error)
				: hx_os_i_format_append(&text, view.data,
					  view.size, limit, error);
	}
	else if (arg->kind == HX_FORMAT_REAL || arg->kind == HX_FORMAT_F32 ||
		arg->kind == HX_FORMAT_F64)
		status = hx_os_format_real(&text, arg, spec, limit, error);
	else if (arg->kind == HX_FORMAT_I64 || arg->kind == HX_FORMAT_U64 ||
		arg->kind == HX_FORMAT_BOOL || arg->kind == HX_FORMAT_CHAR ||
		arg->kind == HX_FORMAT_POINTER) {
		status = hx_os_format_integer(&text, arg, spec, limit, error);
		if (spec->type == 'c' ||
			(arg->kind == HX_FORMAT_CHAR && !spec->type))
			numeric = 0;
	}
	else
		status = HX_OS_TYPE_MISMATCH;
	if (status == HX_OS_OK && spec->grouping) {
		int binary = spec->type && strchr("bBoxX", spec->type);
		if (spec->grouping == ',' && binary)
			status = HX_OS_TYPE_MISMATCH;
		else
			status = hx_os_format_group(&text, spec->grouping,
				binary ? 4 : 3, limit, error);
	}
	if (status != HX_OS_OK)
		goto done;
	points = hx_os_format_points(hx_string_c_str(&text), text.size);
	padding = spec->width > points ? spec->width - points : 0;
	if (!align)
		align = numeric ? '>' : '<';
	if (numeric && spec->zero && !spec->align) {
		align = '=';
		fill = hx_string_view_from_cstr("0");
	}
	if (align == '<')
		right = padding;
	else if (align == '^') {
		left = padding / 2;
		right = padding - left;
	}
	else
		left = padding;
	if (align == '=') {
		if (text.size && strchr("+- ", text.data[0]))
			++prefix;
		if (prefix + 1 < text.size && text.data[prefix] == '0' &&
			strchr("xXbBo", text.data[prefix + 1]))
			prefix += 2;
		status = hx_os_i_format_append(
			out, hx_string_c_str(&text), prefix, limit, error);
	}
	if (status == HX_OS_OK)
		status = hx_os_format_pad(out, fill, left, limit, error);
	if (status == HX_OS_OK)
		status = hx_os_i_format_append(out,
			hx_string_c_str(&text) + prefix, text.size - prefix,
			limit, error);
	if (status == HX_OS_OK)
		status = hx_os_format_pad(out, fill, right, limit, error);
done:
	hx_string_destroy(&text);
	if (status != HX_OS_OK && (!error || error->status == HX_OS_OK))
		return hx_os_i_error(error, status, 0, 0);
	return status;
}
