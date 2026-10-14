#include "internal.h"
#include "algorithm_private.h"

static hx_os_status hx_os_array_rng(
	hx_os_rng **rng, hx_os_rng *local, hx_os_error *error)
{
	if (!*rng) {
		hx_os_status status = hx_os_rng_seed_system(local, error);

		if (status != HX_OS_OK)
			return status;
		*rng = local;
	}
	if (!((*rng)->state[0] | (*rng)->state[1] | (*rng)->state[2] |
		    (*rng)->state[3]))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return HX_OS_OK;
}

hx_os_status hx_os_array_shuffle(void *data, size_t count, size_t size,
	hx_os_rng *rng, hx_os_error *error)
{
	hx_os_rng local;
	size_t i;
	hx_os_status status = hx_os_i_array_check(data, count, size);

	if (status != HX_OS_OK || count < 2)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_array_rng(&rng, &local, error);
	if (status != HX_OS_OK)
		return status;
	for (i = count; i > 1; --i) {
		uint64_t index;

		(void)hx_os_rng_bounded(rng, (uint64_t)i, &index, NULL);
		hx_os_i_array_swap(data, size, i - 1, (size_t)index);
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_sample(const void *data, size_t count, size_t size,
	void *out, size_t capacity, hx_os_rng *rng, size_t *written,
	hx_os_error *error)
{
	hx_os_rng local;
	size_t needed = count < capacity ? count : capacity, i, n = 0;
	hx_os_status status = hx_os_i_array_check(data, count, size);

	if (written)
		*written = 0;
	if (!written)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK)
		status = hx_os_i_array_check(out, needed, size);
	if (status == HX_OS_OK && needed && count &&
		(uintptr_t)data < (uintptr_t)out + needed * size &&
		(uintptr_t)out < (uintptr_t)data + count * size)
		status = HX_OS_INVALID;
	if (status != HX_OS_OK || !needed)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_array_rng(&rng, &local, error);
	if (status != HX_OS_OK)
		return status;
	for (i = 0; needed; ++i) {
		uint64_t index;

		(void)hx_os_rng_bounded(
			rng, (uint64_t)(count - i), &index, NULL);
		if (index < (uint64_t)needed) {
			memcpy((unsigned char *)out + n * size,
				(const unsigned char *)data + i * size, size);
			++n;
			--needed;
		}
	}
	*written = n;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static unsigned hx_os_radix_digit(
	const unsigned char *record, unsigned width, unsigned pass, int sign)
{
	uint64_t key;

	if (width == 4) {
		uint32_t number;

		memcpy(&number, record, sizeof(number));
		key = number;
	}
	else
		memcpy(&key, record, sizeof(key));
	if (sign)
		key ^= UINT64_C(1) << (width * 8 - 1);
	return (unsigned)((key >> (pass * 8)) & 255);
}

static hx_os_status hx_os_radix(hx_os_context *context, void *data,
	size_t count, unsigned width, int sign, size_t budget,
	hx_os_error *error)
{
	unsigned char *scratch, *source = data, *destination;
	size_t bytes, cost, i;
	unsigned pass;
	hx_os_status status = hx_os_i_array_check(data, count, width);

	if (status != HX_OS_OK || count < 2)
		return hx_os_i_error(error, status, 0, 0);
	bytes = count * width;
	cost = hx_os_i_allocation_cost(bytes, 0);
	if (!budget)
		budget = 16 * 1024 * 1024;
	if (!cost || cost > budget)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	scratch = hx_os_i_alloc(context, bytes, 0);
	if (!scratch)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	destination = scratch;
	for (pass = 0; pass < width; ++pass) {
		size_t bucket[256] = {0}, total = 0;
		unsigned digit;
		unsigned char *temporary;

		for (i = 0; i < count; ++i)
			++bucket[hx_os_radix_digit(
				source + i * width, width, pass, sign)];
		for (digit = 0; digit < 256; ++digit) {
			size_t next = total + bucket[digit];

			bucket[digit] = total;
			total = next;
		}
		for (i = 0; i < count; ++i) {
			digit = hx_os_radix_digit(
				source + i * width, width, pass, sign);
			memcpy(destination + bucket[digit]++ * width,
				source + i * width, width);
		}
		temporary = source;
		source = destination;
		destination = temporary;
	}
	hx_os_memory_free(scratch);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

#define HX_OS_RADIX(suffix, type, sign)                                        \
	hx_os_status hx_os_radix_sort_##suffix(hx_os_context *context,         \
		type *data, size_t count, size_t budget, hx_os_error *error)   \
	{                                                                      \
		return hx_os_radix(context, data, count, sizeof(type), sign,   \
			budget, error);                                        \
	}
HX_OS_RADIX(i32, int32_t, 1)
HX_OS_RADIX(u32, uint32_t, 0)
HX_OS_RADIX(i64, int64_t, 1)
HX_OS_RADIX(u64, uint64_t, 0)
#undef HX_OS_RADIX
