#include "containers_private.h"

hx_os_status hx_os_vector_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_vector **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	(void)error;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_deque_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_deque **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	(void)error;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_list_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_list **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	(void)error;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_heap_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_heap **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	(void)error;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_sequence_size(const hx_os_sequence *sequence)
{
	(void)sequence;

	return 0;
}

size_t hx_os_sequence_capacity(const hx_os_sequence *sequence)
{
	(void)sequence;

	return 0;
}

size_t hx_os_sequence_memory(const hx_os_sequence *sequence)
{
	(void)sequence;

	return 0;
}

const void *hx_os_sequence_at(const hx_os_sequence *sequence, size_t index)
{
	(void)sequence;
	(void)index;

	return NULL;
}

void *hx_os_vector_data(hx_os_vector *vector)
{
	(void)vector;

	return NULL;
}

hx_os_status hx_os_sequence_reserve(
	hx_os_sequence *sequence, size_t capacity, hx_os_error *error)
{
	(void)sequence;
	(void)capacity;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_insert(hx_os_sequence *sequence, size_t index,
	const void *value, hx_os_error *error)
{
	(void)sequence;
	(void)index;
	(void)value;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_insert_take(
	hx_os_sequence *sequence, size_t index, void *value, hx_os_error *error)
{
	(void)sequence;
	(void)index;
	(void)value;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_push_back(
	hx_os_sequence *sequence, const void *value, hx_os_error *error)
{
	(void)sequence;
	(void)value;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_push_back_take(
	hx_os_sequence *sequence, void *value, hx_os_error *error)
{
	(void)sequence;
	(void)value;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_push_front(
	hx_os_sequence *sequence, const void *value, hx_os_error *error)
{
	(void)sequence;
	(void)value;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_pop_back(
	hx_os_sequence *sequence, void *out, hx_os_error *error)
{
	(void)sequence;
	(void)out;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_pop_front(
	hx_os_sequence *sequence, void *out, hx_os_error *error)
{
	(void)sequence;
	(void)out;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_erase(
	hx_os_sequence *sequence, size_t index, void *out, hx_os_error *error)
{
	(void)sequence;
	(void)index;
	(void)out;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_clear(hx_os_sequence *sequence, hx_os_error *error)
{
	(void)sequence;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_destroy(
	hx_os_sequence **sequence, hx_os_error *error)
{
	(void)sequence;
	(void)error;
	if (sequence && !*sequence)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_vector_sort(hx_os_vector *vector, hx_os_error *error)
{
	(void)vector;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_vector_lower_bound(
	const hx_os_vector *vector, const void *key, size_t *index, int *found)
{
	(void)vector;
	(void)key;
	(void)index;
	(void)found;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_begin(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator)
{
	(void)sequence;
	(void)iterator;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_next(hx_os_sequence_iterator *iterator)
{
	(void)iterator;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_value(
	const hx_os_sequence_iterator *iterator, const void **value)
{
	(void)iterator;
	(void)value;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_create(hx_os_context *context, int kind,
	const hx_os_container_options *value_options,
	hx_os_key_compare_fn key_compare, hx_os_map **out, hx_os_error *error)
{
	(void)context;
	(void)kind;
	(void)value_options;
	(void)key_compare;
	(void)out;
	(void)error;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_map_size(const hx_os_map *map)
{
	(void)map;

	return 0;
}

size_t hx_os_map_memory(const hx_os_map *map)
{
	(void)map;

	return 0;
}

hx_os_status hx_os_map_insert(hx_os_map *map, const void *key, size_t key_bytes,
	const void *value, int replace, hx_os_error *error)
{
	(void)map;
	(void)key;
	(void)key_bytes;
	(void)value;
	(void)replace;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_map_insert_take(hx_os_map *map, const void *key,
	size_t key_bytes, void *value, int replace, hx_os_error *error)
{
	(void)map;
	(void)key;
	(void)key_bytes;
	(void)value;
	(void)replace;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_map_find(const hx_os_map *map, const void *key,
	size_t key_bytes, const void **value)
{
	(void)map;
	(void)key;
	(void)key_bytes;
	(void)value;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_erase(hx_os_map *map, const void *key, size_t key_bytes,
	void *out, hx_os_error *error)
{
	(void)map;
	(void)key;
	(void)key_bytes;
	(void)out;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_map_begin(const hx_os_map *map, hx_os_map_iterator *iterator)
{
	(void)map;
	(void)iterator;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_next(hx_os_map_iterator *iterator)
{
	(void)iterator;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_entry(const hx_os_map_iterator *iterator,
	const void **key, size_t *key_bytes, const void **value)
{
	(void)iterator;
	(void)key;
	(void)key_bytes;
	(void)value;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_clear(hx_os_map *map, hx_os_error *error)
{
	(void)map;
	(void)error;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_map_destroy(hx_os_map **map, hx_os_error *error)
{
	(void)map;
	(void)error;
	if (map && !*map)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_map_lower_bound(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *iterator)
{
	(void)map;
	(void)key;
	(void)bytes;
	(void)iterator;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_map_upper_bound(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *iterator)
{
	return hx_os_map_lower_bound(map, key, bytes, iterator);
}

hx_os_status hx_os_map_equal_range(const hx_os_map *map, const void *key,
	size_t bytes, hx_os_map_iterator *first, hx_os_map_iterator *last)
{
	(void)last;

	return hx_os_map_lower_bound(map, key, bytes, first);
}

hx_os_status hx_os_map_count(
	const hx_os_map *map, const void *key, size_t bytes, size_t *count)
{
	(void)count;

	return hx_os_map_lower_bound(map, key, bytes, NULL);
}

hx_os_status hx_os_map_last(const hx_os_map *map, hx_os_map_iterator *iterator)
{
	return hx_os_map_begin(map, iterator);
}

hx_os_status hx_os_map_previous(hx_os_map_iterator *iterator)
{
	return hx_os_map_next(iterator);
}

hx_os_status hx_os_map_erase_at(hx_os_map *map, hx_os_map_iterator *iterator,
	void *out, hx_os_error *error)
{
	(void)iterator;

	return hx_os_map_erase(map, NULL, 0, out, error);
}

hx_os_status hx_os_map_erase_all(hx_os_map *map, const void *key, size_t bytes,
	size_t *count, hx_os_error *error)
{
	(void)count;

	return hx_os_map_erase(map, key, bytes, NULL, error);
}

hx_os_status hx_os_map_validate(const hx_os_map *map)
{
	(void)map;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_last(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator)
{
	return hx_os_sequence_begin(sequence, iterator);
}

hx_os_status hx_os_sequence_previous(hx_os_sequence_iterator *iterator)
{
	return hx_os_sequence_next(iterator);
}

hx_os_status hx_os_sequence_erase_at(hx_os_sequence *sequence,
	hx_os_sequence_iterator *iterator, void *out, hx_os_error *error)
{
	(void)iterator;

	return hx_os_sequence_erase(sequence, 0, out, error);
}

hx_os_status hx_os_sequence_find_if(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user,
	hx_os_sequence_iterator *iterator)
{
	(void)predicate;
	(void)user;

	return hx_os_sequence_begin(sequence, iterator);
}

hx_os_status hx_os_sequence_count_if(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *count)
{
	(void)count;

	return hx_os_sequence_find_if(sequence, predicate, user, NULL);
}

hx_os_status hx_os_sequence_visit(hx_os_sequence *sequence,
	hx_os_value_visit_fn visit, void *user, size_t *completed,
	hx_os_error *error)
{
	(void)sequence;
	(void)visit;
	(void)user;
	if (completed)
		*completed = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_remove_if(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *removed,
	hx_os_error *error)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)removed;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_unique(
	hx_os_sequence *sequence, size_t *removed, hx_os_error *error)
{
	return hx_os_sequence_remove_if(sequence, NULL, NULL, removed, error);
}

hx_os_status hx_os_sequence_reverse(
	hx_os_sequence *sequence, hx_os_error *error)
{
	return hx_os_sequence_unique(sequence, NULL, error);
}

hx_os_status hx_os_sequence_rotate(
	hx_os_sequence *sequence, size_t left_count, hx_os_error *error)
{
	(void)left_count;

	return hx_os_sequence_reverse(sequence, error);
}

hx_os_status hx_os_sequence_stable_sort(
	hx_os_sequence *sequence, hx_os_error *error)
{
	return hx_os_sequence_reverse(sequence, error);
}

hx_os_status hx_os_vector_upper_bound(
	const hx_os_vector *vector, const void *key, size_t *index)
{
	return hx_os_vector_lower_bound(vector, key, index, NULL);
}

hx_os_status hx_os_list_splice(hx_os_list *destination, size_t position,
	hx_os_list *source, size_t first, size_t count, hx_os_error *error)
{
	(void)source;
	(void)first;
	(void)count;

	return hx_os_sequence_rotate(destination, position, error);
}

hx_os_status hx_os_vector_partial_sort(
	hx_os_vector *vector, size_t count, hx_os_error *error)
{
	return hx_os_sequence_rotate(vector, count, error);
}

hx_os_status hx_os_vector_nth_element(
	hx_os_vector *vector, size_t index, hx_os_error *error)
{
	return hx_os_sequence_rotate(vector, index, error);
}

hx_os_status hx_os_sequence_clone(
	const hx_os_sequence *source, hx_os_vector **out, hx_os_error *error)
{
	(void)source;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_copy_if(const hx_os_sequence *source,
	hx_os_value_predicate_fn predicate, void *user, hx_os_vector **out,
	hx_os_error *error)
{
	(void)predicate;
	(void)user;

	return hx_os_sequence_clone(source, out, error);
}

hx_os_status hx_os_sequence_transform(const hx_os_sequence *source,
	const hx_os_container_options *options,
	hx_os_value_transform_fn transform, void *user, hx_os_vector **out,
	hx_os_error *error)
{
	(void)options;
	(void)transform;
	(void)user;

	return hx_os_sequence_clone(source, out, error);
}

hx_os_status hx_os_sequence_fold(const hx_os_sequence *source,
	void *accumulator, hx_os_value_fold_fn fold, void *user,
	size_t *completed, hx_os_error *error)
{
	(void)source;
	(void)accumulator;
	(void)fold;
	(void)user;
	if (completed)
		*completed = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_merge(const hx_os_sequence *left,
	const hx_os_sequence *right, int operation, hx_os_vector **out,
	hx_os_error *error)
{
	(void)right;
	(void)operation;

	return hx_os_sequence_clone(left, out, error);
}

hx_os_status hx_os_sequence_any_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)result;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_all_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)result;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_none_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)result;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_sequence_partition(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *boundary,
	hx_os_error *error)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)boundary;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_sequence_replace_if(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, const void *replacement,
	size_t *replaced, hx_os_error *error)
{
	(void)sequence;
	(void)predicate;
	(void)user;
	(void)replacement;
	(void)replaced;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
