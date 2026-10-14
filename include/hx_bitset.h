#ifndef HX_C_PLATFORM_BITSET_H
#define HX_C_PLATFORM_BITSET_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_bitset hx_os_bitset;

typedef enum hx_os_bitset_operation {
	HX_OS_BITSET_AND,
	HX_OS_BITSET_OR,
	HX_OS_BITSET_XOR,
	HX_OS_BITSET_AND_NOT
} hx_os_bitset_operation;

/**
 * \brief Bit numbers are zero-based; storage layout is private, not a wire
 * format.
 *
 * \details max_bits=0 selects 1,048,576 bits. New bits are zero. Growth is
 * bounded by max_bits; resizing may temporarily retain both old and new
 * allocations. NULL context uses the default allocator. A custom context must
 * outlive the bitset. No internal locks: exclude concurrent mutation and
 * destruction. Allocator callbacks must not reenter this object. Failed
 * mutations preserve contents and size. Padding bits are always zero and never
 * counted/searched.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_create(hx_os_context *context,
	size_t bit_count, size_t max_bits, hx_os_bitset **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_destroy(
	hx_os_bitset **bits, hx_os_error *error);

/**
 * \brief Queries return zero for NULL.
 *
 * \details count counts set bits, not storage words.
 */
HX_OS_API size_t HX_OS_CALL hx_os_bitset_size(const hx_os_bitset *bits);
HX_OS_API size_t HX_OS_CALL hx_os_bitset_max_size(const hx_os_bitset *bits);
HX_OS_API size_t HX_OS_CALL hx_os_bitset_count(const hx_os_bitset *bits);

/**
 * \brief Shrinking discards removed values.
 *
 * \details Regrowth always exposes zero bits. Shrinking retains capacity;
 * destroy releases all storage.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_resize(
	hx_os_bitset *bits, size_t bit_count, hx_os_error *error);

/**
 * \brief Values use C truth semantics: zero clears, any nonzero value sets.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_set(
	hx_os_bitset *bits, size_t bit, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_reset(
	hx_os_bitset *bits, size_t bit, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_test(
	const hx_os_bitset *bits, size_t bit, int *value, hx_os_error *error);

/**
 * \brief The half-open range [first, first + count) must fit the current size.
 *
 * \details A zero count accepts first == size. Invalid ranges return RANGE.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_set_range(hx_os_bitset *bits,
	size_t first, size_t count, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_fill(
	hx_os_bitset *bits, int value, hx_os_error *error);

/**
 * \brief Clears values while preserving the logical size.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_clear(
	hx_os_bitset *bits, hx_os_error *error);

/**
 * \brief Search includes start.
 *
 * \details start == size returns NOT_FOUND, start > size returns RANGE. Outputs
 * from test/search/set relations change on OK only.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_find_next(
	const hx_os_bitset *bits, size_t start, int value, size_t *index,
	hx_os_error *error);

/**
 * \brief Set operations require equal sizes, otherwise INVALID.
 *
 * \details Self operations are supported. is_subset asks whether every set bit
 * in left is in right.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_apply(hx_os_bitset *bits,
	const hx_os_bitset *other, hx_os_bitset_operation operation,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_intersects(
	const hx_os_bitset *left, const hx_os_bitset *right, int *value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_bitset_is_subset(
	const hx_os_bitset *left, const hx_os_bitset *right, int *value,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
