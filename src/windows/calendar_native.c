#include "internal.h"
#include <time.h>

hx_os_status hx_os_i_local_offset(int *minutes, int *dst, hx_os_error *error)
{
	__time64_t now;
	struct tm local, utc;
	errno_t code;
	int days, seconds;

	errno = 0;
	now = _time64(NULL);
	if (now == -1)
		return hx_os_i_errno(error, errno ? errno : EIO);

	code = _localtime64_s(&local, &now);
	if (!code)
		code = _gmtime64_s(&utc, &now);

	if (code)
		return hx_os_i_errno(error, code);

	if (local.tm_year != utc.tm_year)
		days = local.tm_year > utc.tm_year ? 1 : -1;
	else
		days = local.tm_yday - utc.tm_yday;

	seconds = days * 86400 + (local.tm_hour - utc.tm_hour) * 3600 +
		(local.tm_min - utc.tm_min) * 60 + local.tm_sec - utc.tm_sec;
	if (seconds % 60)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	*minutes = seconds / 60;
	*dst = local.tm_isdst < 0 ? -1 : local.tm_isdst > 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
