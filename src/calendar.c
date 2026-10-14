#include "internal.h"
#include "hx_os_calendar.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#if HX_OS_ENABLE_CALENDAR
hx_os_status hx_os_i_local_offset(int *minutes, int *dst, hx_os_error *error);
hx_os_status hx_os_i_local_offset_at(
	int64_t seconds, int *offset, int *dst, hx_os_error *error);
hx_os_status hx_os_i_local_calendar(int64_t seconds, unsigned fields[8],
	int *offset, int *dst, hx_os_error *error);

hx_os_status hx_os_i_calendar_tm_fields(
	const struct tm *local, unsigned fields[8], hx_os_error *error)
{
	if (!fields)
		return HX_OS_OK;
	if (local->tm_year < -1899 || local->tm_year > 8099)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (local->tm_sec < 0 || local->tm_sec > 59)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	fields[0] = (unsigned)(local->tm_year + 1900);
	fields[1] = (unsigned)(local->tm_mon + 1);
	fields[2] = (unsigned)local->tm_mday;
	fields[3] = (unsigned)local->tm_hour;
	fields[4] = (unsigned)local->tm_min;
	fields[5] = (unsigned)local->tm_sec;
	fields[6] = (unsigned)local->tm_wday;
	fields[7] = (unsigned)(local->tm_yday + 1);

	return HX_OS_OK;
}

static int hx_os_days_before_year(int year)
{
	int prior = year - 1;

	return 365 * prior + prior / 4 - prior / 100 + prior / 400;
}

static hx_os_status hx_os_calendar_fields(
	int64_t ms, unsigned fields[9], hx_os_error *error)
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

	fields[6] = (unsigned)((days + 4) % 7 + 7) % 7;
	year = low;
	serial -= hx_os_days_before_year(year);
	fields[7] = (unsigned)serial + 1;
	fields[8] = (unsigned)((ms % 1000 + 1000) % 1000);
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

static int hx_os_calendar_offset(char *text, int offset, int colon, int label)
{
	int64_t magnitude = offset;
	unsigned hours, minutes, seconds;
	const char *separator = colon ? ":" : "";
	const char *prefix = label ? "UTC" : "";
	char sign = offset < 0 ? '-' : '+';

	if (label && !offset) {
		memcpy(text, "UTC", 4);
		return 3;
	}
	if (magnitude < 0)
		magnitude = -magnitude;
	hours = (unsigned)(magnitude / 3600);
	minutes = (unsigned)(magnitude / 60 % 60);
	seconds = (unsigned)(magnitude % 60);
	if (seconds)
		return snprintf(text, 32, "%s%c%02u%s%02u%s%02u", prefix, sign,
			hours, separator, minutes, separator, seconds);

	return snprintf(text, 32, "%s%c%02u%s%02u", prefix, sign, hours,
		separator, minutes);
}

static hx_os_status hx_os_calendar_piece(const char *spec,
	const unsigned fields[9], int offset, int64_t epoch, char buffer[32],
	const char **text, size_t *length, size_t *consumed, int render,
	hx_os_error *error)
{
	static const char *const weekdays[] = {"Sunday", "Monday", "Tuesday",
		"Wednesday", "Thursday", "Friday", "Saturday"};
	static const char *const months[] = {"January", "February", "March",
		"April", "May", "June", "July", "August", "September",
		"October", "November", "December"};
	static const char *const legacy[] = {
		"CCYY", "MM", "DD", "hh", "mm", "ss"};
	unsigned width = 2, value = 0, i;
	int count;

	*text = buffer;
	for (i = 0; i < 6; ++i) {
		size_t size = i ? 2 : 4;

		if (!strncmp(spec, legacy[i], size)) {
			*consumed = size;
			value = fields[i];
			width = (unsigned)size;
			goto numeric;
		}
	}
	*consumed = 1;
	switch (*spec) {
	case 'Y':
		if (spec[1] == 'Y') {
			*consumed = 2;
			value = fields[0] % 100;
		}
		else {
			value = fields[0];
			width = 4;
		}
		break;
	case 'y':
		value = fields[0] % 100;
		break;
	case 'm':
		value = fields[1];
		break;
	case 'd':
		value = fields[2];
		break;
	case 'H':
		value = fields[3];
		break;
	case 'I':
		value = fields[3] % 12;
		if (!value)
			value = 12;
		break;
	case 'M':
		value = fields[4];
		break;
	case 'S':
		value = fields[5];
		break;
	case 'L':
		value = fields[8];
		width = 3;
		break;
	case 'j':
		value = fields[7];
		width = 3;
		break;
	case 'u':
		value = fields[6] ? fields[6] : 7;
		width = 1;
		break;
	case 'w':
		value = fields[6];
		width = 1;
		break;
	case 'p':
		*text = fields[3] < 12 ? "AM" : "PM";
		*length = 2;
		return HX_OS_OK;
	case 'a':
	case 'A':
		*text = weekdays[fields[6]];
		*length = *spec == 'a' ? 3 : strlen(*text);
		return HX_OS_OK;
	case 'b':
	case 'B':
		*text = months[fields[1] - 1];
		*length = *spec == 'b' ? 3 : strlen(*text);
		return HX_OS_OK;
	case 's':
		count = snprintf(buffer, 32, "%" PRId64, epoch);
		goto formatted;
	case ':':
		if (spec[1] != 'z')
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		*consumed = 2;
		count = hx_os_calendar_offset(buffer, offset, 1, 0);
		goto formatted;
	case 'z':
	case 'Z':
		count = hx_os_calendar_offset(
			buffer, offset, *spec == 'Z', *spec == 'Z');
		goto formatted;
	default:
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	}

numeric:
	// Keep fixed-width calendar fields off the CRT formatting path.
	if (render) {
		unsigned position = width;

		buffer[width] = 0;
		while (position) {
			buffer[--position] = (char)('0' + value % 10);
			value /= 10;
		}
	}
	count = (int)width;
formatted:
	if (count < 0 || count >= 32)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*length = (size_t)count;

	return HX_OS_OK;
}

static hx_os_status hx_os_calendar_render(const char *spec,
	const unsigned fields[9], int offset, int64_t epoch, char *out,
	size_t *bytes, hx_os_error *error)
{
	size_t used = 0;

	while (*spec) {
		const char *text = spec;
		size_t length = 1, consumed = 1;
		char token[32];
		hx_os_status status;

		if (*spec == '%' && spec[1] != '%') {
			status = hx_os_calendar_piece(spec + 1, fields, offset,
				epoch, token, &text, &length, &consumed,
				out != NULL, error);
			if (status != HX_OS_OK)
				return status;
			++consumed;
		}
		else if (*spec == '%')
			consumed = 2;
		if (length > 1024 * 1024 - used)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		if (out)
			memcpy(out + used, text, length);
		used += length;
		spec += consumed;
	}
	if (out)
		out[used] = 0;
	*bytes = used + 1;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif
static hx_os_status hx_os_calendar_format(int64_t ms, const char *spec,
	char *buffer, size_t capacity, size_t *required, int local,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	unsigned fields[9];
	size_t bytes = 0, invalid, result;
	int offset = 0, dst;
	int64_t epoch = ms / 1000 - (ms % 1000 < 0);
	hx_os_status status;

	if (!required || (!buffer && capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!spec)
		spec = local ? "%CCYY-%MM-%DDT%hh:%mm:%ss%:z"
			     : "%CCYY-%MM-%DDT%hh:%mm:%ssZ";

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

	if (local) {
		status = hx_os_i_local_calendar(
			epoch, fields, &offset, &dst, error);
		if (status != HX_OS_OK)
			return status;
	}
	status = hx_os_calendar_render(
		spec, fields, offset, epoch, NULL, &result, error);
	if (status != HX_OS_OK)
		return status;

	*required = result;
	if (capacity < result)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	return hx_os_calendar_render(
		spec, fields, offset, epoch, buffer, &result, error);
#else
	(void)ms;
	(void)local;
	(void)spec;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_calendar_string(int64_t ms, const char *spec,
	hx_string *out, int local, hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	char *buffer;
	size_t required;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_calendar_format(
		ms, spec, NULL, 0, &required, local, error);
	if (status != HX_OS_OUTPUT_FULL)
		return status;

	buffer = hx_os_i_alloc(out->context, required, 0);
	if (!buffer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_calendar_format(
		ms, spec, buffer, required, &required, local, error);
	if (status == HX_OS_OK)
		status = hx_string_assign_cstr(out, buffer, error);

	hx_os_memory_free(buffer);

	return status;
#else
	(void)ms;
	(void)local;
	(void)spec;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_wall_time_format_utc(int64_t ms, const char *spec,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	return hx_os_calendar_format(
		ms, spec, buffer, capacity, required, 0, error);
}

hx_os_status hx_os_wall_time_format_local(int64_t ms, const char *spec,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error)
{
	return hx_os_calendar_format(
		ms, spec, buffer, capacity, required, 1, error);
}

hx_os_status hx_os_wall_time_format_utc_string(
	int64_t ms, const char *spec, hx_string *out, hx_os_error *error)
{
	return hx_os_calendar_string(ms, spec, out, 0, error);
}

hx_os_status hx_os_wall_time_format_local_string(
	int64_t ms, const char *spec, hx_string *out, hx_os_error *error)
{
	return hx_os_calendar_string(ms, spec, out, 1, error);
}

hx_os_status hx_os_wall_time_local_offset_at(
	int64_t ms, int *seconds, int *dst, hx_os_error *error)
{
#if HX_OS_ENABLE_CALENDAR
	unsigned fields[9];
	hx_os_status status;

	if (!seconds || !dst)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_calendar_fields(ms, fields, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_i_local_offset_at(
		ms / 1000 - (ms % 1000 < 0), seconds, dst, error);
#else
	(void)ms;
	(void)seconds;
	(void)dst;
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
