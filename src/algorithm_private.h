#ifndef HX_C_PLATFORM_ALGORITHM_PRIVATE_H
#define HX_C_PLATFORM_ALGORITHM_PRIVATE_H

#include "hx_algorithm.h"

hx_os_status hx_os_i_array_check(const void *data, size_t count, size_t size);
void hx_os_i_array_swap(void *data, size_t size, size_t a, size_t b);

/**
 * \brief Validated ranges only.
 *
 * \details Scratch must fit count records for stable sort, or one correctly
 * aligned record for selection. Never calls copy/drop.
 */
void hx_os_i_array_stable_sort(void *data, size_t count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, void *scratch);

void hx_os_i_array_nth_element(void *data, size_t count, size_t element_size,
	size_t index, hx_os_value_compare_fn compare, void *user, void *pivot);

#endif
