#ifndef HX_C_PLATFORM_ALGORITHM_H
#define HX_C_PLATFORM_ALGORITHM_H

#include "hx_container.h"
#include "hx_os_random.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fisher-Yates shuffle; NULL rng seeds a local noncryptographic generator
 * once from the OS. Allocation/seed failure leaves the array unchanged.
 * Rejection-sampled indices; no per-element allocation or system call.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_shuffle(void *data, size_t count,
	size_t element_size, hx_os_rng *rng, hx_os_error *error);
/* Uniform subset without replacement, preserving input order. Copies
 * min(count,capacity) byte-relocatable records; out must not overlap input.
 * Caller owns output storage. Failure before copying preserves out.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_sample(const void *data,
	size_t count, size_t element_size, void *out, size_t capacity,
	hx_os_rng *rng, size_t *written, hx_os_error *error);

/* Stable LSD integer radix sorts, O(width*count), ascending numeric order.
 * max_scratch_bytes=0 is 16 MiB, including allocation overhead. Initial
 * validation/allocation failure preserves data. Scratch is freed on return.
 */
#define HX_OS_DECLARE_RADIX(suffix, type)                                      \
	HX_OS_API hx_os_status HX_OS_CALL hx_os_radix_sort_##suffix(           \
		hx_os_context *context, type *data, size_t count,              \
		size_t max_scratch_bytes, hx_os_error *error)
HX_OS_DECLARE_RADIX(i32, int32_t);
HX_OS_DECLARE_RADIX(u32, uint32_t);
HX_OS_DECLARE_RADIX(i64, int64_t);
HX_OS_DECLARE_RADIX(u64, uint64_t);
#undef HX_OS_DECLARE_RADIX

/* Algorithms for ordinary contiguous C arrays; no container is required.
 * element_size is the complete stride, normally sizeof(array[0]). Elements
 * must support byte relocation. No copy/drop or ownership callbacks run.
 * data may be NULL only for count=0; element_size must be nonzero. Callers
 * provide valid storage and exclude concurrent mutation. Output indices and
 * counts must not alias array storage. Comparators define a strict weak order;
 * comparators/predicates must not modify inputs. Callbacks cannot throw or
 * longjmp. User data is borrowed during the call. Failed validation/allocation
 * preserves the range; ordinary query outputs stay unchanged, while progress
 * structures and completed counts are reset to zero on entry.
 * With CONTAINERS disabled these functions return NOT_SUPPORTED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_sort(void *data, size_t count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	hx_os_error *error);

/* Stable bottom-up merge. max_scratch_bytes=0 selects 16 MiB, including
 * library allocation overhead. A NULL context uses the default allocator.
 * Scratch is released before return; exceeding the budget returns LIMIT.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_stable_sort(
	hx_os_context *context, void *data, size_t count, size_t element_size,
	hx_os_value_compare_fn compare, void *user, size_t max_scratch_bytes,
	hx_os_error *error);

/* Sorted input is required. Bounds return an insertion index in [0,count].
 * binary_search returns OK with found=0 for an absent key. Keys may borrow
 * the input range; key must be non-NULL even when count=0.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_lower_bound(const void *data,
	size_t count, size_t element_size, const void *key,
	hx_os_value_compare_fn compare, void *user, size_t *index,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_upper_bound(const void *data,
	size_t count, size_t element_size, const void *key,
	hx_os_value_compare_fn compare, void *user, size_t *index,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_binary_search(const void *data,
	size_t count, size_t element_size, const void *key,
	hx_os_value_compare_fn compare, void *user, int *found,
	hx_os_error *error);

/* find_if returns NOT_FOUND at exhaustion. minmax returns NOT_FOUND for an
 * empty range and selects the first minimum and first maximum on ties.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_find_if(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, size_t *index, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_count_if(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, size_t *matches, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_minmax(const void *data,
	size_t count, size_t element_size, hx_os_value_compare_fn compare,
	void *user, size_t *minimum, size_t *maximum, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_is_sorted(const void *data,
	size_t count, size_t element_size, hx_os_value_compare_fn compare,
	void *user, int *sorted, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_reverse(
	void *data, size_t count, size_t element_size, hx_os_error *error);

/* Rotate left; left_count is reduced modulo count for nonempty ranges. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_rotate(void *data, size_t count,
	size_t element_size, size_t left_count, hx_os_error *error);

/* Unstable partition; boundary is the number of matching records. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_partition(void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, size_t *boundary, hx_os_error *error);

/* Compact adjacent equivalent records into [0,*remaining). The tail is
 * unspecified and must not be treated as additional owned values. No dropped
 * resources are released: use sequence_unique for values needing drop.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_unique(void *data, size_t count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	size_t *remaining, hx_os_error *error);

/* Sort the smallest prefix_count records; suffix order is unspecified.
 * prefix_count=0 does no work. Neither selection operation is stable.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_partial_sort(void *data,
	size_t count, size_t element_size, size_t prefix_count,
	hx_os_value_compare_fn compare, void *user, hx_os_error *error);

/* Put the sorted-rank element at index and partition the remaining records.
 * Uses one aligned pivot record; the scratch budget has stable_sort semantics.
 * Depth exhaustion falls back to heapsort, bounding work by O(n log n).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_nth_element(
	hx_os_context *context, void *data, size_t count, size_t element_size,
	size_t index, hx_os_value_compare_fn compare, void *user,
	size_t max_scratch_bytes, hx_os_error *error);

/* Integer shortcuts use numeric order and require sorted input for search. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sort_i32(
	int32_t *values, size_t count, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_sort_u32(
	uint32_t *values, size_t count, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_sort_i64(
	int64_t *values, size_t count, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_sort_u64(
	uint64_t *values, size_t count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_binary_search_i32(const int32_t *values,
	size_t count, int32_t key, int *found, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_binary_search_u32(
	const uint32_t *values, size_t count, uint32_t key, int *found,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_binary_search_i64(const int64_t *values,
	size_t count, int64_t key, int *found, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_binary_search_u64(
	const uint64_t *values, size_t count, uint64_t key, int *found,
	hx_os_error *error);

/* Counts successful records, not callbacks attempted. Entry resets progress
 * to zero, including on validation failure. OUTPUT_FULL retains the prefix;
 * resume from consumed input positions. Single-input algorithms leave
 * right_consumed=0.
 * No output capacity is inferred and no implicit allocation/deep copy occurs.
 * Output buffers cannot overlap inputs except exact, equal-stride transform.
 * Progress/count outputs must not alias input/output/accumulator storage.
 */
typedef struct hx_os_array_progress {
	size_t left_consumed;
	size_t right_consumed;
	size_t written;
} hx_os_array_progress;

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_copy_if(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, void *out, size_t capacity, hx_os_array_progress *progress,
	hx_os_error *error);

/* The callback owns cleanup of a partially constructed failing record.
 * That record may be modified but is excluded from progress. Exact in-place
 * transforms may likewise leave their failing input record modified.
 * OUTPUT_FULL with written==capacity comes from the destination bound;
 * with written<capacity it came from the callback and needs its cleanup.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_transform(const void *data,
	size_t count, size_t element_size, hx_os_value_transform_fn transform,
	void *user, void *out, size_t capacity, size_t output_element_size,
	hx_os_array_progress *progress, hx_os_error *error);

/* Stable compaction; remaining is the valid prefix length. Tail and owned
 * resource restrictions are the same as array_unique. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_remove_if(void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, size_t *remaining, hx_os_error *error);

/* replacement must be outside the input range. Plain bytes are copied;
 * replaced counts matching records, without clone/drop or allocation. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_replace_if(void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, const void *replacement, size_t *replaced,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_any_of(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, int *result, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_all_of(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, int *result, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_none_of(const void *data,
	size_t count, size_t element_size, hx_os_value_predicate_fn predicate,
	void *user, int *result, hx_os_error *error);

/* Sequential left fold. Accumulator must not overlap the input. On callback
 * error retain its changes, including partial changes in the failing call;
 * completed counts successful callbacks. Arithmetic overflow belongs to the
 * callback's explicit policy; no reassociation or parallel execution occurs.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_fold(const void *data,
	size_t count, size_t element_size, void *accumulator,
	size_t accumulator_size, hx_os_value_fold_fn fold, void *user,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_equal(const void *left,
	size_t left_count, const void *right, size_t right_count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	int *equal, hx_os_error *error);

/* Returns the first unequal index or min(left_count,right_count) if their
 * common prefix is equal. Different lengths alone are an equality mismatch.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_mismatch(const void *left,
	size_t left_count, const void *right, size_t right_count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	size_t *index, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_array_equal_range(const void *data,
	size_t count, size_t element_size, const void *key,
	hx_os_value_compare_fn compare, void *user, size_t *first, size_t *last,
	hx_os_error *error);

/* Sorted inputs under the same comparator. Uses HX_OS_MERGE/SET_* modes;
 * equivalent records retain left-before-right order in plain MERGE, and set
 * modes retain multiset counts. Output must not overlap either input.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_merge(const void *left,
	size_t left_count, const void *right, size_t right_count,
	size_t element_size, hx_os_value_compare_fn compare, void *user,
	int operation, void *out, size_t capacity,
	hx_os_array_progress *progress, hx_os_error *error);

enum { HX_OS_SCAN_EXCLUSIVE, HX_OS_SCAN_INCLUSIVE };

/* Prefix fold snapshots, with a caller-initialized accumulator. Output
 * records are accumulator_size raw bytes. All three ranges must be disjoint.
 * EXCLUSIVE copies the pre-fold state; INCLUSIVE copies the post-fold state.
 * Callback failure has fold semantics and may touch the uncounted output
 * slot. OUTPUT_FULL does not fold the unconsumed item. Pointer fields in
 * snapshots remain borrowed; no cloning of the referenced resource occurs.
 * As with transform, written<capacity distinguishes callback OUTPUT_FULL.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_array_scan(const void *data,
	size_t count, size_t element_size, void *accumulator,
	size_t accumulator_size, hx_os_value_fold_fn fold, void *user, int mode,
	void *out, size_t capacity, hx_os_array_progress *progress,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
