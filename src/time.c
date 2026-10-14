#include "internal.h"
#include "hx_os_wide.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif
#endif

#if defined(_WIN32)
static INIT_ONCE g_counterInit_ = INIT_ONCE_STATIC_INIT;
static uint64_t g_counterFrequency_;

static BOOL CALLBACK hx_os_counter_init(
	PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	LARGE_INTEGER frequency;

	(void)once;
	(void)parameter;
	(void)context;
	if (!QueryPerformanceFrequency(&frequency)) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return FALSE;
	}
	if (frequency.QuadPart <= 0) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return FALSE;
	}
	g_counterFrequency_ = (uint64_t)frequency.QuadPart;
	return TRUE;
}
#endif

hx_os_status hx_os_high_resolution_read(
	uint64_t *nanoseconds, hx_os_error *error)
{
	uint64_t value;

	if (!nanoseconds)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if defined(_WIN32)
	{
		LARGE_INTEGER counter;
		uint64_t whole, fraction;
		hx_os_status status;

		if (!InitOnceExecuteOnce(
			    &g_counterInit_, hx_os_counter_init, NULL, NULL) ||
			!QueryPerformanceCounter(&counter)) {
			DWORD code = GetLastError();

			return hx_os_i_win32(
				error, code ? code : ERROR_GEN_FAILURE);
		}
		if (counter.QuadPart < 0)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		whole = (uint64_t)counter.QuadPart / g_counterFrequency_;
		status = hx_os_i_muldiv_u64(
			(uint64_t)counter.QuadPart % g_counterFrequency_,
			UINT64_C(1000000000), g_counterFrequency_,
			HX_OS_ROUND_DOWN, &fraction, error);
		if (status != HX_OS_OK)
			return status;
		if (whole > (UINT64_MAX - 1 - fraction) / 1000000000)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		value = whole * UINT64_C(1000000000) + fraction;
	}
#elif defined(__APPLE__)
	{
		mach_timebase_info_data_t scale;
		kern_return_t code = mach_timebase_info(&scale);
		hx_os_status status;

		if (code != KERN_SUCCESS || !scale.numer || !scale.denom)
			return hx_os_i_error(
				error, HX_OS_IO_ERROR, HX_OS_ERROR_MACH, code);
		status = hx_os_i_muldiv_u64(mach_absolute_time(), scale.numer,
			scale.denom, HX_OS_ROUND_DOWN, &value, error);
		if (status != HX_OS_OK)
			return status;
	}
#else
	{
		struct timespec stamp;

		if (clock_gettime(CLOCK_MONOTONIC, &stamp))
			return hx_os_i_errno(error, errno);
		if (stamp.tv_sec < 0 || stamp.tv_nsec < 0 ||
			stamp.tv_nsec >= 1000000000 ||
			(uint64_t)stamp.tv_sec >
				(UINT64_MAX - 1 - (uint64_t)stamp.tv_nsec) /
					1000000000)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		value = (uint64_t)stamp.tv_sec * UINT64_C(1000000000) +
			(uint64_t)stamp.tv_nsec;
	}
#endif
	if (value == UINT64_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	*nanoseconds = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

uint64_t hx_os_high_resolution_ns(void)
{
	uint64_t value;

	return hx_os_high_resolution_read(&value, NULL) == HX_OS_OK
		? value
		: UINT64_MAX;
}

hx_os_status hx_os_monotonic_read(uint64_t *nanoseconds, hx_os_error *error)
{
	if (!nanoseconds)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*nanoseconds = 0;
#if defined(_WIN32)
	{
		ULONGLONG ticks = 0;

		if (!QueryUnbiasedInterruptTime(&ticks))
			return hx_os_i_win32(error, GetLastError());

		*nanoseconds = (uint64_t)ticks * 100;
	}
#elif defined(__APPLE__)
	{
		mach_timebase_info_data_t scale;

		uint64_t ticks = mach_absolute_time();

		kern_return_t code = mach_timebase_info(&scale);
		if (code != KERN_SUCCESS || !scale.denom)
			return hx_os_i_error(
				error, HX_OS_IO_ERROR, HX_OS_ERROR_MACH, code);

		*nanoseconds = (ticks / scale.denom) * scale.numer +
			(ticks % scale.denom) * scale.numer / scale.denom;
	}
#else
	{
		struct timespec time_value;

		if (clock_gettime(CLOCK_MONOTONIC, &time_value))
			return hx_os_i_errno(error, errno);

		*nanoseconds =
			(uint64_t)time_value.tv_sec * UINT64_C(1000000000) +
			(uint64_t)time_value.tv_nsec;
	}
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_wall_time_read(int64_t *milliseconds, hx_os_error *error)
{
	if (!milliseconds)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*milliseconds = 0;
#if defined(_WIN32)
	{
		FILETIME time_value;
		ULARGE_INTEGER ticks;

		GetSystemTimeAsFileTime(&time_value);
		ticks.LowPart = time_value.dwLowDateTime;
		ticks.HighPart = time_value.dwHighDateTime;
		*milliseconds = (int64_t)(ticks.QuadPart / 10000) -
			INT64_C(11644473600000);
	}
#else
	{
		struct timespec time_value;
		int64_t fraction;

		if (clock_gettime(CLOCK_REALTIME, &time_value))
			return hx_os_i_errno(error, errno);

		fraction = time_value.tv_nsec / 1000000;
		if ((int64_t)time_value.tv_sec >
				(INT64_MAX - fraction) / 1000 ||
			(int64_t)time_value.tv_sec < INT64_MIN / 1000)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		*milliseconds = (int64_t)time_value.tv_sec * 1000 + fraction;
	}
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

uint64_t hx_os_monotonic_ns(void)
{
	uint64_t value;

	return hx_os_monotonic_read(&value, NULL) == HX_OS_OK ? value
							      : UINT64_MAX;
}

int64_t hx_os_wall_time_ms(void)
{
	int64_t value;

	return hx_os_wall_time_read(&value, NULL) == HX_OS_OK ? value
							      : INT64_MIN;
}

hx_os_deadline hx_os_deadline_after_ms(uint64_t ms)
{
	uint64_t now = hx_os_monotonic_ns();
	if (now == UINT64_MAX)
		return HX_OS_DEADLINE_NOW;

	if (ms > (UINT64_MAX - 1 - now) / 1000000)
		return UINT64_MAX - 1;

	return now + ms * 1000000;
}

int hx_os_deadline_expired(hx_os_deadline deadline)
{
	return deadline != HX_OS_DEADLINE_INFINITE &&
		hx_os_monotonic_ns() >= deadline;
}

uint32_t hx_os_i_wait_ms(hx_os_deadline deadline)
{
	uint64_t now;
	uint64_t remaining;

	if (deadline == HX_OS_DEADLINE_INFINITE)
		return UINT32_MAX;

	now = hx_os_monotonic_ns();
	if (deadline <= now)
		return 0;

	remaining =
		(deadline - now) / 1000000 + ((deadline - now) % 1000000 != 0);

	return remaining > INT32_MAX ? INT32_MAX : (uint32_t)remaining;
}

hx_os_status hx_os_sleep_until(hx_os_deadline deadline, hx_os_error *error)
{
	if (deadline == HX_OS_DEADLINE_INFINITE)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (;;) {
		uint64_t now;

		hx_os_status status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (now >= deadline)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
#if defined(_WIN32)
		Sleep(hx_os_i_wait_ms(deadline));
#else
		struct timespec duration;
		uint64_t left = deadline > now ? deadline - now : 0;

		// Bound the time_t conversion on 32-bit embedded libcs.
		if (left > UINT64_C(3600000000000))
			left = UINT64_C(3600000000000);

		duration.tv_sec = (time_t)(left / 1000000000);
		duration.tv_nsec = (long)(left % 1000000000);
		if (nanosleep(&duration, NULL) && errno != EINTR)
			return hx_os_i_errno(error, errno);
#endif
	}
}
