#include "number_private.h"
#include <float.h>
#include <locale.h>
#include <stdio.h>

int hx_os_i_number_digit(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hx_os_number_digits(const char *text, size_t bytes, size_t *position,
	int base, int underscores)
{
	size_t start = *position;
	int last_digit = 0;
	while (*position < bytes) {
		int digit =
			hx_os_i_number_digit((unsigned char)text[*position]);
		if (digit >= 0 && digit < base)
			last_digit = 1;
		else if (text[*position] == '_' && underscores && last_digit)
			last_digit = 0;
		else
			break;
		++*position;
	}
	return *position > start && last_digit;
}

int hx_os_i_number_valid(
	const char *text, size_t bytes, int extended, int *real)
{
	size_t i = 0, start;
	int sign = 0;
	*real = 0;
	if (!bytes)
		return 0;
	if (text[i] == '-' || (extended && text[i] == '+')) {
		sign = 1;
		if (++i == bytes)
			return 0;
	}
	if (extended && bytes - i == 3 &&
		(!memcmp(text + i, "inf", 3) || !memcmp(text + i, "nan", 3))) {
		*real = 1;
		return 1;
	}
	if (extended && !sign && bytes - i > 2 && text[i] == '0') {
		int base = text[i + 1] == 'x' ? 16
			: text[i + 1] == 'o'  ? 8
			: text[i + 1] == 'b'  ? 2
					      : 0;
		if (base) {
			i += 2;
			return hx_os_number_digits(text, bytes, &i, base, 1) &&
				i == bytes;
		}
	}
	start = i;
	if (!hx_os_number_digits(text, bytes, &i, 10, extended))
		return 0;
	if (text[start] == '0' && i - start > 1)
		return 0;
	if (i < bytes && text[i] == '.') {
		*real = 1;
		++i;
		if (!hx_os_number_digits(text, bytes, &i, 10, extended))
			return 0;
	}
	if (i < bytes && (text[i] == 'e' || text[i] == 'E')) {
		*real = 1;
		++i;
		if (i < bytes && (text[i] == '+' || text[i] == '-'))
			++i;
		if (!hx_os_number_digits(text, bytes, &i, 10, extended))
			return 0;
	}
	return i == bytes;
}

hx_os_status hx_os_i_number_integer(
	const char *text, size_t bytes, uint64_t *magnitude, int *negative)
{
	size_t i = 0;
	int real, base = 10;
	uint64_t result = 0;
	if (!hx_os_i_number_valid(text, bytes, 1, &real) || real)
		return HX_OS_TYPE_MISMATCH;
	*negative = text[0] == '-';
	if (text[0] == '-' || text[0] == '+')
		++i;
	if (bytes - i > 2 && text[i] == '0') {
		char prefix = text[i + 1];
		if (prefix == 'x' || prefix == 'o' || prefix == 'b') {
			base = prefix == 'x' ? 16 : prefix == 'o' ? 8 : 2;
			i += 2;
		}
	}
	for (; i < bytes; ++i) {
		unsigned digit;
		if (text[i] == '_')
			continue;
		digit = (unsigned)hx_os_i_number_digit((unsigned char)text[i]);
		if (result > (UINT64_MAX - digit) / (unsigned)base)
			return HX_OS_RANGE;
		result = result * (unsigned)base + digit;
	}
	*magnitude = result;
	return HX_OS_OK;
}

hx_os_status hx_os_i_strtod_c(
	const char *text, char **end, double *value, int *native_error)
{
#if HX_OS_HAVE_LOCAL_STRTOD
#if defined(_WIN32)
	_locale_t locale = _create_locale(LC_NUMERIC, "C");
#else
	locale_t locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
	if (!locale) {
		if (end)
			*end = (char *)text;
		*value = 0;
		*native_error = ENOMEM;
		return HX_OS_NO_MEMORY;
	}
	errno = 0;
#if defined(_WIN32)
	*value = _strtod_l(text, end, locale);
#else
	*value = strtod_l(text, end, locale);
#endif
	*native_error = errno;
#if defined(_WIN32)
	_free_locale(locale);
#else
	freelocale(locale);
#endif
	return HX_OS_OK;
#else
	if (end)
		*end = (char *)text;
	*value = 0;
	*native_error = ENOSYS;
	return HX_OS_NOT_SUPPORTED;
#endif
}

hx_os_status hx_os_i_parse_double(hx_os_context *context, const char *text,
	size_t bytes, double *out, hx_os_error *error)
{
#if HX_OS_HAVE_LOCAL_STRTOD
	char *copy, *end;
	size_t i, n = 0;
	int real, native_error;
	double value;
	hx_os_status status;
	if (!out || (!text && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!hx_os_i_number_valid(text, bytes, 1, &real))
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	if (!real) {
		uint64_t magnitude;
		int negative;
		status = hx_os_i_number_integer(
			text, bytes, &magnitude, &negative);
		if (status == HX_OS_OK) {
			*out = negative ? -(double)magnitude
					: (double)magnitude;
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}
	copy = hx_os_i_alloc(context, bytes + 1, 0);
	if (!copy)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	for (i = 0; i < bytes; ++i)
		if (text[i] != '_')
			copy[n++] = text[i];
	copy[n] = 0;
	status = hx_os_i_strtod_c(copy, &end, &value, &native_error);
	if (status == HX_OS_OK)
		status = end != copy + n	 ? HX_OS_TYPE_MISMATCH
			: native_error == ERANGE ? HX_OS_RANGE
						 : HX_OS_OK;
	hx_os_memory_free(copy);
	if (status == HX_OS_OK)
		*out = value;
	return hx_os_i_error(error, status,
		native_error ? HX_OS_ERROR_ERRNO : HX_OS_ERROR_NONE,
		native_error);
#else
	(void)context;
	(void)text;
	(void)bytes;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_text_parse_double(
	const char *text, size_t bytes, double *out, hx_os_error *error)
{
#if HX_OS_ENABLE_TEXT_NUMBER
	return hx_os_i_parse_double(NULL, text, bytes, out, error);
#else
	(void)text;
	(void)bytes;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_i_format_double(
	double value, char *buffer, size_t capacity, size_t *required)
{
#if HX_OS_HAVE_LOCAL_PRINTF &&                                                 \
	(HX_OS_ENABLE_TEXT_NUMBER ||                                           \
		(HX_OS_ENABLE_SETTINGS && HX_OS_ENABLE_CONFIG))
	char text[128];
	int length;
#if FLT_RADIX == 2 && DBL_MANT_DIG == 53
	int precision = 17;
#else
	int precision = DECIMAL_DIG;
#endif
#if defined(_WIN32)
	_locale_t locale;
#else
	locale_t locale, previous;
#endif
	if (!required || (!buffer && capacity))
		return HX_OS_INVALID;
	if (value != value)
		return hx_os_i_text_copy(
			"nan", buffer, capacity, required, NULL);
	if (value > DBL_MAX || value < -DBL_MAX)
		return hx_os_i_text_copy(value < 0 ? "-inf" : "inf", buffer,
			capacity, required, NULL);
#if defined(_WIN32)
	locale = _create_locale(LC_NUMERIC, "C");
#else
	locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
#endif
	if (!locale)
		return HX_OS_NO_MEMORY;
#if defined(_WIN32)
	length = _snprintf_l(
		text, sizeof(text), "%.*g", locale, precision, value);
	_free_locale(locale);
#else
	previous = uselocale(locale);
	if (!previous) {
		freelocale(locale);
		return HX_OS_IO_ERROR;
	}
	length = snprintf(text, sizeof(text), "%.*g", precision, value);
	(void)uselocale(previous);
	freelocale(locale);
#endif
	if (length < 0 || (size_t)length >= sizeof(text))
		return HX_OS_RANGE;
	return hx_os_i_text_copy(text, buffer, capacity, required, NULL);
#else
	(void)value;
	(void)buffer;
	(void)capacity;
	(void)required;
	return HX_OS_NOT_SUPPORTED;
#endif
}
hx_os_status hx_os_text_format_double(
	double value, char *buffer, size_t capacity, size_t *required)
{
#if HX_OS_ENABLE_TEXT_NUMBER
	return hx_os_i_format_double(value, buffer, capacity, required);
#else
	(void)value;
	(void)buffer;
	(void)capacity;
	(void)required;
	return HX_OS_NOT_SUPPORTED;
#endif
}
