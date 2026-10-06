#ifndef HX_C_PLATFORM_STDIO_H
#define HX_C_PLATFORM_STDIO_H

#include "hx_os_base.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 paths; borrowed for this call. Content encoding is not converted.
 * Supports r/w/a with optional b and +, in either order. Other modes are
 * INVALID, including CRT-specific ccs= strings. Use b for unchanged bytes.
 * Windows shares the native long-path conversion used by hx_os_file_open.
 * NULL return reports the failure through optional error, not a last-error
 * global. The returned FILE belongs to this library's C runtime. Using
 * caller-side stdio requires a compatible shared CRT (especially on Windows).
 * Different CRT instances must use opaque hx_os_file handles instead.
 */
HX_OS_API FILE *HX_OS_CALL hx_os_fopen_utf8(
	const char *path, const char *mode, hx_os_error *error);

/* Closes through the opening library's CRT. Consumes *stream even on error;
 * fclose may block while flushing. Caller excludes concurrent stdio/close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_fclose(
	FILE **stream, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
