#include "power_private.h"
#include <windows.h>
static POWER_REQUEST_TYPE hx_os_power_kind(const hx_os_i_power *power)
{
	/* Execution-required (3) is unsupported before Windows 8. Keeping the
	 * enum value local allows the public baseline to remain Windows 7.
	 */
	return power->kind == HX_OS_I_SLEEP_REQUEST ? PowerRequestSystemRequired
						    : (POWER_REQUEST_TYPE)3;
}
hx_os_status hx_os_i_power_open(hx_os_i_power *power, hx_os_error *error)
{
	hx_os_error local;
	REASON_CONTEXT reason;
	HANDLE native;
	if (!error)
		error = &local;
	power->detail = hx_os_i_windows_utf16(
		power->reason.context, hx_string_c_str(&power->reason), error);
	if (!power->detail)
		return error->status;
	memset(&reason, 0, sizeof(reason));
	reason.Version = POWER_REQUEST_CONTEXT_VERSION;
	reason.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
	reason.Reason.SimpleReasonString = power->detail;
	native = PowerCreateRequest(&reason);
	if (native == INVALID_HANDLE_VALUE) {
		DWORD code = GetLastError();
		hx_os_memory_free(power->detail);
		power->detail = NULL;
		return hx_os_i_win32(error, code);
	}
	power->native = (uintptr_t)native;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_i_power_set(
	hx_os_i_power *power, int active, hx_os_error *error)
{
	BOOL result = active ? PowerSetRequest((HANDLE)power->native,
				       hx_os_power_kind(power))
			     : PowerClearRequest((HANDLE)power->native,
				       hx_os_power_kind(power));
	if (!result) {
		DWORD code = GetLastError();
		if (power->kind == HX_OS_I_PERFORMANCE_REQUEST &&
			(code == ERROR_INVALID_PARAMETER ||
				code == ERROR_NOT_SUPPORTED))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED,
				HX_OS_ERROR_WIN32, code);
		return hx_os_i_win32(error, code);
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_i_power_dispose(hx_os_i_power *power, hx_os_error *error)
{
	hx_os_error first;
	hx_os_status status = HX_OS_OK;
	if (power->active)
		status = hx_os_i_power_set(power, 0, &first);
	if (!CloseHandle((HANDLE)power->native)) {
		DWORD code = GetLastError();
		if (status == HX_OS_OK)
			status = hx_os_i_win32(&first, code);
		else
			first.cause_code = code;
	}
	hx_os_memory_free(power->detail);
	power->detail = NULL;
	power->native = 0;
	if (status == HX_OS_OK)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (error)
		*error = first;
	return status;
}
