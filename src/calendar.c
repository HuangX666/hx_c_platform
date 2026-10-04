#include "internal.h"
#include "hx_os_calendar.h"
#if HX_OS_ENABLE_CALENDAR
hx_os_status hx_os_i_local_offset(int *minutes, int *dst, hx_os_error *error);

static int hx_os_days_before_year(int year)
{
	int prior = year - 1;

	return 365 * prior + prior / 4 - prior / 100 + prior / 400;
}

static hx_os_status hx_os_calendar_fields(
	int64_t ms, unsigned fields[6], hx_os_error *error)
{
	static const unsigned months[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int64_t seconds, days, remainder;
	int low = 1, high = 10000, serial, year, month, leap;

	if (ms < -INT64_C(62135596800000) || ms > INT64_C(253402300799999))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	seconds = ms / 1000 - (ms % 1000 < 0);
	days = seconds / 86400;
	remainder = seconds % 86400;
	if (remainder < 0) {
		--days;
		remainder += 86400;
	}

	serial = (int)(days + 719162);

	while (low + 1 < high) {
		int middle = low + (high - low) / 2;

		if (hx_os_days_before_year(middle) <= serial)
			low = middle;
		else
			high = middle;
	}

	year = low;
	serial -= hx_os_days_before_year(year);
	leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);

	for (month = 0; month < 11; ++month) {
		int length = (int)months[month] + (month == 1 && leap);

		if (serial < length)
			break;

		serial -= length;
	}

	fields[0] = (unsigned)year;
	fields[1] = (unsigned)month + 1;
	fields[2] = (unsigned)serial + 1;
	fields[3] = (unsigned)(remainder / 3600);
	fields[4] = (unsigned)(remainder / 60 % 60);
	fields[5] = (unsigned)(remainder % 60);

	return HX_OS_OK;
}

static hx_os_status hx_os_calendar_render(const char *spec,
	const unsigned fields[6], char *out, size_t *bytes, hx_os_error *error)
{
	static const char *const tokens[] = {
		"CCYY", "MM", "DD", "hh", "mm", "ss"};
	size_t used = 0;

	while (*spec) {
		if (*spec != '%' || spec[1] == '%') {
			if (*spec == '%')
				++spec;

			if (out)
				out[used] = *spec;

			++used;
			++spec;
		}
		else {
			unsigned i, width, value;

			++spec;

			for (i = 0; i < 6; ++i)
				if (!strncmp(spec, tokens[i], i == 0 ? 4 : 2))
					break;

			if (i == 6)
				return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

			width = i == 0 ? 4 : 2;
			spec += width;
			value = fields[i];
			if (out) {
				unsigned j;

				for (j = width; j; --j) {
					out[used + j - 1] =
						(char)('0' + value % 10);
					value /= 10;
				}
			}

			used += width;
		}
	}

	if (out)
		out[used] = 0;

	*bytes = used + 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif
hx_os_status hx_os_wall_time_format_utc(int64_t ms, const char *spec,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	unsigned fields[6];
	size_t bytes = 0, invalid, result;
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!spec)
		spec = "%CCYY-%MM-%DDT%hh:%mm:%ssZ";

	while (bytes <= 65536 && spec[bytes])
		++bytes;

	if (bytes > 65536)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_text_validate_utf8(spec, bytes, &invalid);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_calendar_fields(ms, fields, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_calendar_render(spec, fields, NULL, &result, error);
	if (status != HX_OS_OK)
		return status;

	*required = result;
	if (capacity < result)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_calendar_render(spec, fields, buffer, &result, error);
#else
	(void)ms;
	(void)spec;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_wall_time_format_utc_string(
	int64_t ms, const char *spec, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	char *buffer;
	size_t required;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status =
		hx_os_wall_time_format_utc(ms, spec, NULL, 0, &required, error);
	if (status != HX_OS_OUTPUT_FULL)
		return status;

	buffer = hx_os_i_alloc(out->context, required, 0);
	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_wall_time_format_utc(
		ms, spec, buffer, required, &required, error);
	if (status == HX_OS_OK)
		status = hx_string_assign_cstr(out, buffer, error);

	hx_os_memory_free(buffer);

	return status;
#else
	(void)ms;
	(void)spec;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_wall_time_local_offset_minutes(
	int *minutes, int *dst, hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	if (!minutes || !dst)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_local_offset(minutes, dst, error);
#else
	(void)minutes;
	(void)dst;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
