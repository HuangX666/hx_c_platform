#ifndef HX_C_PLATFORM_CALENDAR_H
#define HX_C_PLATFORM_CALENDAR_H

#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Unix milliseconds, proleptic Gregorian UTC, years 0001..9999. Fractional
 * seconds are floored, including before 1970. No leap-second representation.
 * Strict UTF-8 spec up to 65536 bytes: %CCYY %MM %DD %hh %mm %ss and %%.
 * NULL spec selects "%CCYY-%MM-%DDT%hh:%mm:%ssZ"; unknown tokens are SYNTAX.
 * required includes NUL; insufficient capacity/other failures do not write
 * buffer. Inputs must not overlap buffer. No locale or timezone mutation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_format_utc(int64_t ms,
	const char *spec, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error);

/* Uses out's context; failure preserves out. spec may borrow from out. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_format_utc_string(
	int64_t ms, const char *spec, hx_string *out, hx_os_error *error);

/* Current native local offset east of UTC, in whole minutes. dst is 0/1 or
 * -1 when the native runtime cannot determine it. Host controls timezone;
 * serialize external TZ/timezone changes. Fractional-minute offsets are
 * NOT_SUPPORTED. Outputs unchanged on failure; not a historical zone API.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_local_offset_minutes(
	int *minutes, int *dst, hx_os_error *error);

/* Portable single-component UTF-8 filename using UTC calendar tokens.
 * Default pattern: %CCYY-%MM-%DD_%hh-%mm-%ss. Replaces controls and
 * <>:"/\|?* with _, repairs trailing spaces/dots, prefixes Windows reserved
 * device stems with _. Extension is optional, ASCII alphanumeric/-/_ up to
 * 32 bytes; one leading dot is accepted. Output is capped at 240 UTF-8 bytes.
 * No directory creation, uniqueness guarantee or implicit collision overwrite.
 * Failure preserves outputs; required includes NUL. String uses its context.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_format_string(
	int64_t milliseconds, const char *pattern, const char *extension,
	hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_format(int64_t milliseconds,
	const char *pattern, const char *extension, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_generate(
	const char *extension, hx_string *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
