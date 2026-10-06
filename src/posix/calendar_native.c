#include "internal.h"
#include <time.h>

hx_os_status hx_os_i_local_calendar(int64_t seconds, unsigned fields[8],
	int *offset, int *dst, hx_os_error *error)
{
	time_t instant = (time_t)seconds;
	struct tm local;
	hx_os_status status;

	if ((int64_t)instant != seconds)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	errno = 0;
	if (!localtime_r(&instant, &local))
		return hx_os_i_errno(error, errno ? errno : EOVERFLOW);
	if (local.tm_gmtoff < INT_MIN || local.tm_gmtoff > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	status = hx_os_i_calendar_tm_fields(&local, fields, error);
	if (status != HX_OS_OK)
		return status;
	*offset = (int)local.tm_gmtoff;
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
	time_t now;
	int seconds, daylight;
	hx_os_status status;

	errno = 0;
	now = time(NULL);
	if (now == (time_t)-1)
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
