#include "containers_private.h"

enum { HX_OS_SEQ_VECTOR, HX_OS_SEQ_DEQUE, HX_OS_SEQ_LIST, HX_OS_SEQ_HEAP };

typedef struct hx_os_seq_link {
	struct hx_os_seq_link *previous;
	struct hx_os_seq_link *next;
} hx_os_seq_link;

struct hx_os_sequence {
	hx_os_container_base base;
	int kind;
	size_t count;
	size_t capacity;
	size_t head;
	size_t value_offset;
	unsigned char *data;
	hx_os_seq_link *first;
	hx_os_seq_link *last;
};

static hx_os_seq_link *hx_os_seq_link_at(
	const hx_os_sequence *sequence, size_t index)
{
	hx_os_seq_link *link;
	if (index < sequence->count / 2) {
		link = sequence->first;
		while (index--)
			link = link->next;
	}
	else {
		size_t steps = sequence->count - index - 1;
		link = sequence->last;
		while (steps--)
			link = link->previous;
	}
	return link;
}

static unsigned char *hx_os_seq_slot(
	const hx_os_sequence *sequence, size_t index)
{
	if (sequence->kind == HX_OS_SEQ_LIST)
		return (unsigned char *)hx_os_seq_link_at(sequence, index) +
			sequence->value_offset;
	if (sequence->kind == HX_OS_SEQ_DEQUE)
		index = index >= sequence->capacity - sequence->head
			? index - (sequence->capacity - sequence->head)
			: sequence->head + index;
	return sequence->data + index * sequence->base.options.element_size;
}

static hx_os_status hx_os_seq_create(hx_os_context *context,
	const hx_os_container_options *options, int kind, hx_os_sequence **out,
	hx_os_error *error)
{
	hx_os_container_base base;
	hx_os_sequence *sequence;
	hx_os_status status;
	size_t alignment;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_i_container_init(
		&base, context, options, sizeof(*sequence), 0);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (kind == HX_OS_SEQ_HEAP && !options->compare)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	alignment = options->element_alignment;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;
	if (alignment > SIZE_MAX - sizeof(hx_os_seq_link))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	sequence = hx_os_i_zero(context, sizeof(*sequence));
	if (!sequence)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	sequence->base = base;
	sequence->kind = kind;
	sequence->value_offset =
		(sizeof(hx_os_seq_link) + alignment - 1) & ~(alignment - 1);
	*out = sequence;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_vector_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_vector **out,
	hx_os_error *error)
{
	return hx_os_seq_create(context, options, HX_OS_SEQ_VECTOR, out, error);
}

hx_os_status hx_os_deque_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_deque **out,
	hx_os_error *error)
{
	return hx_os_seq_create(context, options, HX_OS_SEQ_DEQUE, out, error);
}

hx_os_status hx_os_list_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_list **out,
	hx_os_error *error)
{
	return hx_os_seq_create(context, options, HX_OS_SEQ_LIST, out, error);
}

hx_os_status hx_os_heap_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_heap **out,
	hx_os_error *error)
{
	return hx_os_seq_create(context, options, HX_OS_SEQ_HEAP, out, error);
}

size_t hx_os_sequence_size(const hx_os_sequence *sequence)
{
	return sequence ? sequence->count : 0;
}

size_t hx_os_sequence_capacity(const hx_os_sequence *sequence)
{
	return sequence ? sequence->kind == HX_OS_SEQ_LIST ? sequence->count
							   : sequence->capacity
			: 0;
}

size_t hx_os_sequence_memory(const hx_os_sequence *sequence)
{
	return sequence ? sequence->base.memory : 0;
}

const void *hx_os_sequence_at(const hx_os_sequence *sequence, size_t index)
{
	return sequence && !sequence->base.busy && index < sequence->count
		? hx_os_seq_slot(sequence, index)
		: NULL;
}

void *hx_os_vector_data(hx_os_vector *vector)
{
	return vector && vector->kind == HX_OS_SEQ_VECTOR && !vector->base.busy
		? vector->data
		: NULL;
}

static hx_os_status hx_os_seq_reserve(hx_os_sequence *sequence, size_t capacity)
{
	size_t i, size = sequence->base.options.element_size;
	unsigned char *memory;
	if (sequence->kind == HX_OS_SEQ_LIST)
		return HX_OS_NOT_SUPPORTED;
	if (capacity <= sequence->capacity)
		return HX_OS_OK;
	if (capacity > sequence->base.options.max_items ||
		capacity > SIZE_MAX / size)
		return HX_OS_LIMIT;
	memory = hx_os_i_container_alloc(&sequence->base, capacity * size,
		sequence->base.options.element_alignment);
	if (!memory)
		return sequence->base.allocation_status;
	if (sequence->kind == HX_OS_SEQ_DEQUE)
		for (i = 0; i < sequence->count; ++i)
			memcpy(memory + i * size, hx_os_seq_slot(sequence, i),
				size);
	else if (sequence->count)
		memcpy(memory, sequence->data, sequence->count * size);
	hx_os_i_container_free(&sequence->base, sequence->data);
	sequence->data = memory;
	sequence->capacity = capacity;
	sequence->head = 0;
	return HX_OS_OK;
}

hx_os_status hx_os_sequence_reserve(
	hx_os_sequence *sequence, size_t capacity, hx_os_error *error)
{
	hx_os_status status;
	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_seq_reserve(sequence, capacity);
	if (status == HX_OS_OK)
		++sequence->base.generation;
	return hx_os_i_container_leave(&sequence->base, status, error);
}

static void hx_os_seq_swap(hx_os_sequence *sequence, size_t a, size_t b)
{
	unsigned char *left = hx_os_seq_slot(sequence, a);
	unsigned char *right = hx_os_seq_slot(sequence, b);
	size_t i;
	for (i = 0; i < sequence->base.options.element_size; ++i) {
		unsigned char byte = left[i];
		left[i] = right[i];
		right[i] = byte;
	}
}

static int hx_os_seq_compare(
	const hx_os_sequence *sequence, const void *a, const void *b)
{
	return sequence->base.options.compare(
		a, b, sequence->base.options.user);
}

static void hx_os_seq_sift(hx_os_sequence *sequence, size_t root, size_t count)
{
	while (root < count / 2) {
		size_t child = root * 2 + 1;
		if (child + 1 < count &&
			hx_os_seq_compare(sequence,
				hx_os_seq_slot(sequence, child),
				hx_os_seq_slot(sequence, child + 1)) < 0)
			++child;
		if (hx_os_seq_compare(sequence, hx_os_seq_slot(sequence, root),
			    hx_os_seq_slot(sequence, child)) >= 0)
			break;
		hx_os_seq_swap(sequence, root, child);
		root = child;
	}
}

static hx_os_status hx_os_seq_add(
	hx_os_sequence *sequence, size_t index, const void *value, int take)
{
	hx_os_container_base *base = &sequence->base;
	hx_os_status status;
	void *temporary = NULL;
	const void *source = value;
	size_t i, size = base->options.element_size;
	int copied = 0;
	if (index > sequence->count)
		return HX_OS_RANGE;
	if (sequence->kind == HX_OS_SEQ_HEAP && index != sequence->count)
		return HX_OS_NOT_SUPPORTED;
	if (sequence->count == base->options.max_items)
		return HX_OS_LIMIT;
	if (!take && base->options.drop && !base->options.copy)
		return HX_OS_NOT_SUPPORTED;
	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *next, *link;
		size_t bytes;
		if (!hx_os_i_add_size(sequence->value_offset, size, &bytes))
			return HX_OS_LIMIT;
		link = hx_os_i_container_alloc(
			base, bytes, base->options.element_alignment);
		if (!link)
			return base->allocation_status;
		status = hx_os_i_container_copy(base,
			(unsigned char *)link + sequence->value_offset, value,
			take);
		if (status != HX_OS_OK) {
			hx_os_i_container_free(base, link);
			return status;
		}
		next = index == sequence->count
			? NULL
			: hx_os_seq_link_at(sequence, index);
		link->next = next;
		link->previous = next ? next->previous : sequence->last;
		if (link->previous)
			link->previous->next = link;
		else
			sequence->first = link;
		if (next)
			next->previous = link;
		else
			sequence->last = link;
		++sequence->count;
		return HX_OS_OK;
	}
	if (hx_os_i_container_overlap(
		    value, size, sequence->data, sequence->capacity * size)) {
		if (take && base->options.drop)
			return HX_OS_INVALID;
		temporary = hx_os_i_container_alloc(
			base, size, base->options.element_alignment);
		if (!temporary)
			return base->allocation_status;
		status = hx_os_i_container_copy(base, temporary, value, take);
		if (status != HX_OS_OK)
			goto done;
		source = temporary;
		copied = !take && base->options.copy != NULL;
	}
	else if (!take && base->options.copy) {
		temporary = hx_os_i_container_alloc(
			base, size, base->options.element_alignment);
		if (!temporary)
			return base->allocation_status;
		status = hx_os_i_container_copy(base, temporary, value, 0);
		if (status != HX_OS_OK)
			goto done;
		copied = 1;
		source = temporary;
	}
	if (sequence->count == sequence->capacity) {
		size_t capacity = sequence->capacity;
		if (!capacity)
			capacity = base->options.max_items < 8
				? base->options.max_items
				: 8;
		else if (capacity > base->options.max_items / 2)
			capacity = base->options.max_items;
		else
			capacity *= 2;
		status = hx_os_seq_reserve(sequence, capacity);
		if (status != HX_OS_OK) {
			if (copied)
				hx_os_i_container_drop(base, temporary);
			goto done;
		}
	}
	if (sequence->kind == HX_OS_SEQ_DEQUE && index < sequence->count / 2) {
		sequence->head = sequence->head ? sequence->head - 1
						: sequence->capacity - 1;
		for (i = 0; i < index; ++i)
			memcpy(hx_os_seq_slot(sequence, i),
				hx_os_seq_slot(sequence, i + 1), size);
	}
	else {
		for (i = sequence->count; i > index; --i)
			memcpy(hx_os_seq_slot(sequence, i),
				hx_os_seq_slot(sequence, i - 1), size);
	}
	memcpy(hx_os_seq_slot(sequence, index), source, size);
	++sequence->count;
	if (sequence->kind == HX_OS_SEQ_HEAP) {
		i = sequence->count - 1;
		while (i) {
			size_t parent = (i - 1) / 2;
			if (hx_os_seq_compare(sequence,
				    hx_os_seq_slot(sequence, parent),
				    hx_os_seq_slot(sequence, i)) >= 0)
				break;
			hx_os_seq_swap(sequence, parent, i);
			i = parent;
		}
	}
	status = HX_OS_OK;
done:
	hx_os_i_container_free(base, temporary);
	return status;
}

static hx_os_status hx_os_seq_insert(hx_os_sequence *sequence, size_t index,
	const void *value, int take, hx_os_error *error)
{
	hx_os_status status;
	if (!sequence || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_seq_add(sequence, index, value, take);
	if (status == HX_OS_OK)
		++sequence->base.generation;
	return hx_os_i_container_leave(&sequence->base, status, error);
}

hx_os_status hx_os_sequence_insert(hx_os_sequence *sequence, size_t index,
	const void *value, hx_os_error *error)
{
	return hx_os_seq_insert(sequence, index, value, 0, error);
}

hx_os_status hx_os_sequence_insert_take(
	hx_os_sequence *sequence, size_t index, void *value, hx_os_error *error)
{
	return hx_os_seq_insert(sequence, index, value, 1, error);
}

hx_os_status hx_os_sequence_push_back(
	hx_os_sequence *sequence, const void *value, hx_os_error *error)
{
	return hx_os_seq_insert(
		sequence, sequence ? sequence->count : 0, value, 0, error);
}

hx_os_status hx_os_sequence_push_back_take(
	hx_os_sequence *sequence, void *value, hx_os_error *error)
{
	return hx_os_seq_insert(
		sequence, sequence ? sequence->count : 0, value, 1, error);
}

hx_os_status hx_os_sequence_push_front(
	hx_os_sequence *sequence, const void *value, hx_os_error *error)
{
	return hx_os_seq_insert(sequence, 0, value, 0, error);
}

static hx_os_status hx_os_seq_remove(
	hx_os_sequence *sequence, size_t index, void *out)
{
	size_t i, size = sequence->base.options.element_size;
	void *value;
	if (index >= sequence->count)
		return HX_OS_NOT_FOUND;
	if (sequence->kind == HX_OS_SEQ_HEAP && index)
		return HX_OS_NOT_SUPPORTED;
	value = hx_os_seq_slot(sequence, index);
	if (out &&
		(sequence->kind == HX_OS_SEQ_LIST
				? hx_os_i_container_overlap(
					  out, size, value, size)
				: hx_os_i_container_overlap(out, size,
					  sequence->data,
					  sequence->capacity * size)))
		return HX_OS_INVALID;
	if (out)
		memcpy(out, value, size);
	else
		hx_os_i_container_drop(&sequence->base, value);
	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *link = hx_os_seq_link_at(sequence, index);
		if (link->previous)
			link->previous->next = link->next;
		else
			sequence->first = link->next;
		if (link->next)
			link->next->previous = link->previous;
		else
			sequence->last = link->previous;
		hx_os_i_container_free(&sequence->base, link);
	}
	else if (sequence->kind == HX_OS_SEQ_HEAP) {
		if (sequence->count > 1)
			memcpy(sequence->data,
				hx_os_seq_slot(sequence, sequence->count - 1),
				size);
	}
	else if (sequence->kind == HX_OS_SEQ_DEQUE &&
		index < sequence->count / 2) {
		for (i = index; i; --i)
			memcpy(hx_os_seq_slot(sequence, i),
				hx_os_seq_slot(sequence, i - 1), size);
		sequence->head = (sequence->head + 1) % sequence->capacity;
	}
	else {
		for (i = index; i + 1 < sequence->count; ++i)
			memcpy(hx_os_seq_slot(sequence, i),
				hx_os_seq_slot(sequence, i + 1), size);
	}
	--sequence->count;
	if (sequence->kind == HX_OS_SEQ_HEAP)
		hx_os_seq_sift(sequence, 0, sequence->count);
	return HX_OS_OK;
}

hx_os_status hx_os_sequence_erase(
	hx_os_sequence *sequence, size_t index, void *out, hx_os_error *error)
{
	hx_os_status status;
	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	status = hx_os_seq_remove(sequence, index, out);
	if (status == HX_OS_OK)
		++sequence->base.generation;
	return hx_os_i_container_leave(&sequence->base, status, error);
}

hx_os_status hx_os_sequence_pop_back(
	hx_os_sequence *sequence, void *out, hx_os_error *error)
{
	return hx_os_sequence_erase(sequence,
		sequence && sequence->count ? sequence->count - 1 : 0, out,
		error);
}

hx_os_status hx_os_sequence_pop_front(
	hx_os_sequence *sequence, void *out, hx_os_error *error)
{
	return hx_os_sequence_erase(sequence, 0, out, error);
}

static void hx_os_seq_clear(hx_os_sequence *sequence)
{
	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *link = sequence->first;
		while (link) {
			hx_os_seq_link *next = link->next;
			hx_os_i_container_drop(&sequence->base,
				(unsigned char *)link + sequence->value_offset);
			hx_os_i_container_free(&sequence->base, link);
			link = next;
		}
		sequence->first = sequence->last = NULL;
	}
	else {
		size_t i;
		for (i = 0; i < sequence->count; ++i)
			hx_os_i_container_drop(
				&sequence->base, hx_os_seq_slot(sequence, i));
	}
	sequence->count = sequence->head = 0;
	++sequence->base.generation;
}

hx_os_status hx_os_sequence_clear(hx_os_sequence *sequence, hx_os_error *error)
{
	hx_os_status status;
	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_seq_clear(sequence);
	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_sequence_destroy(
	hx_os_sequence **sequence, hx_os_error *error)
{
	hx_os_sequence *value;
	hx_os_status status;
	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	value = *sequence;
	if (!value)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_i_container_enter(&value->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	hx_os_seq_clear(value);
	hx_os_i_container_free(&value->base, value->data);
	*sequence = NULL;
	hx_os_memory_free(value);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_vector_sort(hx_os_vector *vector, hx_os_error *error)
{
	hx_os_status status;
	size_t i;
	if (!vector || vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&vector->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	for (i = vector->count / 2; i; --i)
		hx_os_seq_sift(vector, i - 1, vector->count);
	for (i = vector->count; i > 1; --i) {
		hx_os_seq_swap(vector, 0, i - 1);
		hx_os_seq_sift(vector, 0, i - 1);
	}
	++vector->base.generation;
	return hx_os_i_container_leave(&vector->base, HX_OS_OK, error);
}

hx_os_status hx_os_vector_lower_bound(
	const hx_os_vector *vector, const void *key, size_t *index, int *found)
{
	size_t low = 0, high;
	if (!vector || !key || !index || !found ||
		vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare)
		return HX_OS_INVALID;
	if (vector->base.busy)
		return HX_OS_BUSY;
	high = vector->count;
	while (low < high) {
		size_t middle = low + (high - low) / 2;
		if (hx_os_seq_compare(
			    vector, hx_os_seq_slot(vector, middle), key) < 0)
			low = middle + 1;
		else
			high = middle;
	}
	*index = low;
	*found = low < vector->count &&
		hx_os_seq_compare(vector, hx_os_seq_slot(vector, low), key) ==
			0;
	return HX_OS_OK;
}

hx_os_status hx_os_sequence_begin(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator)
{
	if (!sequence || !iterator)
		return HX_OS_INVALID;
	if (sequence->base.busy)
		return HX_OS_BUSY;
	iterator->owner = sequence;
	iterator->link = sequence->first;
	iterator->index = 0;
	iterator->generation = sequence->base.generation;
	return sequence->count ? HX_OS_OK : HX_OS_EOF;
}

static hx_os_status hx_os_seq_iterator_check(
	const hx_os_sequence_iterator *iterator)
{
	if (!iterator || !iterator->owner)
		return HX_OS_INVALID;
	if (iterator->owner->base.busy)
		return HX_OS_BUSY;
	if (iterator->generation != iterator->owner->base.generation)
		return HX_OS_INVALID;
	return iterator->index < iterator->owner->count ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_sequence_next(hx_os_sequence_iterator *iterator)
{
	hx_os_status status = hx_os_seq_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;
	++iterator->index;
	if (iterator->link)
		iterator->link = ((const hx_os_seq_link *)iterator->link)->next;
	return iterator->index < iterator->owner->count ? HX_OS_OK : HX_OS_EOF;
}

hx_os_status hx_os_sequence_value(
	const hx_os_sequence_iterator *iterator, const void **value)
{
	hx_os_status status;
	if (!value)
		return HX_OS_INVALID;
	status = hx_os_seq_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;
	*value = iterator->owner->kind == HX_OS_SEQ_LIST
		? (const unsigned char *)iterator->link +
			iterator->owner->value_offset
		: hx_os_seq_slot(iterator->owner, iterator->index);
	return HX_OS_OK;
}
