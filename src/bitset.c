#include "internal.h"
#include "hx_bitset.h"

struct hx_os_bitset {
	hx_os_context *context;
	uint32_t *words;
	size_t size;
	size_t max_size;
	size_t capacity;
};

#if HX_OS_ENABLE_CONTAINERS
static size_t hx_os_bitset_words(size_t bits)
{
	return bits / 32 + (bits % 32 != 0);
}

static uint32_t hx_os_bitset_low_mask(unsigned bits)
{
	return bits == 32 ? UINT32_MAX : (UINT32_C(1) << bits) - 1;
}

static void hx_os_bitset_trim(hx_os_bitset *bits)
{
	unsigned tail = (unsigned)(bits->size % 32);

	if (tail)
		bits->words[bits->size / 32] &= hx_os_bitset_low_mask(tail);
}

static unsigned hx_os_bitset_popcount(uint32_t word)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned)__builtin_popcount((unsigned)word);
#else
	word -= (word >> 1) & UINT32_C(0x55555555);
	word = (word & UINT32_C(0x33333333)) +
		((word >> 2) & UINT32_C(0x33333333));
	word = (word + (word >> 4)) & UINT32_C(0x0f0f0f0f);

	return (unsigned)((word * UINT32_C(0x01010101)) >> 24);
#endif
}

// Called only for a nonzero word.
static unsigned hx_os_bitset_first(uint32_t word)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned)__builtin_ctz((unsigned)word);
#else
	unsigned result = 0;

	while (!(word & 1)) {
		word >>= 1;
		++result;
	}

	return result;
#endif
}

hx_os_status hx_os_bitset_resize(
	hx_os_bitset *bits, size_t bit_count, hx_os_error *error)
{
	size_t words, old_words;

	if (!bits)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bit_count > bits->max_size)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	words = hx_os_bitset_words(bit_count);
	old_words = hx_os_bitset_words(bits->size);
	if (words > bits->capacity) {
		size_t maximum = hx_os_bitset_words(bits->max_size);
		size_t capacity = bits->capacity;
		uint32_t *next;

		if (capacity <= maximum / 2)
			capacity *= 2;
		else
			capacity = maximum;
		if (capacity < words)
			capacity = words;

		next = hx_os_i_zero(bits->context, capacity * sizeof(*next));
		if (!next)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		if (old_words)
			memcpy(next, bits->words, old_words * sizeof(*next));

		hx_os_memory_free(bits->words);
		bits->words = next;
		bits->capacity = capacity;
	}
	else if (words < old_words)
		memset(bits->words + words, 0,
			(old_words - words) * sizeof(*bits->words));

	bits->size = bit_count;
	hx_os_bitset_trim(bits);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bitset_create(hx_os_context *context, size_t bit_count,
	size_t max_bits, hx_os_bitset **out, hx_os_error *error)
{
	hx_os_bitset *bits;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!max_bits)
		max_bits = 1048576;
	if (bit_count > max_bits)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	bits = hx_os_i_zero(context, sizeof(*bits));
	if (!bits)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	bits->context = context;
	bits->max_size = max_bits;
	status = hx_os_bitset_resize(bits, bit_count, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(bits);

		return status;
	}

	*out = bits;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bitset_set_range(hx_os_bitset *bits, size_t first,
	size_t count, int value, hx_os_error *error)
{
	size_t word;
	unsigned offset;

	if (!bits)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (first > bits->size || count > bits->size - first)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	word = first / 32;
	offset = (unsigned)(first % 32);
	while (count) {
		unsigned length = 32 - offset;
		uint32_t mask;

		if (count < length)
			length = (unsigned)count;
		mask = hx_os_bitset_low_mask(length) << offset;
		if (value)
			bits->words[word] |= mask;
		else
			bits->words[word] &= ~mask;

		count -= length;
		++word;
		offset = 0;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bitset_set(
	hx_os_bitset *bits, size_t bit, int value, hx_os_error *error)
{
	return hx_os_bitset_set_range(bits, bit, 1, value, error);
}

hx_os_status hx_os_bitset_test(
	const hx_os_bitset *bits, size_t bit, int *value, hx_os_error *error)
{
	if (!bits || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bit >= bits->size)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*value = (bits->words[bit / 32] & (UINT32_C(1) << (bit % 32))) != 0;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bitset_fill(
	hx_os_bitset *bits, int value, hx_os_error *error)
{
	if (!bits)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (bits->size)
		memset(bits->words, value ? 0xff : 0,
			hx_os_bitset_words(bits->size) * sizeof(*bits->words));
	hx_os_bitset_trim(bits);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_bitset_count(const hx_os_bitset *bits)
{
	size_t i, count = 0;

	if (bits)
		for (i = 0; i < hx_os_bitset_words(bits->size); ++i)
			count += hx_os_bitset_popcount(bits->words[i]);

	return count;
}

hx_os_status hx_os_bitset_find_next(const hx_os_bitset *bits, size_t start,
	int value, size_t *index, hx_os_error *error)
{
	size_t i, words;
	uint32_t mask;

	if (!bits || !index)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (start > bits->size)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (start == bits->size)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);

	words = hx_os_bitset_words(bits->size);
	mask = UINT32_MAX << (start % 32);
	for (i = start / 32; i < words; ++i) {
		uint32_t word = (value ? bits->words[i] : ~bits->words[i]);

		word &= mask;
		if (i == words - 1 && bits->size % 32)
			word &= hx_os_bitset_low_mask(
				(unsigned)(bits->size % 32));
		if (word) {
			*index = i * 32 + hx_os_bitset_first(word);

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

		mask = UINT32_MAX;
	}

	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
}

hx_os_status hx_os_bitset_apply(hx_os_bitset *bits, const hx_os_bitset *other,
	hx_os_bitset_operation operation, hx_os_error *error)
{
	size_t i, words;

	if (!bits || !other || bits->size != other->size ||
		operation < HX_OS_BITSET_AND ||
		operation > HX_OS_BITSET_AND_NOT)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	words = hx_os_bitset_words(bits->size);
	for (i = 0; i < words; ++i) {
		switch (operation) {
		case HX_OS_BITSET_AND:
			bits->words[i] &= other->words[i];
			break;
		case HX_OS_BITSET_OR:
			bits->words[i] |= other->words[i];
			break;
		case HX_OS_BITSET_XOR:
			bits->words[i] ^= other->words[i];
			break;
		case HX_OS_BITSET_AND_NOT:
			bits->words[i] &= ~other->words[i];
			break;
		}
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_bitset_relation(const hx_os_bitset *left,
	const hx_os_bitset *right, int subset, int *value, hx_os_error *error)
{
	size_t i;
	int result = subset;

	if (!left || !right || !value || left->size != right->size)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (i = 0; i < hx_os_bitset_words(left->size); ++i) {
		uint32_t word = subset ? ~right->words[i] : right->words[i];

		if (left->words[i] & word) {
			result = !subset;
			break;
		}
	}

	*value = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#else
hx_os_status hx_os_bitset_create(hx_os_context *context, size_t bit_count,
	size_t max_bits, hx_os_bitset **out, hx_os_error *error)
{
	(void)context;
	(void)bit_count;
	(void)max_bits;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_bitset_resize(
	hx_os_bitset *bits, size_t bit_count, hx_os_error *error)
{
	(void)bits;
	(void)bit_count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_bitset_set_range(hx_os_bitset *bits, size_t first,
	size_t count, int value, hx_os_error *error)
{
	(void)bits;
	(void)first;
	(void)count;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_bitset_set(
	hx_os_bitset *bits, size_t bit, int value, hx_os_error *error)
{
	return hx_os_bitset_set_range(bits, bit, 1, value, error);
}

hx_os_status hx_os_bitset_test(
	const hx_os_bitset *bits, size_t bit, int *value, hx_os_error *error)
{
	(void)bits;
	(void)bit;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_bitset_fill(
	hx_os_bitset *bits, int value, hx_os_error *error)
{
	(void)bits;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_bitset_count(const hx_os_bitset *bits)
{
	(void)bits;

	return 0;
}

hx_os_status hx_os_bitset_find_next(const hx_os_bitset *bits, size_t start,
	int value, size_t *index, hx_os_error *error)
{
	(void)bits;
	(void)start;
	(void)value;
	(void)index;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_bitset_apply(hx_os_bitset *bits, const hx_os_bitset *other,
	hx_os_bitset_operation operation, hx_os_error *error)
{
	(void)bits;
	(void)other;
	(void)operation;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

static hx_os_status hx_os_bitset_relation(const hx_os_bitset *left,
	const hx_os_bitset *right, int subset, int *value, hx_os_error *error)
{
	(void)left;
	(void)right;
	(void)subset;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
#endif

size_t hx_os_bitset_size(const hx_os_bitset *bits)
{
	return bits ? bits->size : 0;
}

size_t hx_os_bitset_max_size(const hx_os_bitset *bits)
{
	return bits ? bits->max_size : 0;
}

hx_os_status hx_os_bitset_destroy(hx_os_bitset **bits, hx_os_error *error)
{
	if (!bits)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (*bits) {
		hx_os_memory_free((*bits)->words);
		hx_os_memory_free(*bits);
		*bits = NULL;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_bitset_reset(
	hx_os_bitset *bits, size_t bit, hx_os_error *error)
{
	return hx_os_bitset_set(bits, bit, 0, error);
}

hx_os_status hx_os_bitset_clear(hx_os_bitset *bits, hx_os_error *error)
{
	return hx_os_bitset_fill(bits, 0, error);
}

hx_os_status hx_os_bitset_intersects(const hx_os_bitset *left,
	const hx_os_bitset *right, int *value, hx_os_error *error)
{
	return hx_os_bitset_relation(left, right, 0, value, error);
}

hx_os_status hx_os_bitset_is_subset(const hx_os_bitset *left,
	const hx_os_bitset *right, int *value, hx_os_error *error)
{
	return hx_os_bitset_relation(left, right, 1, value, error);
}
