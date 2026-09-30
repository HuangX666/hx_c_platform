#ifndef HX_C_PLATFORM_OBFUSCATE_H
#define HX_C_PLATFORM_OBFUSCATE_H
#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed pre-encoded bytes, XOR key 1..255. Reversible obscuring of casual
 * string extraction: the embedded key/data do not protect secrets.
 * C99 data can be generated with tools/obfuscate_string.py. No compiler
 * extension or runtime third-party dependency is required.
 */
typedef struct hx_os_obfuscated {
	const unsigned char *data;
	size_t bytes;
	uint8_t key;
} hx_os_obfuscated;
#define HX_OS_OBFUSCATED_INIT(data, key)                                       \
	{                                                                      \
		data, sizeof(data), key                                        \
	}
/* required includes the appended NUL. Capacity failure preserves buffer;
 * this raw decoding does not validate text. Source/output must be disjoint
 * or identical writable storage. Borrowed views live as long as that buffer.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_obfuscate_decode(
	const hx_os_obfuscated *value, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error);
/* Strict UTF-8 result, transactional, using the output string's context.
 * Temporary plaintext is securely cleared; result lifetime is caller-owned.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_obfuscate_string(
	const hx_os_obfuscated *value, hx_string *out, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
