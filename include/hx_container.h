#ifndef HX_C_PLATFORM_CONTAINERS_H
#define HX_C_PLATFORM_CONTAINERS_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef hx_os_status(HX_OS_CALL *hx_os_value_copy_fn)(
	void *destination, const void *source, void *user);

typedef void(HX_OS_CALL *hx_os_value_drop_fn)(void *value, void *user);
typedef int(HX_OS_CALL *hx_os_value_compare_fn)(
	const void *left, const void *right, void *user);
typedef int(HX_OS_CALL *hx_os_key_compare_fn)(const void *left,
	size_t left_bytes, const void *right, size_t right_bytes, void *user);

typedef struct hx_os_container_options {
	uint32_t size;
	uint32_t version;
	size_t element_size;
	size_t element_alignment;
	size_t max_items;
	size_t max_bytes;
	hx_os_value_copy_fn copy;
	hx_os_value_drop_fn drop;
	hx_os_value_compare_fn compare;
	void *user;
} hx_os_container_options;

/*
 * Values must be bitwise relocatable. copy constructs an independent value;
 * on failure it must leave no owned resources in destination. drop releases
 * a live value. Callbacks cannot throw, longjmp or mutate this container.
 * Without copy/drop, insertion copies plain bytes. With drop but no copy,
 * only the explicit _take insertions are supported.
 * _take transfers the release obligation on success only; source bytes are
 * unchanged and must then be treated as moved-from. Its source must not be
 * an owned value in this container. A non-NULL pop/erase output takes the
 * value without drop; NULL discards it with drop. Output storage must be
 * uninitialized and outside all container-owned storage.
 * Containers have no internal locks: serialize mutation and exclude readers
 * during mutation/destruction. Failed mutation preserves logical contents,
 * but reserve/capacity may change. Iterators expire on successful structural
 * mutation. Iterators and borrowed pointers cannot outlive their container.
 */
HX_OS_API void HX_OS_CALL hx_os_container_options_init(
	hx_os_container_options *options, size_t element_size);

/* Deep-copy/drop preset for hx_string; context is borrowed for this lifetime.
 */
HX_OS_API void HX_OS_CALL hx_os_string_container_options_init(
	hx_os_container_options *options, hx_os_context *context);

typedef struct hx_os_sequence hx_os_sequence;
typedef hx_os_sequence hx_os_vector;
typedef hx_os_sequence hx_os_deque;
typedef hx_os_sequence hx_os_list;
typedef hx_os_sequence hx_os_heap;
typedef hx_os_sequence hx_os_stack;
typedef hx_os_sequence hx_os_queue;
typedef hx_os_sequence hx_os_byte_queue;

/* Byte FIFO backed by the existing deque. max_bytes=0 selects 64 KiB of
 * queued payload. Storage grows on demand; growth may temporarily retain
 * old and new buffers, bounded by twice the payload limit plus metadata.
 * No copy/drop callbacks. External synchronization is required.
 * write copies the entire input or leaves contents unchanged. read/peek
 * require the entire requested range, otherwise NEED_INPUT without mutation.
 * NULL read output discards bytes; peek output must be non-NULL for bytes>0.
 * Input/output must not overlap queue-owned storage. Offsets are bytes.
 * Successful nonempty writes/reads invalidate existing container iterators.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_byte_queue_create(
	hx_os_context *context, size_t max_bytes, hx_os_byte_queue **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_byte_queue_write(
	hx_os_byte_queue *queue, const void *data, size_t bytes,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_byte_queue_read(
	hx_os_byte_queue *queue, void *data, size_t bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_byte_queue_peek(
	const hx_os_byte_queue *queue, size_t offset, void *data, size_t bytes,
	hx_os_error *error);

#define hx_os_byte_queue_size hx_os_sequence_size
#define hx_os_byte_queue_destroy hx_os_sequence_destroy

HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_vector **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_deque_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_deque **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_list_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_list **out,
	hx_os_error *error);

/* A max-heap under options.compare; compare is required. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_heap_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_heap **out,
	hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_sequence_size(const hx_os_sequence *sequence);

HX_OS_API size_t HX_OS_CALL hx_os_sequence_capacity(
	const hx_os_sequence *sequence);

/* Library allocation bytes, including metadata, excluding callback allocations.
 */
HX_OS_API size_t HX_OS_CALL hx_os_sequence_memory(
	const hx_os_sequence *sequence);

HX_OS_API const void *HX_OS_CALL hx_os_sequence_at(
	const hx_os_sequence *sequence, size_t index);

/* Only vector/stack exposes a contiguous mutable array; others return NULL. */
HX_OS_API void *HX_OS_CALL hx_os_vector_data(hx_os_vector *vector);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_reserve(
	hx_os_sequence *sequence, size_t capacity, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_insert(
	hx_os_sequence *sequence, size_t index, const void *value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_insert_take(
	hx_os_sequence *sequence, size_t index, void *value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_push_back(
	hx_os_sequence *sequence, const void *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_push_back_take(
	hx_os_sequence *sequence, void *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_push_front(
	hx_os_sequence *sequence, const void *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_pop_back(
	hx_os_sequence *sequence, void *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_pop_front(
	hx_os_sequence *sequence, void *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_erase(
	hx_os_sequence *sequence, size_t index, void *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_clear(
	hx_os_sequence *sequence, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_destroy(
	hx_os_sequence **sequence, hx_os_error *error);

/* Vector-only in-place heapsort. Comparator must define a strict weak order. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_sort(
	hx_os_vector *vector, hx_os_error *error);

/* Requires vector sorted by compare; returns insertion position and exact
 * match. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_lower_bound(
	const hx_os_vector *vector, const void *key, size_t *index, int *found);

#define hx_os_stack_create hx_os_vector_create
#define hx_os_stack_push hx_os_sequence_push_back
#define hx_os_stack_pop hx_os_sequence_pop_back
#define hx_os_queue_create hx_os_deque_create
#define hx_os_queue_push hx_os_sequence_push_back
#define hx_os_queue_pop hx_os_sequence_pop_front
#define hx_os_heap_push hx_os_sequence_push_back
#define hx_os_heap_pop hx_os_sequence_pop_front
#define hx_os_vector_push_back hx_os_sequence_push_back
#define hx_os_vector_pop_back hx_os_sequence_pop_back

typedef struct hx_os_sequence_iterator {
	const hx_os_sequence *owner;
	const void *link;
	size_t index;
	uint64_t generation;
} hx_os_sequence_iterator;

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_begin(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_next(
	hx_os_sequence_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_value(
	const hx_os_sequence_iterator *iterator, const void **value);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_last(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_previous(
	hx_os_sequence_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_erase_at(
	hx_os_sequence *sequence, hx_os_sequence_iterator *iterator, void *out,
	hx_os_error *error);

typedef int(HX_OS_CALL *hx_os_value_predicate_fn)(
	const void *value, void *user);

typedef hx_os_status(HX_OS_CALL *hx_os_value_visit_fn)(
	const void *value, void *user, hx_os_error *error);

/* Predicates are synchronous, non-failing and must not mutate the container.
 * find_if returns NOT_FOUND at exhaustion; count_if succeeds with zero.
 * visit counts callbacks started, EOF stops successfully; other statuses keep
 * callback details. Callback effects are not rolled back on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_find_if(
	const hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, hx_os_sequence_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_count_if(
	const hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, size_t *count);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_visit(hx_os_sequence *sequence,
	hx_os_value_visit_fn visit, void *user, size_t *completed,
	hx_os_error *error);

/* Stable linear filtering; discarded values are dropped once. unique removes
 * adjacent equivalents using options.compare. Reordering/filtering heap
 * containers is NOT_SUPPORTED. reverse/rotate do not allocate. stable_sort
 * uses O(n) scratch bytes for arrays and O(1) scratch for lists; it preserves
 * equal-element order. Allocation failure preserves contents and iterators.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_remove_if(
	hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, size_t *removed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_unique(
	hx_os_sequence *sequence, size_t *removed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_reverse(
	hx_os_sequence *sequence, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_rotate(
	hx_os_sequence *sequence, size_t left_count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_stable_sort(
	hx_os_sequence *sequence, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_upper_bound(
	const hx_os_vector *vector, const void *key, size_t *index);

/* Move [first, first+count) before destination's position. Lists must share
 * context, element layout and copy/drop/compare/user policy. No allocation,
 * copy or drop. Capacity/memory limits still apply. Self-splice into its own
 * range is a no-op. Other successful moves invalidate both sets of iterators.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_list_splice(hx_os_list *destination,
	size_t position, hx_os_list *source, size_t first, size_t count,
	hx_os_error *error);

/* Short-circuit predicates; empty sequences satisfy all_of and none_of. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_any_of(
	const hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, int *result);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_all_of(
	const hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, int *result);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_none_of(
	const hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, int *result);

/* Linear, no allocation. Lists retain relative order; vectors/deques do not
 * promise stability. boundary is the count of matching elements. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_partition(
	hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, size_t *boundary, hx_os_error *error);

/* Clone replacement once to permit an aliased source value, then copy into
 * each match. First copy failure stops and keeps prior replacements; replaced
 * reports progress. Scratch uses two elements charged to the byte limit.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_replace_if(
	hx_os_sequence *sequence, hx_os_value_predicate_fn predicate,
	void *user, const void *replacement, size_t *replaced,
	hx_os_error *error);

/* Sorted prefix [0,count); the remaining suffix has unspecified order.
 * nth_element puts the element of sorted rank index at that index and
 * partitions the rest. Neither algorithm changes element ownership.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_partial_sort(
	hx_os_vector *vector, size_t count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_vector_nth_element(
	hx_os_vector *vector, size_t index, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_value_transform_fn)(
	void *destination, const void *source, void *user, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_value_fold_fn)(
	void *accumulator, const void *value, void *user, hx_os_error *error);

/* New owned vectors inherit context/options from source unless transform
 * supplies a different destination policy. Output is NULL on failure; source
 * stays intact. transform constructs one destination value and must clean up
 * its partial construction on failure. All callbacks run synchronously.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_clone(
	const hx_os_sequence *source, hx_os_vector **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_copy_if(
	const hx_os_sequence *source, hx_os_value_predicate_fn predicate,
	void *user, hx_os_vector **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_transform(
	const hx_os_sequence *source, const hx_os_container_options *options,
	hx_os_value_transform_fn transform, void *user, hx_os_vector **out,
	hx_os_error *error);

/* fold updates a caller-owned accumulator; completed counts callbacks
 * started. First error stops, retaining prior accumulator changes. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_fold(
	const hx_os_sequence *source, void *accumulator,
	hx_os_value_fold_fn fold, void *user, size_t *completed,
	hx_os_error *error);

enum {
	HX_OS_MERGE,
	HX_OS_SET_UNION,
	HX_OS_SET_INTERSECTION,
	HX_OS_SET_DIFFERENCE,
	HX_OS_SET_SYMMETRIC_DIFFERENCE
};

/* Sorted inputs must have identical element/callback/user policy. Result
 * inherits left's context and limits. Multiset counts follow max/min/subtract
 * rules. Merge is stable with left preceding right for equivalent elements.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sequence_merge(
	const hx_os_sequence *left, const hx_os_sequence *right, int operation,
	hx_os_vector **out, hx_os_error *error);

typedef struct hx_os_map hx_os_map;
typedef hx_os_map hx_os_set;

enum {
	HX_OS_MAP_HASH,
	HX_OS_MAP_ORDERED,
	HX_OS_MAP_RED_BLACK,
	HX_OS_MAP_MULTI = 16
};

typedef struct hx_os_map_iterator {
	const hx_os_map *owner;
	const void *entry;
	uint64_t generation;
} hx_os_map_iterator;

/*
 * Keys are copied bytes (including zero length/NUL); hash keys use byte
 * equality. Ordered maps use key_compare or unsigned-byte lexicographic order.
 * value_options.element_size=0 creates a set; value/copy/drop then are NULL.
 * Ordered lookups/inserts/erases are O(log n); hash iteration is insertion
 * order. RED_BLACK selects a red-black tree; ORDERED retains AVL behavior.
 * OR a kind with MULTI to allow duplicate keys. insert(replace=0) then adds
 * another entry; replace=1 replaces one existing entry. Ordered duplicates
 * retain insertion order. find/erase choose the first ordered equivalent;
 * hash duplicates have unspecified lookup order.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_create(hx_os_context *context,
	int kind, const hx_os_container_options *value_options,
	hx_os_key_compare_fn key_compare, hx_os_map **out, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_map_size(const hx_os_map *map);

HX_OS_API size_t HX_OS_CALL hx_os_map_memory(const hx_os_map *map);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_insert(hx_os_map *map,
	const void *key, size_t key_bytes, const void *value, int replace,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_insert_take(hx_os_map *map,
	const void *key, size_t key_bytes, void *value, int replace,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_find(const hx_os_map *map,
	const void *key, size_t key_bytes, const void **value);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_erase(hx_os_map *map,
	const void *key, size_t key_bytes, void *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_begin(
	const hx_os_map *map, hx_os_map_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_next(hx_os_map_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_entry(
	const hx_os_map_iterator *iterator, const void **key, size_t *key_bytes,
	const void **value);

/* Bounds are ordered-only, O(log n), and produce EOF at the end. Equal range
 * returns OK even for an empty range; last is excluded. Duplicate counting
 * is O(log n + matches) for trees, O(bucket entries) for hash maps.
 * Last/previous follow reverse iteration, including insertion order in hashes.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_lower_bound(const hx_os_map *map,
	const void *key, size_t bytes, hx_os_map_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_upper_bound(const hx_os_map *map,
	const void *key, size_t bytes, hx_os_map_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_equal_range(const hx_os_map *map,
	const void *key, size_t bytes, hx_os_map_iterator *first,
	hx_os_map_iterator *last);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_count(
	const hx_os_map *map, const void *key, size_t bytes, size_t *count);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_last(
	const hx_os_map *map, hx_os_map_iterator *iterator);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_previous(
	hx_os_map_iterator *iterator);

/* On success erase_at advances iterator to the following entry and refreshes
 * its generation. Other iterators expire. out follows map_erase ownership.
 * erase_all discards all equivalents; a missing key succeeds with count zero.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_erase_at(hx_os_map *map,
	hx_os_map_iterator *iterator, void *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_erase_all(hx_os_map *map,
	const void *key, size_t bytes, size_t *count, hx_os_error *error);

/* Read-only logical invariant check for diagnostics/tests; O(n), no allocation.
 * This does not validate arbitrary damaged pointers or replace memory tools. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_validate(const hx_os_map *map);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_clear(
	hx_os_map *map, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_map_destroy(
	hx_os_map **map, hx_os_error *error);

/* Declare iterator and status first. Container is evaluated once. Normal
 * exhaustion leaves EOF; break leaves OK; structural mutation reports INVALID
 * on the next step. These macros introduce one loop, so break/continue work.
 */
#define HX_OS_SEQUENCE_FOR_EACH(iterator, container, status)                   \
	for ((status) = hx_os_sequence_begin((container), &(iterator));        \
		(status) == HX_OS_OK;                                          \
		(status) = hx_os_sequence_next(&(iterator)))
#define HX_OS_MAP_FOR_EACH(iterator, container, status)                        \
	for ((status) = hx_os_map_begin((container), &(iterator));             \
		(status) == HX_OS_OK; (status) = hx_os_map_next(&(iterator)))
#define HX_OS_MAP_FOR_EACH_REVERSE(iterator, container, status)                \
	for ((status) = hx_os_map_last((container), &(iterator));              \
		(status) == HX_OS_OK;                                          \
		(status) = hx_os_map_previous(&(iterator)))

#define HX_OS_SEQUENCE_FOR_EACH_REVERSE(iterator, container, status)           \
	for ((status) = hx_os_sequence_last((container), &(iterator));         \
		(status) == HX_OS_OK;                                          \
		(status) = hx_os_sequence_previous(&(iterator)))

/* Thin typed adapters for trivially byte-copyable values, not owning strings
 * or nontrivial C++ objects. name must be a lowercase C identifier.
 * Generated static functions reuse the same C sequence implementation.
 * max_items=0 keeps the existing default bound. Views expire on mutation.
 */
#define HX_OS_DECLARE_POD_SEQUENCE(name, type, factory, popper)                \
	typedef hx_os_sequence name;                                           \
	typedef type name##_value_type;                                        \
	typedef struct name##_alignment {                                      \
		char prefix;                                                   \
		name##_value_type value;                                       \
	} name##_alignment;                                                    \
	static inline hx_os_status name##_create(hx_os_context *context,       \
		size_t max_items, name **out, hx_os_error *error)              \
	{                                                                      \
		hx_os_container_options options;                               \
		hx_os_container_options_init(&options, sizeof(type));          \
		options.element_alignment = offsetof(name##_alignment, value); \
		if (max_items)                                                 \
			options.max_items = max_items;                         \
		return factory(context, &options, out, error);                 \
	}                                                                      \
	static inline hx_os_status name##_push(                                \
		name *container, name##_value_type value, hx_os_error *error)  \
	{                                                                      \
		return hx_os_sequence_push_back(                               \
			container, (const void *)&value, error);               \
	}                                                                      \
	static inline hx_os_status name##_pop(                                 \
		name *container, name##_value_type *out, hx_os_error *error)   \
	{                                                                      \
		return popper(container, (void *)out, error);                  \
	}                                                                      \
	static inline size_t name##_size(const name *container)                \
	{                                                                      \
		return hx_os_sequence_size(container);                         \
	}                                                                      \
	static inline const name##_value_type *name##_at(                      \
		const name *container, size_t index)                           \
	{                                                                      \
		return (const name##_value_type *)hx_os_sequence_at(           \
			container, index);                                     \
	}                                                                      \
	static inline hx_os_status name##_destroy(                             \
		name **container, hx_os_error *error)                          \
	{                                                                      \
		return hx_os_sequence_destroy(container, error);               \
	}
#define HX_OS_DECLARE_VECTOR(name, type)                                       \
	HX_OS_DECLARE_POD_SEQUENCE(                                            \
		name, type, hx_os_vector_create, hx_os_sequence_pop_back)
#define HX_OS_DECLARE_QUEUE(name, type)                                        \
	HX_OS_DECLARE_POD_SEQUENCE(                                            \
		name, type, hx_os_deque_create, hx_os_sequence_pop_front)

#ifdef __cplusplus
}
#endif
#endif
