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
