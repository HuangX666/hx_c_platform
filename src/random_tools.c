#include "internal.h"
#include "hx_os_random.h"

static uint64_t hx_os_rng_mix(uint64_t *seed)
{
	uint64_t value = (*seed += UINT64_C(0x9e3779b97f4a7c15));

	value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
	return value ^ (value >> 31);
}

void hx_os_rng_seed(hx_os_rng *rng, uint64_t seed)
{
	unsigned i;

	for (i = 0; i < 4; ++i)
		rng->state[i] = hx_os_rng_mix(&seed);
}

hx_os_status hx_os_rng_seed_system(hx_os_rng *rng, hx_os_error *error)
{
	hx_os_rng value;
	hx_os_status status;

	if (!rng)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_random_bytes(&value, sizeof(value), error);
	if (status != HX_OS_OK)
		return status;
	if (!(value.state[0] | value.state[1] | value.state[2] |
		    value.state[3]))
		hx_os_rng_seed(&value, 0);
	*rng = value;

	return HX_OS_OK;
}

static uint64_t hx_os_rng_rotate(uint64_t value, unsigned shift)
{
	return (value << shift) | (value >> (64 - shift));
}

uint64_t hx_os_rng_next(hx_os_rng *rng)
{
	uint64_t result = hx_os_rng_rotate(rng->state[1] * 5, 7) * 9;
	uint64_t temporary = rng->state[1] << 17;

	rng->state[2] ^= rng->state[0];
	rng->state[3] ^= rng->state[1];
	rng->state[1] ^= rng->state[2];
	rng->state[0] ^= rng->state[3];
	rng->state[2] ^= temporary;
	rng->state[3] = hx_os_rng_rotate(rng->state[3], 45);

	return result;
}

hx_os_status hx_os_rng_bounded(
	hx_os_rng *rng, uint64_t bound, uint64_t *out, hx_os_error *error)
{
	uint64_t value, threshold;

	if (!rng || !bound || !out ||
		!(rng->state[0] | rng->state[1] | rng->state[2] |
			rng->state[3]))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	threshold = (UINT64_C(0) - bound) % bound;
	do {
		value = hx_os_rng_next(rng);
	} while (value < threshold);
	*out = value % bound;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
