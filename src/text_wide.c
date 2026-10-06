#include "internal.h"

static hx_os_encoding hx_os_wide_encoding(void)
{
	const uint16_t value = 1;
	int little = *(const unsigned char *)&value;

	if (sizeof(wchar_t) == 2)
		return little ? HX_OS_UTF16LE : HX_OS_UTF16BE;

	return little ? HX_OS_UTF32LE : HX_OS_UTF32BE;
}

static hx_os_status hx_os_wide_convert(hx_os_encoding source,
	hx_os_encoding target, const void *input, size_t bytes, void *output,
	size_t capacity, size_t unit, size_t *required, hx_os_error *error)
{
	hx_os_text_result result;
	hx_os_status status;
	size_t needed = 0;

	if (!required)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*required = 0;
	if ((!input && bytes) || (!output && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (sizeof(wchar_t) != 2 && sizeof(wchar_t) != 4)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_text_measure(
		source, target, input, bytes, &needed, error);
	if (status != HX_OS_OK)
		return status;
	if (needed > SIZE_MAX - unit)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*required = needed / unit + 1;
	if (capacity < *required)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	status = hx_os_text_convert(
		source, target, input, bytes, output, needed, &result, error);
	if (status == HX_OS_OK)
		memset((unsigned char *)output + needed, 0, unit);

	return status;
}

hx_os_status hx_os_text_to_wide(const char *input, size_t bytes,
	wchar_t *output, size_t capacity, size_t *required, hx_os_error *error)
{
	return hx_os_wide_convert(HX_OS_UTF8, hx_os_wide_encoding(), input,
		bytes, output, capacity, sizeof(wchar_t), required, error);
}

hx_os_status hx_os_text_from_wide(const wchar_t *input, size_t units,
	char *output, size_t capacity, size_t *required, hx_os_error *error)
{
	if (units > SIZE_MAX / sizeof(wchar_t)) {
		if (required)
			*required = 0;

		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	}

	return hx_os_wide_convert(hx_os_wide_encoding(), HX_OS_UTF8, input,
		units * sizeof(wchar_t), output, capacity, 1, required, error);
}

hx_os_status hx_string_assign_wide(hx_string *string, const wchar_t *input,
	size_t units, hx_os_error *error)
{
	if (!string || (!input && units))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (sizeof(wchar_t) != 2 && sizeof(wchar_t) != 4)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (units > SIZE_MAX / sizeof(wchar_t))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	return hx_string_assign_encoded(string, hx_os_wide_encoding(), input,
		units * sizeof(wchar_t), error);
}
