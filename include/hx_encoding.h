#ifndef HX_C_PLATFORM_ENCODING_H
#define HX_C_PLATFORM_ENCODING_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_OS_BASE64_URL = 1,
	HX_OS_BASE64_NO_PADDING = 2,
	HX_OS_HEX_UPPERCASE = 1
};

HX_OS_API int HX_OS_CALL hx_os_encoding_available(void);

/**
 * \brief Strict byte/text encoding, independent of OpenSSL and locale.
 *
 * \details Encode required includes the trailing NUL; decode required is binary
 * bytes. Query with output=NULL/capacity=0; nonempty required returns
 * OUTPUT_FULL. After valid input, required is written on OK/OUTPUT_FULL. Other
 * failures preserve required. Every failure preserves output.
 * Input/output/required storage must not overlap. Base64 defaults to standard
 * alphabet with required padding where needed. URL selects -_ instead of +/.
 * NO_PADDING omits/rejects '='; never accepts whitespace, mixed alphabets or
 * nonzero unused tail bits. No line wrapping. Hex defaults to lowercase output;
 * decode accepts either case, no spaces, separators or 0x prefix. Odd digit
 * count is INVALID_ENCODING.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_base64_encode(const void *input,
	size_t bytes, uint32_t flags, char *output, size_t capacity,
	size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_base64_decode(const char *input,
	size_t bytes, uint32_t flags, void *output, size_t capacity,
	size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_hex_encode(const void *input,
	size_t bytes, uint32_t flags, char *output, size_t capacity,
	size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_hex_decode(const char *input,
	size_t bytes, void *output, size_t capacity, size_t *required,
	hx_os_error *error);

/**
 * \brief Transactional owning text using out's context; encoded payload <=16
 * MiB.
 *
 * \details Input may borrow from out. Buffer APIs above have no implicit size
 * limit.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_base64_encode_string(const void *input,
	size_t bytes, uint32_t flags, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_hex_encode_string(const void *input,
	size_t bytes, uint32_t flags, hx_string *out, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
