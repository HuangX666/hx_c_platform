#ifndef HX_C_PLATFORM_UTIL_H
#define HX_C_PLATFORM_UTIL_H

#include "hx_os_base.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * System cryptographic RNG, no initialization or weak fallback. May block
 * for OS entropy; no deadline or cancellation guarantee. NULL is valid only
 * for zero bytes. A native failure clears the entire supplied buffer.
 * Invalid/disabled calls do not touch it. Does not use an injected allocator.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_random_bytes(
	void *buffer, size_t bytes, hx_os_error *error);

/* RFC 9562 UUID v4. Failure preserves out; no uniqueness guarantee. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_generate(
	unsigned char out[16], hx_os_error *error);

/* Lowercase 8-4-4-4-12 plus NUL, 37 bytes. Inputs may overlap. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_format(
	const unsigned char uuid[16], char out[37]);

/* Generate directly into the string's own context; failure preserves it. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_generate_string(
	hx_string *out, hx_os_error *error);

/* Exact 36-byte 8-4-4-4-12 text, either hexadecimal case. No braces, URI
 * prefix or whitespace. Any version/variant, including nil UUID, is accepted.
 * Failure preserves out. Input may overlap out.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_parse(hx_string_view text,
	unsigned char out[16], hx_os_error *error);

/* RFC 9562 v7 with a 48-bit Unix millisecond timestamp and random payload.
 * Stateless: values in one millisecond are not ordered, clocks can go
 * backwards and uniqueness is probabilistic. No global event-order promise.
 * generate_at rejects milliseconds >= 2^48. Requires RANDOM; failures keep
 * outputs unchanged. String output uses its own initialized context.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_v7_generate_at(
	uint64_t milliseconds, unsigned char out[16], hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_v7_generate(
	unsigned char out[16], hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_v7_generate_string(
	hx_string *out, hx_os_error *error);
/* Rejects non-v7 or non-RFC-variant values with TYPE_MISMATCH. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_uuid_v7_time(
	const unsigned char uuid[16], uint64_t *milliseconds,
	hx_os_error *error);

/*
 * CRC-32/ISO-HDLC, reflected polynomial 0xedb88320; initial/final xor all
 * ones. Start with seed=0; pass a prior result to continue. Not a MAC/hash.
 * data must address bytes readable bytes; NULL is valid for bytes=0.
 */
HX_OS_API uint32_t HX_OS_CALL hx_os_crc32(
	uint32_t seed, const void *data, size_t bytes);

/* Prevents removal of these writes, not erasure of copies/registers/swaps.
 * buffer must address bytes writable bytes, or may be NULL for zero bytes.
 * This is not synchronization; callers exclude concurrent access.
 */
HX_OS_API void HX_OS_CALL hx_os_secure_zero(void *buffer, size_t bytes);

/* Explicit debugger breakpoint, active in Release too. Windows DebugBreak;
 * POSIX SIGTRAP on the calling thread. Execution may resume under a debugger
 * or application signal handler; without one the OS can terminate the process.
 * Installs no handler and is never called automatically by the library.
 */
HX_OS_API void HX_OS_CALL hx_os_debug_break(void);

#ifdef __cplusplus
}
#endif
#endif
