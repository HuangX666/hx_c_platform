#include "internal.h"
#include <time.h>

hx_os_status hx_os_i_local_offset(int *minutes, int *dst, hx_os_error *error)
{
	time_t now;
	struct tm local;

	errno = 0;
	now = time(NULL);
	if (now == (time_t)-1)
		return hx_os_i_errno(error, errno ? errno : EIO);

	if (!localtime_r(&now, &local))
		return hx_os_i_errno(error, errno ? errno : EOVERFLOW);

	if (local.tm_gmtoff % 60)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	if (local.tm_gmtoff / 60 < INT_MIN || local.tm_gmtoff / 60 > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*minutes = (int)(local.tm_gmtoff / 60);
	*dst = local.tm_isdst < 0 ? -1 : local.tm_isdst > 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
