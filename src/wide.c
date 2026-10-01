#include "internal.h"
#include "hx_os_wide.h"
hx_os_status hx_os_u128_multiply(uint64_t a, uint64_t b, hx_os_uint128 *out)
{
#if HX_OS_ENABLE_WIDE_MATH
	hx_os_uint128 product;
	if (!out)
		return HX_OS_INVALID;
#if defined(__SIZEOF_INT128__) && !defined(HX_OS_TEST_PORTABLE_WIDE)
	{
		__uint128_t value = (__uint128_t)a * b;
		product.low = (uint64_t)value;
		product.high = (uint64_t)(value >> 64);
	}
#else
	{
		uint64_t al = (uint32_t)a, ah = a >> 32;
		uint64_t bl = (uint32_t)b, bh = b >> 32;
		uint64_t first = al * bl;
		uint64_t second = ah * bl + (first >> 32);
		uint64_t third = al * bh + (uint32_t)second;
		product.low = (third << 32) | (uint32_t)first;
		product.high = ah * bh + (second >> 32) + (third >> 32);
	}
#endif
	*out = product;
	return HX_OS_OK;
#else
	(void)a;
	(void)b;
	(void)out;
	return HX_OS_NOT_SUPPORTED;
#endif
}
hx_os_status hx_os_u128_divide(hx_os_uint128 value, uint64_t divisor,
	hx_os_uint128 *quotient, uint64_t *remainder)
{
#if HX_OS_ENABLE_WIDE_MATH
	hx_os_uint128 result = {0, 0};
	uint64_t rest = 0;
	if (!quotient || !divisor)
		return HX_OS_INVALID;
#if defined(__SIZEOF_INT128__) && !defined(HX_OS_TEST_PORTABLE_WIDE)
	{
		__uint128_t number =
			((__uint128_t)value.high << 64) | value.low;
		__uint128_t divided = number / divisor;
		result.low = (uint64_t)divided;
		result.high = (uint64_t)(divided >> 64);
		rest = (uint64_t)(number % divisor);
	}
#else
	{
		int bit;
		for (bit = 127; bit >= 0; --bit) {
			unsigned shift = (unsigned)bit & 63;
			uint64_t input = bit >= 64 ? value.high : value.low;
			int carry = (int)(rest >> 63);
			rest = (rest << 1) | ((input >> shift) & 1);
			if (carry || rest >= divisor) {
				rest -= divisor;
				if (bit >= 64)
					result.high |= UINT64_C(1) << shift;
				else
					result.low |= UINT64_C(1) << shift;
			}
		}
	}
#endif
	*quotient = result;
	if (remainder)
		*remainder = rest;
	return HX_OS_OK;
#else
	(void)value;
	(void)divisor;
	(void)quotient;
	(void)remainder;
	return HX_OS_NOT_SUPPORTED;
#endif
}
hx_os_status hx_os_muldiv_u64(uint64_t a, uint64_t b, uint64_t divisor,
	int rounding, uint64_t *out, hx_os_error *error)
{
#if HX_OS_ENABLE_WIDE_MATH
	hx_os_uint128 product, quotient;
	uint64_t remainder, rounded;
	hx_os_status status;
	int increment;
	if (!out || !divisor || rounding < HX_OS_ROUND_DOWN ||
		rounding > HX_OS_ROUND_NEAREST)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_u128_multiply(a, b, &product);
	if (status == HX_OS_OK)
		status = hx_os_u128_divide(
			product, divisor, &quotient, &remainder);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	increment = remainder &&
		(rounding == HX_OS_ROUND_UP ||
			(rounding == HX_OS_ROUND_NEAREST &&
				remainder >= divisor - remainder));
	if (quotient.high || (increment && quotient.low == UINT64_MAX))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	rounded = quotient.low + (unsigned)increment;
	*out = rounded;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)a;
	(void)b;
	(void)divisor;
	(void)rounding;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
