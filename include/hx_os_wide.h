#ifndef HX_C_PLATFORM_WIDE_H
#define HX_C_PLATFORM_WIDE_H
#include "hx_os_base.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_uint128 {
	uint64_t low;
	uint64_t high;
} hx_os_uint128;
enum { HX_OS_ROUND_DOWN, HX_OS_ROUND_UP, HX_OS_ROUND_NEAREST };
/* Unsigned exact multiplication/division, no allocation or native state.
 * Pointers are caller-owned and must not overlap. On error outputs remain
 * unchanged. A zero divisor/NULL required output is INVALID.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_u128_multiply(
	uint64_t a, uint64_t b, hx_os_uint128 *out);
HX_OS_API hx_os_status HX_OS_CALL hx_os_u128_divide(hx_os_uint128 value,
	uint64_t divisor, hx_os_uint128 *quotient, uint64_t *remainder);
/* Calculates a*b/divisor using the full product. Nearest ties round upward.
 * A rounded result exceeding UINT64_MAX is RANGE, never saturation/truncation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_muldiv_u64(uint64_t a, uint64_t b,
	uint64_t divisor, int rounding, uint64_t *out, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
