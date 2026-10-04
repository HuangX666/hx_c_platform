#include "internal.h"
#include "hx_os_wait.h"
#if defined(_WIN32)
#include <windows.h>
#endif
hx_os_status hx_os_sleep_until_precise(
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_sleep_until_precise_ex(deadline, 200000, error);
}

hx_os_status hx_os_sleep_until_precise_ex(
	hx_os_deadline deadline, uint64_t spin_ns, hx_os_error *error)
{
#if HX_OS_ENABLE_PRECISE_WAIT
	uint64_t now;
	hx_os_status status;
#if defined(_WIN32)
	LARGE_INTEGER frequency, started, current;
	uint64_t spin_ticks;
#endif
	if (deadline == HX_OS_DEADLINE_INFINITE || spin_ns > 10000000)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!spin_ns)
		return hx_os_sleep_until(deadline, error);

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;

	if (now >= deadline)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (deadline > now && deadline - now > spin_ns) {
		status = hx_os_sleep_until(deadline - spin_ns, error);
		if (status != HX_OS_OK)
			return status;
	}
#if defined(_WIN32)
	/* The deadline clock is tick-based on the Win7-compatible path.
	 * QPC bounds spinning without changing its epoch/suspend semantics.
	 */
	if (!QueryPerformanceFrequency(&frequency))
		return hx_os_i_win32(error, GetLastError());

	if (frequency.QuadPart <= 0)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	if (!QueryPerformanceCounter(&started))
		return hx_os_i_win32(error, GetLastError());

	spin_ticks =
		(uint64_t)frequency.QuadPart / UINT64_C(1000000000) * spin_ns +
		((uint64_t)frequency.QuadPart % UINT64_C(1000000000) * spin_ns +
			UINT64_C(999999999)) /
			UINT64_C(1000000000);
#endif
	for (;;) {
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (now >= deadline)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
#if defined(_WIN32)
		if (!QueryPerformanceCounter(&current))
			return hx_os_i_win32(error, GetLastError());

		if ((uint64_t)current.QuadPart - (uint64_t)started.QuadPart >=
			spin_ticks)
			return hx_os_sleep_until(deadline, error);
#endif
		hx_os_cpu_relax();
	}
#else
	(void)deadline;
	(void)spin_ns;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
