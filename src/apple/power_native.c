#include "power_private.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#if HX_OS_ENABLE_PERFORMANCE_REQUEST
hx_os_status hx_os_i_activity_set(
	hx_os_i_power *power, int active, hx_os_error *error);
#endif
hx_os_status hx_os_i_power_open(hx_os_i_power *power, hx_os_error *error)
{
	CFStringRef reason = CFStringCreateWithBytes(kCFAllocatorDefault,
		(const UInt8 *)hx_string_c_str(&power->reason),
		(CFIndex)power->reason.size, kCFStringEncodingUTF8, 0);
	if (!reason)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	power->detail = (void *)reason;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_i_power_set(
	hx_os_i_power *power, int active, hx_os_error *error)
{
	IOReturn code;
	if (power->kind == HX_OS_I_PERFORMANCE_REQUEST) {
#if HX_OS_ENABLE_PERFORMANCE_REQUEST
		return hx_os_i_activity_set(power, active, error);
#else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	}
	if (active) {
		IOPMAssertionID identifier = kIOPMNullAssertionID;
		code = IOPMAssertionCreateWithName(
			kIOPMAssertionTypeNoIdleSleep, kIOPMAssertionLevelOn,
			(CFStringRef)power->detail, &identifier);
		if (code == kIOReturnSuccess)
			power->native = identifier;
	}
	else {
		code = IOPMAssertionRelease((IOPMAssertionID)power->native);
		if (code == kIOReturnSuccess)
			power->native = 0;
	}
	return hx_os_i_error(error,
		code == kIOReturnSuccess ? HX_OS_OK : HX_OS_IO_ERROR,
		code == kIOReturnSuccess ? 0 : HX_OS_ERROR_MACH, code);
}
hx_os_status hx_os_i_power_dispose(hx_os_i_power *power, hx_os_error *error)
{
	hx_os_status status = power->active
		? hx_os_i_power_set(power, 0, error)
		: hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (power->detail)
		CFRelease((CFTypeRef)power->detail);
	power->detail = NULL;
	return status;
}
