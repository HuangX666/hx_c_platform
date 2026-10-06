#include "internal.h"
#include "hx_os_calendar.h"
#if HX_OS_ENABLE_CALENDAR
static int hx_os_filename_reserved(const char *text)
{
	char name[5] = {0};
	size_t i = 0;

	while (i < 4 && text[i] && text[i] != '.') {
		unsigned char c = (unsigned char)text[i];

		name[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
		++i;
	}

	if (text[i] && text[i] != '.')
		return 0;

	return !strcmp(name, "CON") || !strcmp(name, "PRN") ||
		!strcmp(name, "AUX") || !strcmp(name, "NUL") ||
		(i == 4 && name[3] >= '1' && name[3] <= '9' &&
			(!memcmp(name, "COM", 3) || !memcmp(name, "LPT", 3)));
}
#endif
static hx_os_status hx_os_filename_string(int64_t milliseconds,
	const char *pattern, const char *extension, hx_string *out, int local,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	hx_string result;
	hx_os_status status;
	size_t i, bytes = 0;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (extension && extension[0] == '.')
		++extension;

	while (extension && extension[bytes]) {
		unsigned char c = (unsigned char)extension[bytes];

		if (bytes == 32)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			    (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		++bytes;
	}

	hx_string_init(&result, out->context);

	if (!pattern)
		pattern = "%CCYY-%MM-%DD_%hh-%mm-%ss";
	status = local ? hx_os_wall_time_format_local_string(
				 milliseconds, pattern, &result, error)
		       : hx_os_wall_time_format_utc_string(
				 milliseconds, pattern, &result, error);
	if (status == HX_OS_OK && !result.size)
		status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (status == HX_OS_OK) {
		for (i = 0; i < result.size; ++i) {
			unsigned char c = (unsigned char)result.data[i];

			if (c < 0x20 || c == 0x7f || strchr("<>:\"/\\|?*", c))
				result.data[i] = '_';
		}

		for (i = result.size; i &&
			(result.data[i - 1] == ' ' ||
				result.data[i - 1] == '.');
			--i)
			result.data[i - 1] = '_';
		if (hx_os_filename_reserved(hx_string_c_str(&result)))
			status = hx_string_insert(&result, 0,
				hx_string_view_from_cstr("_"), error);
	}

	if (status == HX_OS_OK && bytes)
		status = hx_string_append_cstr(&result, ".", error);

	if (status == HX_OS_OK && bytes)
		status = hx_string_append_cstr(&result, extension, error);

	if (status == HX_OS_OK && result.size > 240)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (status == HX_OS_OK)
		hx_string_move(out, &result);

	hx_string_destroy(&result);

	return status;
#else
	(void)milliseconds;
	(void)local;
	(void)pattern;
	(void)extension;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_filename_buffer(int64_t milliseconds,
	const char *pattern, const char *extension, char *buffer,
	size_t capacity, size_t *required, int local, hx_os_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_filename_string(
		milliseconds, pattern, extension, &text, local, error);
	if (status == HX_OS_OK) {
		*required = text.size + 1;
		if (capacity < *required)
			status = hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
		else
			memcpy(buffer, hx_string_c_str(&text), *required);
	}

	hx_string_destroy(&text);

	return status;
}

hx_os_status hx_os_filename_format_string(int64_t ms, const char *pattern,
	const char *extension, hx_string *out, hx_os_error *error)
{
	return hx_os_filename_string(ms, pattern, extension, out, 0, error);
}

hx_os_status hx_os_filename_format_local_string(int64_t ms, const char *pattern,
	const char *extension, hx_string *out, hx_os_error *error)
{
	return hx_os_filename_string(ms, pattern, extension, out, 1, error);
}

hx_os_status hx_os_filename_format(int64_t ms, const char *pattern,
	const char *extension, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error)
{
	return hx_os_filename_buffer(
		ms, pattern, extension, buffer, capacity, required, 0, error);
}

hx_os_status hx_os_filename_format_local(int64_t ms, const char *pattern,
	const char *extension, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error)
{
	return hx_os_filename_buffer(
		ms, pattern, extension, buffer, capacity, required, 1, error);
}

hx_os_status hx_os_filename_generate_local(
	const char *extension, hx_string *out, hx_os_error *error)
{
	int64_t ms;
	hx_os_status status = hx_os_wall_time_read(&ms, error);

	return status == HX_OS_OK ? hx_os_filename_format_local_string(
					    ms, NULL, extension, out, error)
				  : status;
}

hx_os_status hx_os_filename_generate(
	const char *extension, hx_string *out, hx_os_error *error)
{
	int64_t milliseconds;

	hx_os_status status = hx_os_wall_time_read(&milliseconds, error);

	return status == HX_OS_OK ? hx_os_filename_format_string(milliseconds,
					    NULL, extension, out, error)
				  : status;
}
