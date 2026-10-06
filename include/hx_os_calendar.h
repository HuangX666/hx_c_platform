#ifndef HX_C_PLATFORM_CALENDAR_H
#define HX_C_PLATFORM_CALENDAR_H

#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Unix milliseconds, proleptic Gregorian UTC, years 0001..9999. Fractional
 * seconds are floored, including before 1970. No leap-second representation.
 * Strict UTF-8 spec up to 65536 bytes. Legacy tokens: %CCYY %MM %DD %hh %mm
 * %ss %%. Additional tokens: %YY, %Y/%y, %m/%d, %H/%I, %M/%S, %p, %a/%A,
 * %b/%B, %j, %u/%w, %L (milliseconds), %s (floored Unix seconds), %z/%:z.
 * Weekday/month names and AM/PM are fixed English, independent of locale.
 * %j is 001..366, %u Monday=1..Sunday=7, %w Sunday=0..Saturday=6.
 * %z is +HHMM, %:z +HH:MM (both append seconds for historical offsets).
 * %Z is the unambiguous numeric label UTC or UTC+HH:MM[:SS], not a zone ID.
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

/* Local fields/offset are computed for this instant using the host timezone.
 * Default: %CCYY-%MM-%DDT%hh:%mm:%ss%:z. No TZ/locale changes. The owner must
 * serialize external timezone/TZ changes. Native supported ranges apply:
 * Windows CRT rejects pre-1970 instants and supports years through 3000;
 * POSIX additionally depends on time_t width and installed timezone data.
 * These APIs do not provide an independent IANA database or named-zone lookup.
 * Outputs are preserved on failure; string spec may borrow from out.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_format_local(int64_t ms,
	const char *spec, char *buffer, size_t capacity, size_t *required,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_format_local_string(
	int64_t ms, const char *spec, hx_string *out, hx_os_error *error);

/* Offset east of UTC in seconds for a specified instant; dst is 0/1/-1. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_wall_time_local_offset_at(
	int64_t milliseconds, int *seconds, int *dst, hx_os_error *error);

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

/* Same filename rules, using local civil time. DST overlaps can repeat names;
 * no uniqueness guarantee. The existing filename APIs remain UTC.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_format_local_string(
	int64_t milliseconds, const char *pattern, const char *extension,
	hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_format_local(
	int64_t milliseconds, const char *pattern, const char *extension,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_filename_generate_local(
	const char *extension, hx_string *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
