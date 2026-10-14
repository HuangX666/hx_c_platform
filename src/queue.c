#include "coord_private.h"
#include "hx_os_queue.h"

int hx_os_queues_available(void)
{
	return HX_OS_ENABLE_QUEUES;
}

#if HX_OS_ENABLE_QUEUES

typedef struct hx_os_queue_storage {
	unsigned char *data;
	size_t bytes;
	size_t capacity;
	size_t head;
	size_t count;
} hx_os_queue_storage;

struct hx_os_message_queue {
	hx_os_queue_storage storage;
	hx_os_mutex *mutex;
	hx_os_condition *readable;
	hx_os_condition *writable;
	size_t waiters;
	int closed;
};

struct hx_os_ring_queue {
	hx_os_queue_storage storage;
	hx_os_atomic enqueue;
	hx_os_atomic dequeue;
	hx_os_atomic *sequence;
	hx_os_mutex *mutex;
	int mode;
	int backend;
};

static hx_os_status hx_os_queue_storage_init(hx_os_queue_storage *storage,
	hx_os_context *context, size_t bytes, size_t capacity,
	hx_os_error *error)
{
	if (!bytes || !capacity)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity > SIZE_MAX / bytes)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	storage->data = hx_os_i_alloc(context, bytes * capacity, 0);
	if (!storage->data)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	storage->bytes = bytes;
	storage->capacity = capacity;
	return HX_OS_OK;
}

static unsigned char *hx_os_queue_slot(
	hx_os_queue_storage *storage, size_t index)
{
	return storage->data + index * storage->bytes;
}

static void hx_os_queue_copy(hx_os_queue_storage *storage, size_t start,
	const void *input, void *output, size_t count)
{
	size_t first = storage->capacity - start;
	size_t first_bytes;

	if (first > count)
		first = count;
	first_bytes = first * storage->bytes;
	if (input) {
		memcpy(hx_os_queue_slot(storage, start), input, first_bytes);
		if (count > first)
			memcpy(storage->data,
				(const unsigned char *)input + first_bytes,
				(count - first) * storage->bytes);
	}
	else {
		memcpy(output, hx_os_queue_slot(storage, start), first_bytes);
		if (count > first)
			memcpy((unsigned char *)output + first_bytes,
				storage->data,
				(count - first) * storage->bytes);
	}
}

static size_t hx_os_queue_storage_transfer(hx_os_queue_storage *storage,
	const void *input, void *output, size_t count)
{
	size_t available =
		input ? storage->capacity - storage->count : storage->count;
	size_t position = storage->head;

	if (count > available)
		count = available;
	if (!count)
		return 0;
	if (input)
		position = storage->count >= storage->capacity - position
			? storage->count - (storage->capacity - position)
			: position + storage->count;

	hx_os_queue_copy(storage, position, input, output, count);
	if (input)
		storage->count += count;
	else {
		storage->head = count >= storage->capacity - position
			? count - (storage->capacity - position)
			: position + count;
		storage->count -= count;
	}
	return count;
}

hx_os_status hx_os_message_queue_create(hx_os_context *context,
	size_t message_bytes, size_t capacity, hx_os_message_queue **out,
	hx_os_error *error)
{
	hx_os_message_queue *queue;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	queue = hx_os_i_zero(context, sizeof(*queue));
	if (!queue)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_queue_storage_init(
		&queue->storage, context, message_bytes, capacity, error);
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(context, 0, &queue->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &queue->readable, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &queue->writable, error);
	if (status != HX_OS_OK) {
		hx_os_condition_destroy(queue->writable);
		hx_os_condition_destroy(queue->readable);
		hx_os_mutex_destroy(queue->mutex);
		hx_os_memory_free(queue->storage.data);
		hx_os_memory_free(queue);
		return status;
	}

	*out = queue;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_message_queue_transfer(hx_os_message_queue *queue,
	const void *input, void *output, size_t count, int sending,
	hx_os_deadline deadline, size_t *completed, hx_os_cancel_token *token,
	hx_os_error *error)
{
	hx_os_cancel_watch watch;
	hx_os_status status;

	if (completed)
		*completed = 0;
	if (!queue || !completed || (count && !input && !output))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (count > SIZE_MAX / queue->storage.bytes)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!count)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_i_cancel_watch(&watch, token, queue->mutex,
		sending ? queue->writable : queue->readable, error);
	if (status != HX_OS_OK)
		return status;
	status = deadline == HX_OS_DEADLINE_NOW
		? hx_os_mutex_try_lock(queue->mutex, error)
		: hx_os_mutex_lock(queue->mutex, error);
	if (status == HX_OS_BUSY)
		status = hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
	if (status != HX_OS_OK) {
		hx_os_i_cancel_unwatch(&watch);
		return status;
	}

	for (;;) {
		size_t moved;

		if (hx_os_cancel_requested(token)) {
			status = hx_os_i_error(error, HX_OS_CANCELLED, 0, 0);
			break;
		}
		if (queue->closed && (sending || !queue->storage.count)) {
			status = hx_os_i_error(error,
				sending ? HX_OS_CLOSED : HX_OS_EOF, 0, 0);
			break;
		}
		moved = hx_os_queue_storage_transfer(&queue->storage,
			sending ? (const unsigned char *)input +
					*completed * queue->storage.bytes
				: NULL,
			output, count - *completed);
		if (moved) {
			hx_os_condition *condition =
				sending ? queue->readable : queue->writable;

			*completed += moved;
			if (moved == 1)
				hx_os_condition_notify_one(condition);
			else
				hx_os_condition_notify_all(condition);
			if (!sending || *completed == count) {
				status = hx_os_i_error(error, HX_OS_OK, 0, 0);
				break;
			}
		}
		if (deadline == HX_OS_DEADLINE_NOW) {
			status = hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
			break;
		}

		++queue->waiters;
		status = hx_os_condition_wait_until(
			sending ? queue->writable : queue->readable,
			queue->mutex, deadline, error);
		--queue->waiters;
		if (status != HX_OS_OK)
			break;
	}

	(void)hx_os_mutex_unlock(queue->mutex, NULL);
	hx_os_i_cancel_unwatch(&watch);
	return status;
}

hx_os_status hx_os_message_queue_send_many_until(hx_os_message_queue *queue,
	const void *messages, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	return hx_os_message_queue_transfer(queue, messages, NULL, count, 1,
		deadline, completed, NULL, error);
}

hx_os_status hx_os_message_queue_receive_many_until(hx_os_message_queue *queue,
	void *messages, size_t maximum, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	return hx_os_message_queue_transfer(queue, NULL, messages, maximum, 0,
		deadline, completed, NULL, error);
}

hx_os_status hx_os_message_queue_send_until(hx_os_message_queue *queue,
	const void *message, hx_os_deadline deadline, hx_os_error *error)
{
	size_t completed;

	return hx_os_message_queue_send_many_until(
		queue, message, 1, deadline, &completed, error);
}

hx_os_status hx_os_message_queue_receive_until(hx_os_message_queue *queue,
	void *message, hx_os_deadline deadline, hx_os_error *error)
{
	size_t completed;

	return hx_os_message_queue_receive_many_until(
		queue, message, 1, deadline, &completed, error);
}

hx_os_status hx_os_message_queue_close(
	hx_os_message_queue *queue, hx_os_error *error)
{
	hx_os_status status;

	if (!queue)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(queue->mutex, error);
	if (status != HX_OS_OK)
		return status;

	queue->closed = 1;
	hx_os_condition_notify_all(queue->readable);
	hx_os_condition_notify_all(queue->writable);
	(void)hx_os_mutex_unlock(queue->mutex, NULL);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_message_queue_size(
	hx_os_message_queue *queue, size_t *out, hx_os_error *error)
{
	hx_os_status status;

	if (!queue || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_mutex_lock(queue->mutex, error);
	if (status != HX_OS_OK)
		return status;

	*out = queue->storage.count;
	(void)hx_os_mutex_unlock(queue->mutex, NULL);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_message_queue_destroy(
	hx_os_message_queue **queue, hx_os_error *error)
{
	hx_os_message_queue *current;
	hx_os_status status;

	if (!queue)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	current = *queue;
	if (!current)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_os_mutex_try_lock(current->mutex, error);
	if (status != HX_OS_OK)
		return status;
	if (current->waiters) {
		(void)hx_os_mutex_unlock(current->mutex, NULL);
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	}
	(void)hx_os_mutex_unlock(current->mutex, NULL);

	hx_os_condition_destroy(current->writable);
	hx_os_condition_destroy(current->readable);
	hx_os_mutex_destroy(current->mutex);
	hx_os_memory_free(current->storage.data);
	hx_os_memory_free(current);
	*queue = NULL;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_queue_create(hx_os_context *context,
	size_t message_bytes, size_t capacity, int mode, int backend,
	hx_os_ring_queue **out, hx_os_error *error)
{
	hx_os_ring_queue *queue;
	hx_os_status status;
	size_t i;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (mode < HX_OS_QUEUE_SPSC || mode > HX_OS_QUEUE_MPMC ||
		(backend != HX_OS_QUEUE_LOCKED &&
			backend != HX_OS_QUEUE_ATOMIC))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (backend == HX_OS_QUEUE_ATOMIC) {
		if (capacity < 2 || (capacity & (capacity - 1)))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		if (capacity > SIZE_MAX / sizeof(hx_os_atomic))
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
		if (!hx_os_atomic_is_lock_free())
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	}

	queue = hx_os_i_zero(context, sizeof(*queue));
	if (!queue)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	queue->mode = mode;
	queue->backend = backend;
	status = hx_os_queue_storage_init(
		&queue->storage, context, message_bytes, capacity, error);
	if (status == HX_OS_OK && backend == HX_OS_QUEUE_LOCKED)
		status = hx_os_mutex_create(context, 0, &queue->mutex, error);
	if (status == HX_OS_OK && backend == HX_OS_QUEUE_ATOMIC &&
		mode != HX_OS_QUEUE_SPSC) {
		queue->sequence = hx_os_i_alloc(
			context, capacity * sizeof(*queue->sequence), 8);
		if (!queue->sequence)
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		else
			for (i = 0; i < capacity; ++i)
				queue->sequence[i].value = (int64_t)i;
	}
	if (status != HX_OS_OK) {
		hx_os_mutex_destroy(queue->mutex);
		hx_os_memory_free(queue->sequence);
		hx_os_memory_free(queue->storage.data);
		hx_os_memory_free(queue);
		return status;
	}

	*out = queue;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

// Preserve counter bits without signed overflow or an out-of-range cast.
static int64_t hx_os_queue_counter(uint64_t value)
{
	int64_t result;

	memcpy(&result, &value, sizeof(result));
	return result;
}

static size_t hx_os_ring_spsc(
	hx_os_ring_queue *queue, const void *input, void *output, size_t count)
{
	int sending = input != NULL;
	hx_os_atomic *own = sending ? &queue->enqueue : &queue->dequeue;
	hx_os_atomic *peer = sending ? &queue->dequeue : &queue->enqueue;
	uint64_t position = (uint64_t)hx_os_atomic_load(own, HX_OS_RELAXED);
	uint64_t other = (uint64_t)hx_os_atomic_load(peer, HX_OS_ACQUIRE);
	size_t available = sending
		? queue->storage.capacity - (size_t)(position - other)
		: (size_t)(other - position);

	if (count > available)
		count = available;
	if (!count)
		return 0;

	hx_os_queue_copy(&queue->storage,
		(size_t)(position & (queue->storage.capacity - 1)), input,
		output, count);
	hx_os_atomic_store(
		own, hx_os_queue_counter(position + count), HX_OS_RELEASE);
	return count;
}

static hx_os_status hx_os_ring_multi(
	hx_os_ring_queue *queue, const void *input, void *output)
{
	int sending = input != NULL;
	hx_os_atomic *cursor = sending ? &queue->enqueue : &queue->dequeue;
	int64_t expected = hx_os_atomic_load(cursor, HX_OS_RELAXED);
	uint64_t position, observed, wanted;
	hx_os_atomic *sequence;
	unsigned char *slot;

	for (;;) {
		position = (uint64_t)expected;
		sequence = &queue->sequence[(
			size_t)(position & (queue->storage.capacity - 1))];
		observed = (uint64_t)hx_os_atomic_load(sequence, HX_OS_ACQUIRE);
		wanted = position + (sending ? 0 : 1);
		if (observed == wanted) {
			if (hx_os_atomic_compare_exchange_weak(cursor,
				    &expected,
				    hx_os_queue_counter(position + 1),
				    HX_OS_RELAXED, HX_OS_RELAXED))
				break;
		}
		else if ((observed - wanted) & (UINT64_C(1) << 63))
			return HX_OS_WOULD_BLOCK;
		else
			expected = hx_os_atomic_load(cursor, HX_OS_RELAXED);
	}

	// Acquire of the slot tag owns its bytes until the release below. A
	// paused owner can stall this slot: this multi-party algorithm is not
	// lock-free.
	slot = hx_os_queue_slot(&queue->storage,
		(size_t)(position & (queue->storage.capacity - 1)));
	if (sending)
		memcpy(slot, input, queue->storage.bytes);
	else
		memcpy(output, slot, queue->storage.bytes);
	hx_os_atomic_store(sequence,
		hx_os_queue_counter(position +
			(sending ? 1 : (uint64_t)queue->storage.capacity)),
		HX_OS_RELEASE);
	return HX_OS_OK;
}

static hx_os_status hx_os_ring_queue_transfer(hx_os_ring_queue *queue,
	const void *input, void *output, size_t count, size_t *completed,
	hx_os_error *error)
{
	hx_os_status status;

	if (completed)
		*completed = 0;
	if (!queue || !completed || (count && !input && !output))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (count > SIZE_MAX / queue->storage.bytes)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!count)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (queue->backend == HX_OS_QUEUE_ATOMIC) {
		if (queue->mode == HX_OS_QUEUE_SPSC)
			*completed =
				hx_os_ring_spsc(queue, input, output, count);
		else {
			while (*completed < count) {
				size_t offset =
					*completed * queue->storage.bytes;

				status = hx_os_ring_multi(queue,
					input ? (const unsigned char *)input +
							offset
					      : NULL,
					output ? (unsigned char *)output +
							offset
					       : NULL);
				if (status != HX_OS_OK)
					break;
				++*completed;
			}
		}
	}
	else {
		status = hx_os_mutex_try_lock(queue->mutex, error);
		if (status == HX_OS_BUSY)
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
		if (status != HX_OS_OK)
			return status;

		*completed = hx_os_queue_storage_transfer(
			&queue->storage, input, output, count);
		(void)hx_os_mutex_unlock(queue->mutex, NULL);
	}
	return hx_os_i_error(
		error, *completed ? HX_OS_OK : HX_OS_WOULD_BLOCK, 0, 0);
}

hx_os_status hx_os_ring_queue_try_push_many(hx_os_ring_queue *queue,
	const void *messages, size_t count, size_t *completed,
	hx_os_error *error)
{
	return hx_os_ring_queue_transfer(
		queue, messages, NULL, count, completed, error);
}

hx_os_status hx_os_ring_queue_try_pop_many(hx_os_ring_queue *queue,
	void *messages, size_t maximum, size_t *completed, hx_os_error *error)
{
	return hx_os_ring_queue_transfer(
		queue, NULL, messages, maximum, completed, error);
}

hx_os_status hx_os_ring_queue_try_push(
	hx_os_ring_queue *queue, const void *message, hx_os_error *error)
{
	size_t completed;

	return hx_os_ring_queue_try_push_many(
		queue, message, 1, &completed, error);
}

hx_os_status hx_os_ring_queue_try_pop(
	hx_os_ring_queue *queue, void *message, hx_os_error *error)
{
	size_t completed;

	return hx_os_ring_queue_try_pop_many(
		queue, message, 1, &completed, error);
}

size_t hx_os_ring_queue_capacity(const hx_os_ring_queue *queue)
{
	return queue ? queue->storage.capacity : 0;
}

int hx_os_ring_queue_is_lock_free(const hx_os_ring_queue *queue)
{
	return queue && queue->backend == HX_OS_QUEUE_ATOMIC &&
		queue->mode == HX_OS_QUEUE_SPSC;
}

hx_os_status hx_os_ring_queue_destroy(
	hx_os_ring_queue **queue, hx_os_error *error)
{
	if (!queue)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (*queue) {
		hx_os_mutex_destroy((*queue)->mutex);
		hx_os_memory_free((*queue)->sequence);
		hx_os_memory_free((*queue)->storage.data);
		hx_os_memory_free(*queue);
		*queue = NULL;
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

#else

hx_os_status hx_os_message_queue_create(hx_os_context *context,
	size_t message_bytes, size_t capacity, hx_os_message_queue **out,
	hx_os_error *error)
{
	(void)context;
	(void)message_bytes;
	(void)capacity;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_message_queue_send_until(hx_os_message_queue *queue,
	const void *message, hx_os_deadline deadline, hx_os_error *error)
{
	(void)queue;
	(void)message;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_message_queue_receive_until(hx_os_message_queue *queue,
	void *message, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_message_queue_send_until(queue, message, deadline, error);
}

hx_os_status hx_os_message_queue_send_many_until(hx_os_message_queue *queue,
	const void *messages, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	(void)count;
	if (completed)
		*completed = 0;
	return hx_os_message_queue_send_until(queue, messages, deadline, error);
}

hx_os_status hx_os_message_queue_receive_many_until(hx_os_message_queue *queue,
	void *messages, size_t maximum, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	return hx_os_message_queue_send_many_until(
		queue, messages, maximum, deadline, completed, error);
}

hx_os_status hx_os_message_queue_close(
	hx_os_message_queue *queue, hx_os_error *error)
{
	(void)queue;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_message_queue_size(
	hx_os_message_queue *queue, size_t *out, hx_os_error *error)
{
	(void)queue;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_message_queue_destroy(
	hx_os_message_queue **queue, hx_os_error *error)
{
	(void)queue;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_ring_queue_create(hx_os_context *context,
	size_t message_bytes, size_t capacity, int mode, int backend,
	hx_os_ring_queue **out, hx_os_error *error)
{
	(void)context;
	(void)message_bytes;
	(void)capacity;
	(void)mode;
	(void)backend;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_ring_queue_try_push(
	hx_os_ring_queue *queue, const void *message, hx_os_error *error)
{
	(void)queue;
	(void)message;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_ring_queue_try_pop(
	hx_os_ring_queue *queue, void *message, hx_os_error *error)
{
	return hx_os_ring_queue_try_push(queue, message, error);
}

hx_os_status hx_os_ring_queue_try_push_many(hx_os_ring_queue *queue,
	const void *messages, size_t count, size_t *completed,
	hx_os_error *error)
{
	(void)count;
	if (completed)
		*completed = 0;
	return hx_os_ring_queue_try_push(queue, messages, error);
}

hx_os_status hx_os_ring_queue_try_pop_many(hx_os_ring_queue *queue,
	void *messages, size_t maximum, size_t *completed, hx_os_error *error)
{
	return hx_os_ring_queue_try_push_many(
		queue, messages, maximum, completed, error);
}

size_t hx_os_ring_queue_capacity(const hx_os_ring_queue *queue)
{
	(void)queue;
	return 0;
}

int hx_os_ring_queue_is_lock_free(const hx_os_ring_queue *queue)
{
	(void)queue;
	return 0;
}

hx_os_status hx_os_ring_queue_destroy(
	hx_os_ring_queue **queue, hx_os_error *error)
{
	(void)queue;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

#endif

hx_os_status hx_os_message_queue_send_many_cancellable_until(
	hx_os_message_queue *queue, const void *messages, size_t count,
	hx_os_deadline deadline, hx_os_cancel_token *token, size_t *completed,
	hx_os_error *error)
{
#if HX_OS_ENABLE_QUEUES
	return hx_os_message_queue_transfer(queue, messages, NULL, count, 1,
		deadline, completed, token, error);
#else
	(void)token;
	return hx_os_message_queue_send_many_until(
		queue, messages, count, deadline, completed, error);
#endif
}

hx_os_status hx_os_message_queue_receive_many_cancellable_until(
	hx_os_message_queue *queue, void *messages, size_t maximum,
	hx_os_deadline deadline, hx_os_cancel_token *token, size_t *completed,
	hx_os_error *error)
{
#if HX_OS_ENABLE_QUEUES
	return hx_os_message_queue_transfer(queue, NULL, messages, maximum, 0,
		deadline, completed, token, error);
#else
	(void)token;
	return hx_os_message_queue_receive_many_until(
		queue, messages, maximum, deadline, completed, error);
#endif
}

hx_os_status hx_os_message_queue_send_cancellable_until(
	hx_os_message_queue *queue, const void *message,
	hx_os_deadline deadline, hx_os_cancel_token *token, hx_os_error *error)
{
	size_t completed;

	return hx_os_message_queue_send_many_cancellable_until(
		queue, message, 1, deadline, token, &completed, error);
}

hx_os_status hx_os_message_queue_receive_cancellable_until(
	hx_os_message_queue *queue, void *message, hx_os_deadline deadline,
	hx_os_cancel_token *token, hx_os_error *error)
{
	size_t completed;

	return hx_os_message_queue_receive_many_cancellable_until(
		queue, message, 1, deadline, token, &completed, error);
}
