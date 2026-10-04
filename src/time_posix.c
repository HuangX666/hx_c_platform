#include "internal.h"
#include "hx_os_posix.h"
#if !defined(_WIN32)
#include <time.h>
#endif

hx_os_status hx_os_posix_duration_to_timespec(
	uint64_t nanoseconds, struct timespec *out, hx_os_error *error)
{
#if !defined(_WIN32)
	struct timespec value;

	uint64_t seconds = nanoseconds / UINT64_C(1000000000);
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	value.tv_sec = (time_t)seconds;
	if (value.tv_sec < 0 || (uint64_t)value.tv_sec != seconds)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	value.tv_nsec = (long)(nanoseconds % UINT64_C(1000000000));
	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)nanoseconds;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_posix_deadline_to_timespec(hx_os_deadline deadline,
	int native_clock, struct timespec *out, hx_os_error *error)
{
#if !defined(_WIN32)
	struct timespec now, duration, result;
	uint64_t current, seconds, carry;
	hx_os_status status;

	if (!out ||
		(native_clock != CLOCK_MONOTONIC &&
			native_clock != CLOCK_REALTIME))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (deadline == HX_OS_DEADLINE_INFINITE)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_monotonic_read(&current, error);
	if (status != HX_OS_OK)
		return status;

	if (clock_gettime((clockid_t)native_clock, &now))
		return hx_os_i_errno(error, errno);

	status = hx_os_posix_duration_to_timespec(
		deadline > current ? deadline - current : 0, &duration, error);
	if (status != HX_OS_OK)
		return status;

	if (now.tv_sec < 0)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	carry = now.tv_nsec + duration.tv_nsec >= 1000000000L ? 1 : 0;
	seconds = (uint64_t)now.tv_sec;
	if (seconds > UINT64_MAX - (uint64_t)duration.tv_sec - carry)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	seconds += (uint64_t)duration.tv_sec + carry;
	result.tv_sec = (time_t)seconds;
	if (result.tv_sec < 0 || (uint64_t)result.tv_sec != seconds)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	result.tv_nsec = (now.tv_nsec + duration.tv_nsec) % 1000000000L;
	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)deadline;
	(void)native_clock;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
