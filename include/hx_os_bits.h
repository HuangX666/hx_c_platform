#ifndef HX_C_PLATFORM_BITS_H
#define HX_C_PLATFORM_BITS_H
#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif
enum { HX_OS_LITTLE_ENDIAN, HX_OS_BIG_ENDIAN };
static inline uint16_t hx_os_bswap16(uint16_t value)
{
	return (uint16_t)((value >> 8) | (value << 8));
}
static inline uint32_t hx_os_bswap32(uint32_t value)
{
	return (value >> 24) | ((value >> 8) & UINT32_C(0x0000ff00)) |
		((value << 8) & UINT32_C(0x00ff0000)) | (value << 24);
}
static inline uint64_t hx_os_bswap64(uint64_t value)
{
	return ((uint64_t)hx_os_bswap32((uint32_t)value) << 32) |
		hx_os_bswap32((uint32_t)(value >> 32));
}
static inline int hx_os_byte_order(void)
{
	const uint16_t value = 1;
	return *(const unsigned char *)&value ? HX_OS_LITTLE_ENDIAN
					      : HX_OS_BIG_ENDIAN;
}
/* Division by zero or NULL quotient is INVALID, outputs remain unchanged.
 * Remainder is optional; outputs must not overlap. Native C division lets
 * supported 32-bit ARM toolchains select their __aeabi_uldivmod equivalent.
 */
static inline hx_os_status hx_os_udiv64(uint64_t value, uint32_t divisor,
	uint64_t *quotient, uint32_t *remainder)
{
	if (!divisor || !quotient)
		return HX_OS_INVALID;
	*quotient = value / divisor;
	if (remainder)
		*remainder = (uint32_t)(value % divisor);
	return HX_OS_OK;
}
#ifdef __cplusplus
}
#endif
#endif
