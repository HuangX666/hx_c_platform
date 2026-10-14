#include "internal.h"
#include "algorithm_private.h"
#include "containers_private.h"

hx_os_status hx_os_i_array_check(const void *data, size_t count, size_t size)
{
	if (!HX_OS_ENABLE_CONTAINERS)
		return HX_OS_NOT_SUPPORTED;
	if ((!data && count) || !size)
		return HX_OS_INVALID;
	if (count > SIZE_MAX / size ||
		count * size > UINTPTR_MAX - (uintptr_t)data)
		return HX_OS_RANGE;

	return HX_OS_OK;
}

static unsigned char *hx_os_array_at(void *data, size_t size, size_t index)
{
	return (unsigned char *)data + size * index;
}

static const unsigned char *hx_os_array_const_at(
	const void *data, size_t size, size_t index)
{
	return (const unsigned char *)data + size * index;
}

void hx_os_i_array_swap(void *data, size_t size, size_t a, size_t b)
{
	unsigned char *left = hx_os_array_at(data, size, a);
	unsigned char *right = hx_os_array_at(data, size, b);
	size_t i;

	if (a == b)
		return;

	for (i = 0; i < size; ++i) {
		unsigned char byte = left[i];

		left[i] = right[i];
		right[i] = byte;
	}
}

static void hx_os_array_sift(void *data, size_t size, size_t root, size_t count,
	hx_os_value_compare_fn compare, void *user)
{
	while (root < count / 2) {
		size_t child = root * 2 + 1;

		if (child + 1 < count &&
			compare(hx_os_array_at(data, size, child),
				hx_os_array_at(data, size, child + 1),
				user) < 0)
			++child;
		if (compare(hx_os_array_at(data, size, root),
			    hx_os_array_at(data, size, child), user) >= 0)
			break;

		hx_os_i_array_swap(data, size, root, child);
		root = child;
	}
}

static void hx_os_array_heap_sort(void *data, size_t count, size_t size,
	hx_os_value_compare_fn compare, void *user)
{
	size_t i;

	for (i = count / 2; i; --i)
		hx_os_array_sift(data, size, i - 1, count, compare, user);

	for (i = count; i > 1; --i) {
		hx_os_i_array_swap(data, size, 0, i - 1);
		hx_os_array_sift(data, size, 0, i - 1, compare, user);
	}
}

hx_os_status hx_os_array_sort(void *data, size_t count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);

	if (status == HX_OS_OK && !compare)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK)
		hx_os_array_heap_sort(data, count, element_size, compare, user);

	return hx_os_i_error(error, status, 0, 0);
}

void hx_os_i_array_stable_sort(void *data, size_t count, size_t size,
	hx_os_value_compare_fn compare, void *user, void *scratch)
{
	size_t width, start;

	for (width = 1; width < count;) {
		for (start = 0; start < count;) {
			size_t mid = start +
				(count - start < width ? count - start : width);
			size_t end = mid +
				(count - mid < width ? count - mid : width);
			size_t left = start, right = mid, target = start;

			while (left < mid || right < end) {
				size_t selected;

				if (left < mid &&
					(right == end ||
						compare(hx_os_array_at(data,
								size, left),
							hx_os_array_at(data,
								size, right),
							user) <= 0))
					selected = left++;
				else
					selected = right++;

				memcpy(hx_os_array_at(scratch, size, target++),
					hx_os_array_at(data, size, selected),
					size);
			}

			start = end;
		}

		memcpy(data, scratch, count * size);
		if (width > count / 2)
			break;

		width *= 2;
	}
}

static hx_os_status hx_os_array_scratch(hx_os_context *context, size_t bytes,
	size_t alignment, size_t maximum, void **out)
{
	size_t cost = hx_os_i_allocation_cost(bytes, alignment);

	if (!maximum)
		maximum = 16 * 1024 * 1024;
	if (!cost)
		return HX_OS_RANGE;
	if (cost > maximum)
		return HX_OS_LIMIT;

	*out = hx_os_i_alloc(context, bytes, alignment);

	return *out ? HX_OS_OK : HX_OS_NO_MEMORY;
}

hx_os_status hx_os_array_stable_sort(hx_os_context *context, void *data,
	size_t count, size_t element_size, hx_os_value_compare_fn compare,
	void *user, size_t max_scratch_bytes, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	void *scratch = NULL;

	if (status == HX_OS_OK && !compare)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK && count > 1) {
		status = hx_os_array_scratch(context, count * element_size, 0,
			max_scratch_bytes, &scratch);
		if (status == HX_OS_OK)
			hx_os_i_array_stable_sort(data, count, element_size,
				compare, user, scratch);

		hx_os_memory_free(scratch);
	}

	return hx_os_i_error(error, status, 0, 0);
}

static size_t hx_os_array_bound(const void *data, size_t count, size_t size,
	const void *key, hx_os_value_compare_fn compare, void *user, int upper)
{
	size_t low = 0, high = count;

	while (low < high) {
		size_t middle = low + (high - low) / 2;
		int order = compare(
			hx_os_array_const_at(data, size, middle), key, user);

		if (order < 0 || (upper && !order))
			low = middle + 1;
		else
			high = middle;
	}

	return low;
}

static hx_os_status hx_os_array_bound_query(const void *data, size_t count,
	size_t size, const void *key, hx_os_value_compare_fn compare,
	void *user, int upper, size_t *index, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, size);

	if (status == HX_OS_OK && (!key || !compare || !index))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK)
		*index = hx_os_array_bound(
			data, count, size, key, compare, user, upper);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_array_lower_bound(const void *data, size_t count,
	size_t element_size, const void *key, hx_os_value_compare_fn compare,
	void *user, size_t *index, hx_os_error *error)
{
	return hx_os_array_bound_query(
		data, count, element_size, key, compare, user, 0, index, error);
}

hx_os_status hx_os_array_upper_bound(const void *data, size_t count,
	size_t element_size, const void *key, hx_os_value_compare_fn compare,
	void *user, size_t *index, hx_os_error *error)
{
	return hx_os_array_bound_query(
		data, count, element_size, key, compare, user, 1, index, error);
}

hx_os_status hx_os_array_binary_search(const void *data, size_t count,
	size_t element_size, const void *key, hx_os_value_compare_fn compare,
	void *user, int *found, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);

	if (status == HX_OS_OK && (!key || !compare || !found))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK) {
		size_t index = hx_os_array_bound(
			data, count, element_size, key, compare, user, 0);

		*found = index < count &&
			!compare(
				hx_os_array_const_at(data, element_size, index),
				key, user);
	}

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_array_find_if(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	size_t *index, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i;

	if (status == HX_OS_OK && (!predicate || !index))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i)
		if (predicate(hx_os_array_const_at(data, element_size, i),
			    user)) {
			*index = i;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

	return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
}

hx_os_status hx_os_array_count_if(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	size_t *matches, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, result = 0;

	if (status == HX_OS_OK && (!predicate || !matches))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i)
		result += predicate(hx_os_array_const_at(data, element_size, i),
				  user) != 0;

	*matches = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_minmax(const void *data, size_t count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	size_t *minimum, size_t *maximum, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, low = 0, high = 0;

	if (status == HX_OS_OK &&
		(!compare || !minimum || !maximum || minimum == maximum))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK && !count)
		status = HX_OS_NOT_FOUND;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 1; i < count; ++i) {
		const void *value = hx_os_array_const_at(data, element_size, i);

		if (compare(value,
			    hx_os_array_const_at(data, element_size, low),
			    user) < 0)
			low = i;
		if (compare(value,
			    hx_os_array_const_at(data, element_size, high),
			    user) > 0)
			high = i;
	}

	*minimum = low;
	*maximum = high;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_is_sorted(const void *data, size_t count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	int *sorted, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i;
	int result = 1;

	if (status == HX_OS_OK && (!compare || !sorted))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 1; i < count; ++i)
		if (compare(hx_os_array_const_at(data, element_size, i - 1),
			    hx_os_array_const_at(data, element_size, i),
			    user) > 0) {
			result = 0;
			break;
		}

	*sorted = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void hx_os_array_reverse_range(
	void *data, size_t size, size_t first, size_t last)
{
	while (first < last && first < --last)
		hx_os_i_array_swap(data, size, first++, last);
}

hx_os_status hx_os_array_reverse(
	void *data, size_t count, size_t element_size, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);

	if (status == HX_OS_OK)
		hx_os_array_reverse_range(data, element_size, 0, count);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_array_rotate(void *data, size_t count, size_t element_size,
	size_t left_count, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);

	if (status == HX_OS_OK && count) {
		left_count %= count;
		if (left_count) {
			hx_os_array_reverse_range(
				data, element_size, 0, left_count);
			hx_os_array_reverse_range(
				data, element_size, left_count, count);
			hx_os_array_reverse_range(data, element_size, 0, count);
		}
	}

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_array_partition(void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	size_t *boundary, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, kept = 0;

	if (status == HX_OS_OK && (!predicate || !boundary))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i)
		if (predicate(hx_os_array_at(data, element_size, i), user))
			hx_os_i_array_swap(data, element_size, kept++, i);

	*boundary = kept;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_unique(void *data, size_t count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, size_t *remaining,
	hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, kept = count != 0;

	if (status == HX_OS_OK && (!compare || !remaining))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 1; i < count; ++i)
		if (compare(hx_os_array_at(data, element_size, kept - 1),
			    hx_os_array_at(data, element_size, i), user)) {
			if (i != kept)
				memcpy(hx_os_array_at(data, element_size, kept),
					hx_os_array_at(data, element_size, i),
					element_size);
			++kept;
		}

	*remaining = kept;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_partial_sort(void *data, size_t count,
	size_t element_size, size_t prefix_count,
	hx_os_value_compare_fn compare, void *user, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i;

	if (status == HX_OS_OK && !compare)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK && prefix_count > count)
		status = HX_OS_RANGE;
	if (status != HX_OS_OK || !prefix_count)
		return hx_os_i_error(error, status, 0, 0);

	for (i = prefix_count / 2; i; --i)
		hx_os_array_sift(
			data, element_size, i - 1, prefix_count, compare, user);

	for (i = prefix_count; i < count; ++i)
		if (compare(hx_os_array_at(data, element_size, i), data, user) <
			0) {
			hx_os_i_array_swap(data, element_size, i, 0);
			hx_os_array_sift(data, element_size, 0, prefix_count,
				compare, user);
		}

	for (i = prefix_count; i > 1; --i) {
		hx_os_i_array_swap(data, element_size, 0, i - 1);
		hx_os_array_sift(data, element_size, 0, i - 1, compare, user);
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_array_nth_element(void *data, size_t count, size_t size,
	size_t index, hx_os_value_compare_fn compare, void *user, void *pivot)
{
	size_t first = 0, last, budget = 0;

	for (last = count; last > 1; last /= 2)
		budget += 2;

	last = count;
	while (last - first > 1) {
		size_t lower = first, scan = first, upper = last;

		if (!budget--) {
			hx_os_array_heap_sort(data, count, size, compare, user);
			break;
		}

		memcpy(pivot,
			hx_os_array_at(data, size, first + (last - first) / 2),
			size);
		while (scan < upper) {
			int order = compare(
				hx_os_array_at(data, size, scan), pivot, user);

			if (order < 0)
				hx_os_i_array_swap(data, size, lower++, scan++);
			else if (order > 0)
				hx_os_i_array_swap(data, size, scan, --upper);
			else
				++scan;
		}

		if (index < lower)
			last = lower;
		else if (index >= upper)
			first = upper;
		else
			break;
	}
}

hx_os_status hx_os_array_nth_element(hx_os_context *context, void *data,
	size_t count, size_t element_size, size_t index,
	hx_os_value_compare_fn compare, void *user, size_t max_scratch_bytes,
	hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	void *pivot = NULL;
	uintptr_t alignment;

	if (status == HX_OS_OK && !compare)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK && index >= count)
		status = HX_OS_RANGE;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (count == 1)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	// Preserve the alignment common to the base and every element stride.
	alignment = (uintptr_t)data | (uintptr_t)element_size;
	alignment &= ~alignment + 1;
	status = hx_os_array_scratch(context, element_size, (size_t)alignment,
		max_scratch_bytes, &pivot);
	if (status == HX_OS_OK)
		hx_os_i_array_nth_element(
			data, count, element_size, index, compare, user, pivot);

	hx_os_memory_free(pivot);

	return hx_os_i_error(error, status, 0, 0);
}

#define HX_OS_INTEGER_ALGORITHMS(name, type)                                   \
	static int hx_os_array_compare_##name(                                 \
		const void *left, const void *right, void *user)               \
	{                                                                      \
		type a, b;                                                     \
		(void)user;                                                    \
		memcpy(&a, left, sizeof(a));                                   \
		memcpy(&b, right, sizeof(b));                                  \
		return (a > b) - (a < b);                                      \
	}                                                                      \
	hx_os_status hx_os_sort_##name(                                        \
		type *values, size_t count, hx_os_error *error)                \
	{                                                                      \
		return hx_os_array_sort(values, count, sizeof(type),           \
			hx_os_array_compare_##name, NULL, error);              \
	}                                                                      \
	hx_os_status hx_os_binary_search_##name(const type *values,            \
		size_t count, type key, int *found, hx_os_error *error)        \
	{                                                                      \
		return hx_os_array_binary_search(values, count, sizeof(type),  \
			&key, hx_os_array_compare_##name, NULL, found, error); \
	}

HX_OS_INTEGER_ALGORITHMS(i32, int32_t)
HX_OS_INTEGER_ALGORITHMS(u32, uint32_t)
HX_OS_INTEGER_ALGORITHMS(i64, int64_t)
HX_OS_INTEGER_ALGORITHMS(u64, uint64_t)
#undef HX_OS_INTEGER_ALGORITHMS

static hx_os_status hx_os_array_output_check(const void *data, size_t count,
	size_t size, void *out, size_t capacity, size_t output_size,
	int inplace)
{
	hx_os_status status = hx_os_i_array_check(data, count, size);

	if (status == HX_OS_OK)
		status = hx_os_i_array_check(out, capacity, output_size);
	if (status == HX_OS_OK &&
		!(inplace && data == out && size == output_size) &&
		hx_os_i_container_overlap(
			data, count * size, out, capacity * output_size))
		status = HX_OS_INVALID;

	return status;
}

static void hx_os_array_progress_clear(hx_os_array_progress *progress)
{
	if (progress)
		memset(progress, 0, sizeof(*progress));
}

static hx_os_status hx_os_array_callback_error(
	hx_os_status status, const hx_os_error *detail, hx_os_error *error)
{
	if (error) {
		*error = *detail;
		error->status = status;
	}

	return status;
}

hx_os_status hx_os_array_copy_if(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	void *out, size_t capacity, hx_os_array_progress *progress,
	hx_os_error *error)
{
	hx_os_status status = hx_os_array_output_check(
		data, count, element_size, out, capacity, element_size, 0);
	size_t i;

	hx_os_array_progress_clear(progress);
	if (status == HX_OS_OK && (!predicate || !progress))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i) {
		const void *value = hx_os_array_const_at(data, element_size, i);

		if (predicate(value, user)) {
			if (progress->written == capacity)
				return hx_os_i_error(
					error, HX_OS_OUTPUT_FULL, 0, 0);

			memcpy(hx_os_array_at(
				       out, element_size, progress->written),
				value, element_size);
			++progress->written;
		}

		++progress->left_consumed;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_transform(const void *data, size_t count,
	size_t element_size, hx_os_value_transform_fn transform, void *user,
	void *out, size_t capacity, size_t output_element_size,
	hx_os_array_progress *progress, hx_os_error *error)
{
	hx_os_status status = hx_os_array_output_check(data, count,
		element_size, out, capacity, output_element_size, 1);
	size_t i;

	hx_os_array_progress_clear(progress);
	if (status == HX_OS_OK && (!transform || !progress))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i) {
		hx_os_error detail;

		if (i == capacity)
			return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

		hx_os_error_clear(&detail);
		status = transform(hx_os_array_at(out, output_element_size, i),
			hx_os_array_const_at(data, element_size, i), user,
			&detail);
		if (status != HX_OS_OK)
			return hx_os_array_callback_error(
				status, &detail, error);

		++progress->left_consumed;
		++progress->written;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_remove_if(void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	size_t *remaining, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, kept = 0;

	if (status == HX_OS_OK && (!predicate || !remaining))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i)
		if (!predicate(hx_os_array_at(data, element_size, i), user)) {
			if (i != kept)
				memcpy(hx_os_array_at(data, element_size, kept),
					hx_os_array_at(data, element_size, i),
					element_size);
			++kept;
		}

	*remaining = kept;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_replace_if(void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	const void *replacement, size_t *replaced, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);
	size_t i, matches = 0;

	if (status == HX_OS_OK && (!predicate || !replacement || !replaced))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK &&
		hx_os_i_container_overlap(
			data, count * element_size, replacement, element_size))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i)
		if (predicate(hx_os_array_at(data, element_size, i), user)) {
			memcpy(hx_os_array_at(data, element_size, i),
				replacement, element_size);
			++matches;
		}

	*replaced = matches;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_array_quantify(const void *data, size_t count,
	size_t size, hx_os_value_predicate_fn predicate, void *user, int mode,
	int *result, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, size);
	size_t i;
	int answer = mode != 0;

	if (status == HX_OS_OK && (!predicate || !result))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i) {
		int matches = predicate(hx_os_array_const_at(data, size, i),
				      user) != 0;

		if (matches != (mode == 1)) {
			answer = mode == 0;
			break;
		}
	}

	*result = answer;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_any_of(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	int *result, hx_os_error *error)
{
	return hx_os_array_quantify(
		data, count, element_size, predicate, user, 0, result, error);
}

hx_os_status hx_os_array_all_of(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	int *result, hx_os_error *error)
{
	return hx_os_array_quantify(
		data, count, element_size, predicate, user, 1, result, error);
}

hx_os_status hx_os_array_none_of(const void *data, size_t count,
	size_t element_size, hx_os_value_predicate_fn predicate, void *user,
	int *result, hx_os_error *error)
{
	return hx_os_array_quantify(
		data, count, element_size, predicate, user, 2, result, error);
}

static hx_os_status hx_os_array_accumulator_check(const void *data,
	size_t count, size_t size, void *accumulator, size_t accumulator_size)
{
	hx_os_status status = hx_os_i_array_check(data, count, size);

	if (status == HX_OS_OK)
		status = hx_os_i_array_check(accumulator, 1, accumulator_size);
	if (status == HX_OS_OK &&
		hx_os_i_container_overlap(
			data, count * size, accumulator, accumulator_size))
		status = HX_OS_INVALID;

	return status;
}

hx_os_status hx_os_array_fold(const void *data, size_t count,
	size_t element_size, void *accumulator, size_t accumulator_size,
	hx_os_value_fold_fn fold, void *user, size_t *completed,
	hx_os_error *error)
{
	hx_os_status status = hx_os_array_accumulator_check(
		data, count, element_size, accumulator, accumulator_size);
	size_t i;

	if (completed)
		*completed = 0;
	if (status == HX_OS_OK && (!fold || !completed))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i) {
		hx_os_error detail;

		hx_os_error_clear(&detail);
		status = fold(accumulator,
			hx_os_array_const_at(data, element_size, i), user,
			&detail);
		if (status != HX_OS_OK)
			return hx_os_array_callback_error(
				status, &detail, error);

		++*completed;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_mismatch(const void *left, size_t left_count,
	const void *right, size_t right_count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, size_t *index,
	hx_os_error *error)
{
	hx_os_status status =
		hx_os_i_array_check(left, left_count, element_size);
	size_t i, common = left_count < right_count ? left_count : right_count;

	if (status == HX_OS_OK)
		status = hx_os_i_array_check(right, right_count, element_size);
	if (status == HX_OS_OK && (!compare || !index))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < common; ++i)
		if (compare(hx_os_array_const_at(left, element_size, i),
			    hx_os_array_const_at(right, element_size, i), user))
			break;

	*index = i;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_equal(const void *left, size_t left_count,
	const void *right, size_t right_count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, int *equal,
	hx_os_error *error)
{
	hx_os_status status =
		hx_os_i_array_check(left, left_count, element_size);
	size_t index;

	if (status == HX_OS_OK)
		status = hx_os_i_array_check(right, right_count, element_size);
	if (status == HX_OS_OK && (!compare || !equal))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (left_count != right_count) {
		*equal = 0;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	status = hx_os_array_mismatch(left, left_count, right, right_count,
		element_size, compare, user, &index, error);
	if (status == HX_OS_OK)
		*equal = index == left_count;

	return status;
}

hx_os_status hx_os_array_equal_range(const void *data, size_t count,
	size_t element_size, const void *key, hx_os_value_compare_fn compare,
	void *user, size_t *first, size_t *last, hx_os_error *error)
{
	hx_os_status status = hx_os_i_array_check(data, count, element_size);

	if (status == HX_OS_OK &&
		(!key || !compare || !first || !last || first == last))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	*first = hx_os_array_bound(
		data, count, element_size, key, compare, user, 0);
	*last = hx_os_array_bound(
		data, count, element_size, key, compare, user, 1);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_merge(const void *left, size_t left_count,
	const void *right, size_t right_count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, int operation, void *out,
	size_t capacity, hx_os_array_progress *progress, hx_os_error *error)
{
	hx_os_status status = hx_os_array_output_check(
		left, left_count, element_size, out, capacity, element_size, 0);
	size_t a = 0, b = 0;

	hx_os_array_progress_clear(progress);
	if (status == HX_OS_OK)
		status = hx_os_array_output_check(right, right_count,
			element_size, out, capacity, element_size, 0);
	if (status == HX_OS_OK &&
		(!compare || !progress || operation < HX_OS_MERGE ||
			operation > HX_OS_SET_SYMMETRIC_DIFFERENCE))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	while (a < left_count || b < right_count) {
		const void *av = a < left_count
			? hx_os_array_const_at(left, element_size, a)
			: NULL;
		const void *bv = b < right_count
			? hx_os_array_const_at(right, element_size, b)
			: NULL;
		const void *chosen = NULL;
		int order = !av ? 1 : !bv ? -1 : compare(av, bv, user);
		int advance_a = order <= 0, advance_b = order >= 0;

		if (operation == HX_OS_MERGE) {
			chosen = order <= 0 ? av : bv;
			advance_b = !advance_a;
		}
		else if (order < 0 && operation != HX_OS_SET_INTERSECTION)
			chosen = av;
		else if (order > 0 &&
			(operation == HX_OS_SET_UNION ||
				operation == HX_OS_SET_SYMMETRIC_DIFFERENCE))
			chosen = bv;
		else if (!order &&
			(operation == HX_OS_SET_UNION ||
				operation == HX_OS_SET_INTERSECTION))
			chosen = av;

		if (chosen) {
			if (progress->written == capacity)
				return hx_os_i_error(
					error, HX_OS_OUTPUT_FULL, 0, 0);

			memcpy(hx_os_array_at(
				       out, element_size, progress->written),
				chosen, element_size);
			++progress->written;
		}

		a += advance_a;
		b += advance_b;
		progress->left_consumed = a;
		progress->right_consumed = b;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_array_scan(const void *data, size_t count,
	size_t element_size, void *accumulator, size_t accumulator_size,
	hx_os_value_fold_fn fold, void *user, int mode, void *out,
	size_t capacity, hx_os_array_progress *progress, hx_os_error *error)
{
	hx_os_status status = hx_os_array_accumulator_check(
		data, count, element_size, accumulator, accumulator_size);
	size_t i;

	hx_os_array_progress_clear(progress);
	if (status == HX_OS_OK)
		status = hx_os_array_output_check(data, count, element_size,
			out, capacity, accumulator_size, 0);
	if (status == HX_OS_OK &&
		(!fold || !progress ||
			(mode != HX_OS_SCAN_EXCLUSIVE &&
				mode != HX_OS_SCAN_INCLUSIVE)))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK &&
		hx_os_i_container_overlap(accumulator, accumulator_size, out,
			capacity * accumulator_size))
		status = HX_OS_INVALID;
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	for (i = 0; i < count; ++i) {
		hx_os_error detail;
		void *slot;

		if (i == capacity)
			return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

		slot = hx_os_array_at(out, accumulator_size, i);
		if (mode == HX_OS_SCAN_EXCLUSIVE)
			memcpy(slot, accumulator, accumulator_size);

		hx_os_error_clear(&detail);
		status = fold(accumulator,
			hx_os_array_const_at(data, element_size, i), user,
			&detail);
		if (status != HX_OS_OK)
			return hx_os_array_callback_error(
				status, &detail, error);
		if (mode == HX_OS_SCAN_INCLUSIVE)
			memcpy(slot, accumulator, accumulator_size);

		++progress->left_consumed;
		++progress->written;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
