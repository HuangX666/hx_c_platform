#include "internal.h"
#include "hx_bloom.h"
#include <math.h>

struct hx_os_bloom {
	unsigned char *bits;
	size_t count;
	unsigned hashes;
};

hx_os_status hx_os_bloom_create(hx_os_context *context, size_t items,
	double rate, size_t budget, hx_os_bloom **out, hx_os_error *error)
{
	hx_os_bloom *bloom;
	double required;
	size_t bits = 8, cost;
	unsigned hashes;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!HX_OS_ENABLE_CONTAINERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!items || !(rate >= 1e-9 && rate <= 0.5))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!budget)
		budget = 16 * 1024 * 1024;
	if (budget > 64 * 1024 * 1024)
		budget = 64 * 1024 * 1024;
	required = -(double)items * log(rate) / 0.4804530139182014;
	while ((double)bits < required && bits < budget * 8)
		bits *= 2;
	cost = hx_os_i_allocation_cost(bits / 8, 0);
	if ((double)bits < required || !cost || cost > budget)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	hashes = (unsigned)((double)bits / (double)items * 0.69314718056);
	if (!hashes)
		hashes = 1;
	if (hashes > 32)
		hashes = 32;
	bloom = hx_os_i_zero(context, sizeof(*bloom));
	if (!bloom)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	bloom->bits = hx_os_i_zero(context, bits / 8);
	if (!bloom->bits) {
		hx_os_memory_free(bloom);
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	bloom->count = bits;
	bloom->hashes = hashes;
	*out = bloom;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_bloom_hash(hx_os_bytes_view value, uint64_t hash[2])
{
	const unsigned char *data = value.data;
	size_t i;
	uint64_t first = UINT64_C(14695981039346656037);

	for (i = 0; i < value.size; ++i)
		first = (first ^ data[i]) * UINT64_C(1099511628211);
	first ^= first >> 33;
	first *= UINT64_C(0xff51afd7ed558ccd);
	first ^= first >> 33;
	hash[0] = first;
	hash[1] = (first * UINT64_C(0xc4ceb9fe1a85ec53) ^ (first >> 17)) | 1;
}

hx_os_status hx_os_bloom_insert(
	hx_os_bloom *bloom, hx_os_bytes_view value, hx_os_error *error)
{
	uint64_t hash[2];
	unsigned i;

	if (!bloom || (!value.data && value.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_bloom_hash(value, hash);
	for (i = 0; i < bloom->hashes; ++i) {
		size_t bit = (size_t)(hash[0] & (bloom->count - 1));

		bloom->bits[bit / 8] |= (unsigned char)(1u << (bit % 8));
		hash[0] += hash[1];
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bloom_contains(const hx_os_bloom *bloom,
	hx_os_bytes_view value, int *maybe, hx_os_error *error)
{
	uint64_t hash[2];
	unsigned i;

	if (!bloom || !maybe || (!value.data && value.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_bloom_hash(value, hash);
	*maybe = 1;
	for (i = 0; i < bloom->hashes; ++i) {
		size_t bit = (size_t)(hash[0] & (bloom->count - 1));

		if (!(bloom->bits[bit / 8] & (1u << (bit % 8)))) {
			*maybe = 0;
			break;
		}
		hash[0] += hash[1];
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_bloom_clear(hx_os_bloom *bloom)
{
	memset(bloom->bits, 0, bloom->count / 8);
}

size_t hx_os_bloom_size_bytes(const hx_os_bloom *bloom)
{
	return bloom->count / 8;
}

void hx_os_bloom_destroy(hx_os_bloom **bloom)
{
	hx_os_bloom *value = *bloom;

	*bloom = NULL;
	if (value) {
		hx_os_memory_free(value->bits);
		hx_os_memory_free(value);
	}
}
