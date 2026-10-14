#include "containers_private.h"
#include "hx_ring.h"

#if HX_OS_ENABLE_CONTAINERS

enum { HX_OS_RING_IDLE, HX_OS_RING_READING, HX_OS_RING_WRITING };

static hx_os_status hx_os_ring_ready(const hx_os_ring *ring)
{
	if (!ring)
		return HX_OS_INVALID;

	return ring->borrowed ? HX_OS_BUSY : HX_OS_OK;
}

static size_t hx_os_ring_offset(const hx_os_ring *ring, size_t bytes)
{
	return bytes >= ring->capacity - ring->head
		? bytes - (ring->capacity - ring->head)
		: ring->head + bytes;
}

static int hx_os_ring_overlap(
	const hx_os_ring *ring, const void *data, size_t bytes)
{
	return hx_os_i_container_overlap(data, bytes, ring, sizeof(*ring)) ||
		hx_os_i_container_overlap(
			data, bytes, ring->data, ring->capacity);
}

static void hx_os_ring_advance(hx_os_ring *ring, size_t bytes)
{
	ring->head = hx_os_ring_offset(ring, bytes);
	ring->count -= bytes;
	if (!ring->count)
		ring->head = 0;
}

hx_os_status hx_os_ring_init(hx_os_ring *ring, void *storage, size_t capacity,
	hx_os_error *error)
{
	if (!ring || (!storage && capacity) ||
		hx_os_i_container_overlap(
			ring, sizeof(*ring), storage, capacity))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (capacity > UINTPTR_MAX - (uintptr_t)storage)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	ring->data = storage;
	ring->capacity = capacity;
	ring->head = 0;
	ring->count = 0;
	ring->borrowed = HX_OS_RING_IDLE;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

size_t hx_os_ring_size(const hx_os_ring *ring)
{
	return ring ? ring->count : 0;
}

size_t hx_os_ring_capacity(const hx_os_ring *ring)
{
	return ring ? ring->capacity : 0;
}

size_t hx_os_ring_space(const hx_os_ring *ring)
{
	return ring ? ring->capacity - ring->count : 0;
}

hx_os_status hx_os_ring_clear(hx_os_ring *ring, hx_os_error *error)
{
	hx_os_status status = hx_os_ring_ready(ring);

	if (status == HX_OS_OK) {
		ring->count = 0;
		ring->head = 0;
	}

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_ring_write(hx_os_ring *ring, const void *data, size_t bytes,
	hx_os_error *error)
{
	hx_os_status status = hx_os_ring_ready(ring);
	size_t tail, first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if ((!data && bytes) || hx_os_ring_overlap(ring, data, bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > ring->capacity - ring->count)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	tail = hx_os_ring_offset(ring, ring->count);
	first = ring->capacity - tail;
	if (first > bytes)
		first = bytes;

	memcpy(ring->data + tail, data, first);
	memcpy(ring->data, (const unsigned char *)data + first, bytes - first);
	ring->count += bytes;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_read(hx_os_ring *ring, void *data, size_t bytes,
	hx_os_error *error)
{
	hx_os_status status = hx_os_ring_ready(ring);
	size_t first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (hx_os_ring_overlap(ring, data, bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > ring->count)
		return hx_os_i_error(error, HX_OS_NEED_INPUT, 0, 0);
	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	first = ring->capacity - ring->head;
	if (first > bytes)
		first = bytes;

	if (data) {
		memcpy(data, ring->data + ring->head, first);
		memcpy((unsigned char *)data + first, ring->data,
			bytes - first);
	}

	hx_os_ring_advance(ring, bytes);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_read_spans(hx_os_ring *ring,
	hx_os_ring_read_span spans[2], hx_os_error *error)
{
	hx_os_status status = hx_os_ring_ready(ring);
	size_t first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (!spans || hx_os_ring_overlap(ring, spans, 2 * sizeof(*spans)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!ring->count)
		return hx_os_i_error(error, HX_OS_NEED_INPUT, 0, 0);

	first = ring->capacity - ring->head;
	if (first > ring->count)
		first = ring->count;

	spans[0].data = ring->data + ring->head;
	spans[0].size = first;
	spans[1].size = ring->count - first;
	spans[1].data = spans[1].size ? ring->data : NULL;
	ring->borrowed = HX_OS_RING_READING;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_write_spans(hx_os_ring *ring,
	hx_os_ring_write_span spans[2], hx_os_error *error)
{
	hx_os_status status = hx_os_ring_ready(ring);
	size_t tail, free_bytes, first;

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (!spans || hx_os_ring_overlap(ring, spans, 2 * sizeof(*spans)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	free_bytes = ring->capacity - ring->count;
	if (!free_bytes)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	tail = hx_os_ring_offset(ring, ring->count);
	first = ring->capacity - tail;
	if (first > free_bytes)
		first = free_bytes;

	spans[0].data = ring->data + tail;
	spans[0].size = first;
	spans[1].size = free_bytes - first;
	spans[1].data = spans[1].size ? ring->data : NULL;
	ring->borrowed = HX_OS_RING_WRITING;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_consume(
	hx_os_ring *ring, size_t bytes, hx_os_error *error)
{
	if (!ring || ring->borrowed != HX_OS_RING_READING)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > ring->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (bytes)
		hx_os_ring_advance(ring, bytes);
	ring->borrowed = HX_OS_RING_IDLE;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_ring_commit(
	hx_os_ring *ring, size_t bytes, hx_os_error *error)
{
	if (!ring || ring->borrowed != HX_OS_RING_WRITING)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (bytes > ring->capacity - ring->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	ring->count += bytes;
	ring->borrowed = HX_OS_RING_IDLE;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

#else

hx_os_status hx_os_ring_init(hx_os_ring *ring, void *storage, size_t capacity,
	hx_os_error *error)
{
	(void)ring;
	(void)storage;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

size_t hx_os_ring_size(const hx_os_ring *ring)
{
	(void)ring;

	return 0;
}

size_t hx_os_ring_capacity(const hx_os_ring *ring)
{
	(void)ring;

	return 0;
}

size_t hx_os_ring_space(const hx_os_ring *ring)
{
	(void)ring;

	return 0;
}

hx_os_status hx_os_ring_clear(hx_os_ring *ring, hx_os_error *error)
{
	(void)ring;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_ring_write(hx_os_ring *ring, const void *data, size_t bytes,
	hx_os_error *error)
{
	(void)data;
	(void)bytes;

	return hx_os_ring_clear(ring, error);
}

hx_os_status hx_os_ring_read(hx_os_ring *ring, void *data, size_t bytes,
	hx_os_error *error)
{
	return hx_os_ring_write(ring, data, bytes, error);
}

hx_os_status hx_os_ring_read_spans(hx_os_ring *ring,
	hx_os_ring_read_span spans[2], hx_os_error *error)
{
	(void)spans;

	return hx_os_ring_clear(ring, error);
}

hx_os_status hx_os_ring_write_spans(hx_os_ring *ring,
	hx_os_ring_write_span spans[2], hx_os_error *error)
{
	(void)spans;

	return hx_os_ring_clear(ring, error);
}

hx_os_status hx_os_ring_consume(
	hx_os_ring *ring, size_t bytes, hx_os_error *error)
{
	(void)bytes;

	return hx_os_ring_clear(ring, error);
}

hx_os_status hx_os_ring_commit(
	hx_os_ring *ring, size_t bytes, hx_os_error *error)
{
	(void)bytes;

	return hx_os_ring_clear(ring, error);
}

#endif
