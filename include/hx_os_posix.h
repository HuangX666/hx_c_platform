#ifndef HX_C_PLATFORM_POSIX_H
#define HX_C_PLATFORM_POSIX_H
#include "hx_os_base.h"

struct timespec;
#ifdef __cplusplus
extern "C" {
#endif
/* POSIX extensions: include native time.h with the appropriate feature
 * macros before constructing timespec storage. Windows returns NOT_SUPPORTED.
 * Conversion preserves output on failure; no representation of infinity.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_posix_duration_to_timespec(
	uint64_t nanoseconds, struct timespec *out, hx_os_error *error);
/*
 * Explicit native clock domain: CLOCK_MONOTONIC or CLOCK_REALTIME only.
 * Samples both clocks; never treats the library epoch as a native epoch.
 * This is a snapshot, not a wall-jump/suspend-proof wait. External native
 * waits must recheck the library deadline after waking and convert again.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_posix_deadline_to_timespec(
	hx_os_deadline deadline, int native_clock, struct timespec *out,
	hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
