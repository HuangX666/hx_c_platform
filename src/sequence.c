#include "containers_private.h"
#include "algorithm_private.h"

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
	size_t size = sequence->base.options.element_size;
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

	if (sequence->kind == HX_OS_SEQ_DEQUE && sequence->count) {
		size_t first = sequence->capacity - sequence->head;

		if (first > sequence->count)
			first = sequence->count;

		memcpy(memory, sequence->data + sequence->head * size,
			first * size);
		memcpy(memory + first * size, sequence->data,
			(sequence->count - first) * size);
	}
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

hx_os_status hx_os_byte_queue_create(hx_os_context *context, size_t max_bytes,
	hx_os_byte_queue **out, hx_os_error *error)
{
	hx_os_container_options options;
	size_t payload, overhead;

	if (out)
		*out = NULL;
	if (!max_bytes)
		max_bytes = 65536;

	payload = hx_os_i_allocation_cost(max_bytes, 0);
	overhead = hx_os_i_allocation_cost(sizeof(hx_os_sequence), 0);
	if (!payload || payload > (SIZE_MAX - overhead) / 2)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	hx_os_container_options_init(&options, 1);
	options.max_items = max_bytes;
	options.max_bytes = overhead + 2 * payload;

	return hx_os_deque_create(context, &options, out, error);
}

static hx_os_status hx_os_byte_queue_check(const hx_os_byte_queue *queue)
{
	if (!queue)
		return HX_OS_INVALID;
	if (queue->base.busy)
		return HX_OS_BUSY;
	if (queue->kind != HX_OS_SEQ_DEQUE ||
		queue->base.options.element_size != 1 ||
		queue->base.options.copy || queue->base.options.drop)
		return HX_OS_TYPE_MISMATCH;

	return HX_OS_OK;
}

hx_os_status hx_os_byte_queue_write(hx_os_byte_queue *queue, const void *data,
	size_t bytes, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);
	size_t needed, capacity, tail, first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) ||
		hx_os_i_container_overlap(
			data, bytes, queue->data, queue->capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > queue->base.options.max_items - queue->count)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	queue->base.busy = 1;
	needed = queue->count + bytes;
	capacity = queue->capacity;
	if (needed > capacity) {
		size_t maximum = queue->base.options.max_items;

		capacity = capacity > maximum / 2 ? maximum : capacity * 2;
		if (capacity < needed)
			capacity = needed;

		status = hx_os_seq_reserve(queue, capacity);
		if (status != HX_OS_OK)
			return hx_os_i_container_leave(
				&queue->base, status, error);
	}

	tail = queue->count >= queue->capacity - queue->head
		? queue->count - (queue->capacity - queue->head)
		: queue->head + queue->count;
	first = queue->capacity - tail;
	if (first > bytes)
		first = bytes;

	memcpy(queue->data + tail, data, first);
	memcpy(queue->data, (const unsigned char *)data + first, bytes - first);
	queue->count = needed;
	++queue->base.generation;

	return hx_os_i_container_leave(&queue->base, HX_OS_OK, error);
}

hx_os_status hx_os_byte_queue_peek(const hx_os_byte_queue *queue, size_t offset,
	void *data, size_t bytes, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);
	size_t start, first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) ||
		hx_os_i_container_overlap(
			data, bytes, queue->data, queue->capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (offset > queue->count || bytes > queue->count - offset)
		return hx_os_i_error(error, HX_OS_NEED_INPUT, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	start = offset >= queue->capacity - queue->head
		? offset - (queue->capacity - queue->head)
		: queue->head + offset;
	first = queue->capacity - start;
	if (first > bytes)
		first = bytes;

	memcpy(data, queue->data + start, first);
	memcpy((unsigned char *)data + first, queue->data, bytes - first);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_byte_queue_read(
	hx_os_byte_queue *queue, void *data, size_t bytes, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (bytes > queue->count)
		return hx_os_i_error(error, HX_OS_NEED_INPUT, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (data) {
		status = hx_os_byte_queue_peek(queue, 0, data, bytes, error);
		if (status != HX_OS_OK)
			return status;
	}

	queue->head = bytes >= queue->capacity - queue->head
		? bytes - (queue->capacity - queue->head)
		: queue->head + bytes;
	queue->count -= bytes;
	if (!queue->count)
		queue->head = 0;

	++queue->base.generation;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_byte_queue_capacity(const hx_os_byte_queue *queue)
{
	return hx_os_byte_queue_check(queue) == HX_OS_OK ? queue->capacity : 0;
}

size_t hx_os_byte_queue_limit(const hx_os_byte_queue *queue)
{
	return hx_os_byte_queue_check(queue) == HX_OS_OK
		? queue->base.options.max_items
		: 0;
}

size_t hx_os_byte_queue_space(const hx_os_byte_queue *queue)
{
	return hx_os_byte_queue_check(queue) == HX_OS_OK
		? queue->base.options.max_items - queue->count
		: 0;
}

hx_os_status hx_os_byte_queue_reserve(
	hx_os_byte_queue *queue, size_t capacity, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	return hx_os_sequence_reserve(queue, capacity, error);
}

hx_os_status hx_os_byte_queue_clear(hx_os_byte_queue *queue, hx_os_error *error)
{
	return hx_os_byte_queue_read(
		queue, NULL, hx_os_byte_queue_size(queue), error);
}

hx_os_status hx_os_byte_queue_write_some(hx_os_byte_queue *queue,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);
	size_t amount;

	if (completed)
		*completed = 0;
	if (!completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	amount = queue->base.options.max_items - queue->count;
	if (amount > bytes)
		amount = bytes;
	if (!amount && bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_byte_queue_write(queue, data, amount, error);
	if (status == HX_OS_OK)
		*completed = amount;

	return status;
}

hx_os_status hx_os_byte_queue_read_some(hx_os_byte_queue *queue, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	hx_os_status status = hx_os_byte_queue_check(queue);
	size_t amount;

	if (completed)
		*completed = 0;
	if (!completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	amount = bytes < queue->count ? bytes : queue->count;
	if (!amount && bytes)
		return hx_os_i_error(error, HX_OS_NEED_INPUT, 0, 0);

	status = hx_os_byte_queue_read(queue, data, amount, error);
	if (status == HX_OS_OK)
		*completed = amount;

	return status;
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

static void hx_os_seq_unlink(hx_os_sequence *sequence, hx_os_seq_link *link)
{
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
		hx_os_seq_unlink(sequence, link);
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

	if (!vector || vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&vector->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_array_sort(vector->data, vector->count,
		vector->base.options.element_size, vector->base.options.compare,
		vector->base.options.user, error);
	if (status == HX_OS_OK)
		++vector->base.generation;

	return hx_os_i_container_leave(&vector->base, status, error);
}

hx_os_status hx_os_vector_lower_bound(
	const hx_os_vector *vector, const void *key, size_t *index, int *found)
{
	hx_os_status status;

	if (!vector || !key || !index || !found ||
		vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare)
		return HX_OS_INVALID;
	if (vector->base.busy)
		return HX_OS_BUSY;

	status = hx_os_array_lower_bound(vector->data, vector->count,
		vector->base.options.element_size, key,
		vector->base.options.compare, vector->base.options.user, index,
		NULL);
	if (status == HX_OS_OK)
		*found = *index < vector->count &&
			hx_os_seq_compare(vector,
				hx_os_seq_slot(vector, *index), key) == 0;

	return status;
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

hx_os_status hx_os_sequence_last(
	const hx_os_sequence *sequence, hx_os_sequence_iterator *iterator)
{
	hx_os_status status = hx_os_sequence_begin(sequence, iterator);
	if (status == HX_OS_OK) {
		iterator->index = sequence->count - 1;
		iterator->link = sequence->last;
	}

	return status;
}

hx_os_status hx_os_sequence_previous(hx_os_sequence_iterator *iterator)
{
	hx_os_status status = hx_os_seq_iterator_check(iterator);
	if (status != HX_OS_OK)
		return status;

	if (iterator->link)
		iterator->link =
			((const hx_os_seq_link *)iterator->link)->previous;

	if (!iterator->index) {
		iterator->index = iterator->owner->count;

		return HX_OS_EOF;
	}

	--iterator->index;

	return HX_OS_OK;
}

hx_os_status hx_os_sequence_erase_at(hx_os_sequence *sequence,
	hx_os_sequence_iterator *iterator, void *out, hx_os_error *error)
{
	hx_os_status status;

	if (!sequence || !iterator || iterator->owner != sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_seq_iterator_check(iterator);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *link = (hx_os_seq_link *)iterator->link;
		void *value = (unsigned char *)link + sequence->value_offset;
		size_t bytes = sequence->base.options.element_size;

		if (hx_os_i_container_overlap(out, bytes, value, bytes))
			return hx_os_i_container_leave(
				&sequence->base, HX_OS_INVALID, error);

		if (out)
			memcpy(out, value, bytes);
		else
			hx_os_i_container_drop(&sequence->base, value);

		iterator->link = link->next;
		hx_os_seq_unlink(sequence, link);
		--sequence->count;
	}
	else
		status = hx_os_seq_remove(sequence, iterator->index, out);

	if (status == HX_OS_OK)
		iterator->generation = ++sequence->base.generation;

	return hx_os_i_container_leave(&sequence->base, status, error);
}

hx_os_status hx_os_sequence_find_if(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user,
	hx_os_sequence_iterator *iterator)
{
	hx_os_sequence_iterator current;
	hx_os_status status;

	if (!predicate || !iterator)
		return HX_OS_INVALID;

	status = hx_os_sequence_begin(sequence, &current);

	while (status == HX_OS_OK) {
		const void *value;

		status = hx_os_sequence_value(&current, &value);
		if (status != HX_OS_OK)
			return status;

		if (predicate(value, user)) {
			*iterator = current;

			return HX_OS_OK;
		}

		status = hx_os_sequence_next(&current);
	}

	return status == HX_OS_EOF ? HX_OS_NOT_FOUND : status;
}

hx_os_status hx_os_sequence_count_if(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *count)
{
	hx_os_sequence_iterator iterator;
	hx_os_status status;
	size_t found = 0;

	if (!predicate || !count)
		return HX_OS_INVALID;

	HX_OS_SEQUENCE_FOR_EACH(iterator, sequence, status)

	{
		const void *value;

		status = hx_os_sequence_value(&iterator, &value);
		if (status != HX_OS_OK)
			return status;

		if (predicate(value, user))
			++found;
	}

	if (status != HX_OS_EOF)
		return status;

	*count = found;

	return HX_OS_OK;
}

hx_os_status hx_os_sequence_visit(hx_os_sequence *sequence,
	hx_os_value_visit_fn visit, void *user, size_t *completed,
	hx_os_error *error)
{
	hx_os_status status;
	hx_os_seq_link *link;
	size_t index;

	if (completed)
		*completed = 0;

	if (!sequence || !visit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	link = sequence->first;

	for (index = 0; index < sequence->count; ++index) {
		const void *value = link
			? (const unsigned char *)link + sequence->value_offset
			: hx_os_seq_slot(sequence, index);
		hx_os_error detail;

		hx_os_error_clear(&detail);
		if (completed)
			*completed = index + 1;

		status = visit(value, user, &detail);
		if (status != HX_OS_OK && status != HX_OS_EOF) {
			sequence->base.busy = 0;
			if (error) {
				*error = detail;
				error->status = status;
			}

			return status;
		}

		if (status == HX_OS_EOF)
			break;

		if (link)
			link = link->next;
	}

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

static hx_os_status hx_os_seq_filter(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int unique,
	size_t *removed, hx_os_error *error)
{
	hx_os_seq_link *link;
	const void *previous = NULL;
	size_t read, kept = 0, bytes;
	hx_os_status status;

	if (!sequence || (!unique && !predicate) ||
		(unique && !sequence->base.options.compare))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	bytes = sequence->base.options.element_size;
	link = sequence->first;

	for (read = 0; read < sequence->count; ++read) {
		hx_os_seq_link *next = link ? link->next : NULL;
		void *value = link
			? (unsigned char *)link + sequence->value_offset
			: hx_os_seq_slot(sequence, read);
		int discard = unique ? previous &&
				!hx_os_seq_compare(sequence, previous, value)
				     : predicate(value, user);

		if (discard) {
			hx_os_i_container_drop(&sequence->base, value);
			if (link)
				hx_os_seq_unlink(sequence, link);
		}
		else {
			previous = value;
			if (!link) {
				void *destination =
					hx_os_seq_slot(sequence, kept);

				if (read != kept)
					memcpy(destination, value, bytes);

				previous = destination;
			}

			++kept;
		}

		link = next;
	}

	if (removed)
		*removed = sequence->count - kept;

	if (kept != sequence->count)
		++sequence->base.generation;

	sequence->count = kept;

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_sequence_remove_if(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *removed,
	hx_os_error *error)
{
	return hx_os_seq_filter(sequence, predicate, user, 0, removed, error);
}

hx_os_status hx_os_sequence_unique(
	hx_os_sequence *sequence, size_t *removed, hx_os_error *error)
{
	return hx_os_seq_filter(sequence, NULL, NULL, 1, removed, error);
}

static hx_os_status hx_os_seq_quantify(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int mode, int *result)
{
	hx_os_sequence_iterator iterator;
	hx_os_status status;

	if (!sequence || !predicate || !result)
		return HX_OS_INVALID;

	*result = mode != 0;

	for (status = hx_os_sequence_begin(sequence, &iterator);
		status == HX_OS_OK; status = hx_os_sequence_next(&iterator)) {
		const void *value;
		int matches;

		status = hx_os_sequence_value(&iterator, &value);
		if (status != HX_OS_OK)
			return status;

		matches = predicate(value, user) != 0;
		if (matches != (mode == 1)) {
			*result = mode == 0;

			return HX_OS_OK;
		}
	}

	return status == HX_OS_EOF ? HX_OS_OK : status;
}

hx_os_status hx_os_sequence_any_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	return hx_os_seq_quantify(sequence, predicate, user, 0, result);
}

hx_os_status hx_os_sequence_all_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	return hx_os_seq_quantify(sequence, predicate, user, 1, result);
}

hx_os_status hx_os_sequence_none_of(const hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, int *result)
{
	return hx_os_seq_quantify(sequence, predicate, user, 2, result);
}

hx_os_status hx_os_sequence_partition(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, size_t *boundary,
	hx_os_error *error)
{
	hx_os_status status;
	size_t kept = 0, scan;

	if (!sequence || !predicate || !boundary)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *heads[2] = {NULL, NULL},
			       *tails[2] = {NULL, NULL};
		hx_os_seq_link *link = sequence->first;

		while (link) {
			hx_os_seq_link *next = link->next;

			int group = !predicate(
				(unsigned char *)link + sequence->value_offset,
				user);
			kept += group == 0;
			link->previous = tails[group];
			link->next = NULL;
			if (tails[group])
				tails[group]->next = link;
			else
				heads[group] = link;

			tails[group] = link;
			link = next;
		}

		if (tails[0])
			tails[0]->next = heads[1];

		if (heads[1])
			heads[1]->previous = tails[0];

		sequence->first = heads[0] ? heads[0] : heads[1];
		sequence->last = tails[1] ? tails[1] : tails[0];
	}
	else
		for (scan = 0; scan < sequence->count; ++scan)
			if (predicate(hx_os_seq_slot(sequence, scan), user))
				hx_os_seq_swap(sequence, kept++, scan);

	if (sequence->count > 1)
		++sequence->base.generation;

	*boundary = kept;

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_sequence_replace_if(hx_os_sequence *sequence,
	hx_os_value_predicate_fn predicate, void *user, const void *replacement,
	size_t *replaced, hx_os_error *error)
{
	hx_os_status status;
	size_t bytes, index, count = 0;
	hx_os_seq_link *link;
	unsigned char *scratch, *value;

	if (replaced)
		*replaced = 0;

	if (!sequence || !predicate || !replacement)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	bytes = sequence->base.options.element_size;
	if (bytes > SIZE_MAX / 2)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	scratch = hx_os_i_container_alloc(&sequence->base, bytes * 2,
		sequence->base.options.element_alignment);
	if (!scratch)
		return hx_os_i_container_leave(&sequence->base,
			sequence->base.allocation_status, error);

	status = hx_os_i_container_copy(
		&sequence->base, scratch, replacement, 0);
	if (status != HX_OS_OK)
		goto done;

	link = sequence->first;

	for (index = 0; index < sequence->count; ++index) {
		value = link ? (unsigned char *)link + sequence->value_offset
			     : hx_os_seq_slot(sequence, index);
		if (link)
			link = link->next;

		if (!predicate(value, user))
			continue;

		status = hx_os_i_container_copy(
			&sequence->base, scratch + bytes, scratch, 0);
		if (status != HX_OS_OK)
			break;

		hx_os_i_container_drop(&sequence->base, value);
		memcpy(value, scratch + bytes, bytes);
		++count;
	}

	hx_os_i_container_drop(&sequence->base, scratch);

done:
	hx_os_i_container_free(&sequence->base, scratch);
	if (count)
		++sequence->base.generation;

	if (replaced)
		*replaced = count;

	return hx_os_i_container_leave(&sequence->base, status, error);
}

static void hx_os_seq_reverse_range(
	hx_os_sequence *sequence, size_t first, size_t last)
{
	while (first < last && first < --last)
		hx_os_seq_swap(sequence, first++, last);
}

hx_os_status hx_os_sequence_reverse(
	hx_os_sequence *sequence, hx_os_error *error)
{
	hx_os_status status;

	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_seq_link *node = sequence->first;

		while (node) {
			hx_os_seq_link *next = node->next;

			node->next = node->previous;
			node->previous = next;
			node = next;
		}

		node = sequence->first;
		sequence->first = sequence->last;
		sequence->last = node;
	}
	else
		hx_os_seq_reverse_range(sequence, 0, sequence->count);

	if (sequence->count > 1)
		++sequence->base.generation;

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_sequence_rotate(
	hx_os_sequence *sequence, size_t left_count, hx_os_error *error)
{
	hx_os_status status;

	if (!sequence)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (sequence->count)
		left_count %= sequence->count;

	if (sequence->count && left_count) {
		if (sequence->kind == HX_OS_SEQ_LIST) {
			hx_os_seq_link *first =
				hx_os_seq_link_at(sequence, left_count);
			hx_os_seq_link *last = first->previous;

			sequence->last->next = sequence->first;
			sequence->first->previous = sequence->last;
			first->previous = last->next = NULL;
			sequence->first = first;
			sequence->last = last;
		}
		else {
			hx_os_seq_reverse_range(sequence, 0, left_count);
			hx_os_seq_reverse_range(
				sequence, left_count, sequence->count);
			hx_os_seq_reverse_range(sequence, 0, sequence->count);
		}

		++sequence->base.generation;
	}

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_vector_upper_bound(
	const hx_os_vector *vector, const void *key, size_t *index)
{
	if (!vector || !key || !index || vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare)
		return HX_OS_INVALID;
	if (vector->base.busy)
		return HX_OS_BUSY;

	return hx_os_array_upper_bound(vector->data, vector->count,
		vector->base.options.element_size, key,
		vector->base.options.compare, vector->base.options.user, index,
		NULL);
}

static void hx_os_list_merge_sort(hx_os_sequence *sequence)
{
	size_t width = 1;
	size_t offset = sequence->value_offset;

	for (;;) {
		hx_os_seq_link *left = sequence->first, *head = NULL,
			       *tail = NULL;
		size_t runs = 0;

		while (left) {
			hx_os_seq_link *right = left;
			size_t left_count = 0, right_count = width;

			++runs;

			while (left_count < width && right) {
				++left_count;
				right = right->next;
			}

			while (left_count || (right_count && right)) {
				hx_os_seq_link *next;

				if (left_count &&
					(!right_count || !right ||
						hx_os_seq_compare(sequence,
							(unsigned char *)left +
								offset,
							(unsigned char *)right +
								offset) <= 0)) {
					next = left;
					left = left->next;
					--left_count;
				}
				else {
					next = right;
					right = right->next;
					--right_count;
				}

				next->previous = tail;
				if (tail)
					tail->next = next;
				else
					head = next;

				tail = next;
			}

			left = right;
		}

		if (tail)
			tail->next = NULL;

		sequence->first = head;
		sequence->last = tail;
		if (runs <= 1)
			return;

		width *= 2;
	}
}

hx_os_status hx_os_sequence_stable_sort(
	hx_os_sequence *sequence, hx_os_error *error)
{
	hx_os_status status;
	size_t width, start, bytes, count;
	unsigned char *scratch;

	if (!sequence || !sequence->base.options.compare)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (sequence->kind == HX_OS_SEQ_HEAP)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_i_container_enter(&sequence->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	count = sequence->count;
	if (count < 2)
		return hx_os_i_container_leave(
			&sequence->base, HX_OS_OK, error);

	if (sequence->kind == HX_OS_SEQ_LIST) {
		hx_os_list_merge_sort(sequence);
		++sequence->base.generation;

		return hx_os_i_container_leave(
			&sequence->base, HX_OS_OK, error);
	}

	bytes = sequence->base.options.element_size;
	scratch = hx_os_i_container_alloc(&sequence->base, count * bytes,
		sequence->base.options.element_alignment);
	if (!scratch)
		return hx_os_i_container_leave(&sequence->base,
			sequence->base.allocation_status, error);

	if (sequence->kind == HX_OS_SEQ_VECTOR) {
		hx_os_i_array_stable_sort(sequence->data, count, bytes,
			sequence->base.options.compare,
			sequence->base.options.user, scratch);
		hx_os_i_container_free(&sequence->base, scratch);
		++sequence->base.generation;

		return hx_os_i_container_leave(
			&sequence->base, HX_OS_OK, error);
	}

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
						hx_os_seq_compare(sequence,
							hx_os_seq_slot(
								sequence, left),
							hx_os_seq_slot(sequence,
								right)) <= 0))
					selected = left++;
				else
					selected = right++;

				memcpy(scratch + target++ * bytes,
					hx_os_seq_slot(sequence, selected),
					bytes);
			}

			start = end;
		}

		for (start = 0; start < count; ++start)
			memcpy(hx_os_seq_slot(sequence, start),
				scratch + start * bytes, bytes);

		if (width > count / 2)
			break;

		width *= 2;
	}

	hx_os_i_container_free(&sequence->base, scratch);
	++sequence->base.generation;

	return hx_os_i_container_leave(&sequence->base, HX_OS_OK, error);
}

hx_os_status hx_os_list_splice(hx_os_list *destination, size_t position,
	hx_os_list *source, size_t first, size_t count, hx_os_error *error)
{
	hx_os_container_options *left, *right;
	hx_os_seq_link *begin, *last, *after, *before;
	size_t cost;

	if (!destination || !source)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (destination->kind != HX_OS_SEQ_LIST ||
		source->kind != HX_OS_SEQ_LIST)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (destination->base.busy || source->base.busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	left = &destination->base.options;
	right = &source->base.options;
	if (destination->base.context != source->base.context ||
		left->element_size != right->element_size ||
		left->element_alignment != right->element_alignment ||
		left->copy != right->copy || left->drop != right->drop ||
		left->compare != right->compare || left->user != right->user ||
		position > destination->count || first > source->count ||
		count > source->count - first)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!count ||
		(source == destination && position >= first &&
			position <= first + count))
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	begin = hx_os_seq_link_at(source, first);
	last = hx_os_seq_link_at(source, first + count - 1);
	cost = hx_os_i_allocation_bytes(begin) * count;
	if (source != destination &&
		(count > left->max_items - destination->count ||
			cost > left->max_bytes - destination->base.memory))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	after = position == destination->count
		? NULL
		: hx_os_seq_link_at(destination, position);
	if (begin->previous)
		begin->previous->next = last->next;
	else
		source->first = last->next;

	if (last->next)
		last->next->previous = begin->previous;
	else
		source->last = begin->previous;

	before = after ? after->previous : destination->last;
	begin->previous = before;
	last->next = after;
	if (before)
		before->next = begin;
	else
		destination->first = begin;

	if (after)
		after->previous = last;
	else
		destination->last = last;

	source->count -= count;
	destination->count += count;
	source->base.memory -= cost;
	destination->base.memory += cost;
	++source->base.generation;
	if (source != destination)
		++destination->base.generation;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_vector_partial_sort(
	hx_os_vector *vector, size_t count, hx_os_error *error)
{
	hx_os_status status;

	if (!vector || vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare || count > vector->count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&vector->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_array_partial_sort(vector->data, vector->count,
		vector->base.options.element_size, count,
		vector->base.options.compare, vector->base.options.user, error);
	if (status == HX_OS_OK && count)
		++vector->base.generation;

	return hx_os_i_container_leave(&vector->base, status, error);
}

hx_os_status hx_os_vector_nth_element(
	hx_os_vector *vector, size_t index, hx_os_error *error)
{
	hx_os_status status;
	size_t bytes;
	void *pivot;

	if (!vector || vector->kind != HX_OS_SEQ_VECTOR ||
		!vector->base.options.compare || index >= vector->count)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_container_enter(&vector->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	bytes = vector->base.options.element_size;
	pivot = hx_os_i_container_alloc(
		&vector->base, bytes, vector->base.options.element_alignment);
	if (!pivot)
		return hx_os_i_container_leave(
			&vector->base, vector->base.allocation_status, error);

	hx_os_i_array_nth_element(vector->data, vector->count, bytes, index,
		vector->base.options.compare, vector->base.options.user, pivot);

	hx_os_i_container_free(&vector->base, pivot);
	++vector->base.generation;

	return hx_os_i_container_leave(&vector->base, HX_OS_OK, error);
}

static hx_os_status hx_os_seq_build(const hx_os_sequence *source,
	const hx_os_container_options *options,
	hx_os_value_predicate_fn predicate, hx_os_value_transform_fn transform,
	void *user, hx_os_vector **out, hx_os_error *error)
{
	hx_os_vector *result;
	hx_os_sequence_iterator iterator;
	hx_os_status status;

	if (out)
		*out = NULL;

	if (!source || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (source->base.busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_vector_create(source->base.context,
		options ? options : &source->base.options, &result, error);
	if (status != HX_OS_OK)
		return status;

	if (transform) {
		status = hx_os_sequence_reserve(result, source->count, error);
		if (status != HX_OS_OK)
			goto fail;
	}

	HX_OS_SEQUENCE_FOR_EACH(iterator, source, status)

	{
		const void *value;

		status = hx_os_sequence_value(&iterator, &value);
		if (status != HX_OS_OK)
			goto fail;

		if (transform) {
			hx_os_error detail;

			hx_os_error_clear(&detail);

			status =
				transform(hx_os_seq_slot(result, result->count),
					value, user, &detail);
			if (status != HX_OS_OK) {
				if (error) {
					*error = detail;
					error->status = status;
				}

				goto fail;
			}

			++result->count;
		}
		else if (!predicate || predicate(value, user)) {
			status = hx_os_sequence_push_back(result, value, error);
			if (status != HX_OS_OK)
				goto fail;
		}
	}

	if (status != HX_OS_EOF)
		goto fail;

	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);

fail:
	(void)hx_os_sequence_destroy(&result, NULL);
	return status;
}

hx_os_status hx_os_sequence_clone(
	const hx_os_sequence *source, hx_os_vector **out, hx_os_error *error)
{
	return hx_os_seq_build(source, NULL, NULL, NULL, NULL, out, error);
}

hx_os_status hx_os_sequence_copy_if(const hx_os_sequence *source,
	hx_os_value_predicate_fn predicate, void *user, hx_os_vector **out,
	hx_os_error *error)
{
	if (out)
		*out = NULL;

	if (!predicate)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_seq_build(source, NULL, predicate, NULL, user, out, error);
}

hx_os_status hx_os_sequence_transform(const hx_os_sequence *source,
	const hx_os_container_options *options,
	hx_os_value_transform_fn transform, void *user, hx_os_vector **out,
	hx_os_error *error)
{
	if (out)
		*out = NULL;

	if (!transform || !options)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_seq_build(
		source, options, NULL, transform, user, out, error);
}

hx_os_status hx_os_sequence_fold(const hx_os_sequence *source,
	void *accumulator, hx_os_value_fold_fn fold, void *user,
	size_t *completed, hx_os_error *error)
{
	hx_os_sequence_iterator iterator;
	hx_os_status status;

	if (completed)
		*completed = 0;

	if (!accumulator || !fold)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	HX_OS_SEQUENCE_FOR_EACH(iterator, source, status)

	{
		const void *value;
		hx_os_error detail;

		status = hx_os_sequence_value(&iterator, &value);
		if (status != HX_OS_OK)
			return hx_os_i_error(error, status, 0, 0);

		hx_os_error_clear(&detail);
		if (completed)
			*completed = iterator.index + 1;

		status = fold(accumulator, value, user, &detail);
		if (status != HX_OS_OK) {
			if (error) {
				*error = detail;
				error->status = status;
			}

			return status;
		}
	}

	return hx_os_i_error(
		error, status == HX_OS_EOF ? HX_OS_OK : status, 0, 0);
}

static int hx_os_seq_same_policy(
	const hx_os_sequence *left, const hx_os_sequence *right)
{
	const hx_os_container_options *a = &left->base.options;
	const hx_os_container_options *b = &right->base.options;

	return a->element_size == b->element_size &&
		a->element_alignment == b->element_alignment &&
		a->copy == b->copy && a->drop == b->drop &&
		a->compare == b->compare && a->user == b->user;
}

hx_os_status hx_os_sequence_merge(const hx_os_sequence *left,
	const hx_os_sequence *right, int operation, hx_os_vector **out,
	hx_os_error *error)
{
	hx_os_sequence_iterator a, b;
	hx_os_status as, bs, status;
	hx_os_vector *result;

	if (out)
		*out = NULL;

	if (!left || !right || !out || !left->base.options.compare ||
		!hx_os_seq_same_policy(left, right) ||
		operation < HX_OS_MERGE ||
		operation > HX_OS_SET_SYMMETRIC_DIFFERENCE)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (left->base.busy || right->base.busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_vector_create(
		left->base.context, &left->base.options, &result, error);
	if (status != HX_OS_OK)
		return status;

	as = hx_os_sequence_begin(left, &a);
	bs = hx_os_sequence_begin(right, &b);

	while (as == HX_OS_OK || bs == HX_OS_OK) {
		const void *av = NULL, *bv = NULL, *chosen = NULL;
		int order;

		if (as == HX_OS_OK)
			(void)hx_os_sequence_value(&a, &av);

		if (bs == HX_OS_OK)
			(void)hx_os_sequence_value(&b, &bv);

		order = !av ? 1 : !bv ? -1 : hx_os_seq_compare(left, av, bv);
		if (operation == HX_OS_MERGE) {
			chosen = order <= 0 ? av : bv;
			if (order <= 0)
				as = hx_os_sequence_next(&a);
			else
				bs = hx_os_sequence_next(&b);
		}
		else {
			if (order < 0 && operation != HX_OS_SET_INTERSECTION)
				chosen = av;
			else if (order > 0 &&
				(operation == HX_OS_SET_UNION ||
					operation ==
						HX_OS_SET_SYMMETRIC_DIFFERENCE))
				chosen = bv;
			else if (!order &&
				(operation == HX_OS_SET_UNION ||
					operation == HX_OS_SET_INTERSECTION))
				chosen = av;
			if (order <= 0)
				as = hx_os_sequence_next(&a);

			if (order >= 0)
				bs = hx_os_sequence_next(&b);
		}

		if (chosen) {
			status =
				hx_os_sequence_push_back(result, chosen, error);
			if (status != HX_OS_OK)
				goto fail;
		}

		if ((as != HX_OS_OK && as != HX_OS_EOF) ||
			(bs != HX_OS_OK && bs != HX_OS_EOF)) {
			status = hx_os_i_error(error, HX_OS_INVALID, 0, 0);
			goto fail;
		}
	}

	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);

fail:
	(void)hx_os_sequence_destroy(&result, NULL);
	return status;
}
