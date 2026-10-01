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

typedef struct hx_os_map hx_os_map;
typedef hx_os_map hx_os_set;
enum { HX_OS_MAP_HASH, HX_OS_MAP_ORDERED };
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
 * order.
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
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_clear(
	hx_os_map *map, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_map_destroy(
	hx_os_map **map, hx_os_error *error);

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
		return hx_os_sequence_push_back(container, &value, error);     \
	}                                                                      \
	static inline hx_os_status name##_pop(                                 \
		name *container, name##_value_type *out, hx_os_error *error)   \
	{                                                                      \
		return popper(container, out, error);                          \
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
