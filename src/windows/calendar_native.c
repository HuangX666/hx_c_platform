#include "internal.h"
#include <time.h>

hx_os_status hx_os_i_local_calendar(int64_t seconds, unsigned fields[8],
	int *offset, int *dst, hx_os_error *error)
{
	__time64_t instant = (__time64_t)seconds;
	struct tm local, utc;
	hx_os_status status;
	errno_t code;
	int days, difference;

	// Guard CRT range failures before entering its parameter validation.
	if (seconds < 0 || seconds > INT64_C(32535215999))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	code = _localtime64_s(&local, &instant);
	if (!code)
		code = _gmtime64_s(&utc, &instant);
	if (code)
		return hx_os_i_errno(error, code);

	days = local.tm_yday - utc.tm_yday;
	if (local.tm_year != utc.tm_year) {
		int earlier = local.tm_year < utc.tm_year ? local.tm_year
							  : utc.tm_year;
		int year = earlier + 1900;
		int leap =
			year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);

		days += local.tm_year > utc.tm_year ? 365 + leap : -365 - leap;
	}
	difference = days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 +
		(local.tm_min - utc.tm_min) * 60 + local.tm_sec - utc.tm_sec;
	status = hx_os_i_calendar_tm_fields(&local, fields, error);
	if (status != HX_OS_OK)
		return status;
	*offset = difference;
	*dst = local.tm_isdst < 0 ? -1 : local.tm_isdst > 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_local_offset_at(
	int64_t seconds, int *offset, int *dst, hx_os_error *error)
{
	return hx_os_i_local_calendar(seconds, NULL, offset, dst, error);
}

hx_os_status hx_os_i_local_offset(int *minutes, int *dst, hx_os_error *error)
{
	__time64_t now;
	int seconds, daylight;
	hx_os_status status;

	errno = 0;
	now = _time64(NULL);
	if (now == -1)
		return hx_os_i_errno(error, errno ? errno : EIO);
	status = hx_os_i_local_offset_at(
		(int64_t)now, &seconds, &daylight, error);
	if (status != HX_OS_OK)
		return status;
	if (seconds % 60)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	*minutes = seconds / 60;
	*dst = daylight;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
