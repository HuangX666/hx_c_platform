#include "internal.h"

typedef struct hx_os_arena_block {
	struct hx_os_arena_block *next;
	size_t capacity;
	size_t position;
	size_t cost;
} hx_os_arena_block;

struct hx_os_arena {
	hx_os_context *context;
	hx_os_arena_block *blocks;
	size_t block_bytes;
	size_t maximum;
	size_t reserved;
	size_t base_cost;
};

struct hx_os_object_pool {
	hx_os_atomic outstanding;
	hx_os_memory_pool *memory;
	size_t size;
	size_t alignment;
	size_t maximum;
};

hx_os_status hx_os_object_pool_create(hx_os_context *context,
	size_t object_bytes, size_t alignment, size_t maximum_objects,
	hx_os_object_pool **out, hx_os_error *error)
{
	hx_os_object_pool *pool;
	hx_os_status status;

	if (!out || !object_bytes || !hx_os_i_alignment_valid(alignment))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if SIZE_MAX > INT64_MAX
	if (maximum_objects > INT64_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#endif
	*out = NULL;
	pool = hx_os_i_zero(context, sizeof(*pool));
	if (!pool)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	status = hx_os_memory_pool_create(context, NULL, &pool->memory, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(pool);

		return status;
	}

	pool->size = object_bytes;
	pool->alignment = alignment;
	pool->maximum = maximum_objects;
	*out = pool;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_object_pool_acquire(
	hx_os_object_pool *pool, void **out, hx_os_error *error)
{
	int64_t previous;
	hx_os_status status;

	if (!pool || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	previous = hx_os_atomic_fetch_add(&pool->outstanding, 1, HX_OS_ACQ_REL);
	if (pool->maximum && (uint64_t)previous >= pool->maximum)
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	else
		status = hx_os_memory_pool_alloc(
			pool->memory, pool->size, pool->alignment, out, error);

	if (status != HX_OS_OK)
		(void)hx_os_atomic_fetch_add(
			&pool->outstanding, -1, HX_OS_RELEASE);

	return status;
}

void hx_os_object_pool_release(hx_os_object_pool *pool, void *object)
{
	if (!object)
		return;

	hx_os_memory_pool_free(pool->memory, object);
	(void)hx_os_atomic_fetch_add(&pool->outstanding, -1, HX_OS_RELEASE);
}

hx_os_status hx_os_object_pool_destroy(
	hx_os_object_pool **pool, hx_os_error *error)
{
	hx_os_status status;

	if (!pool)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*pool)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_atomic_load(&(*pool)->outstanding, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_memory_pool_destroy(&(*pool)->memory, error);
	if (status != HX_OS_OK)
		return status;

	hx_os_memory_free(*pool);
	*pool = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_arena_create(hx_os_context *context, size_t block_bytes,
	size_t maximum_bytes, hx_os_arena **out, hx_os_error *error)
{
	hx_os_arena *arena;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	arena = hx_os_i_zero(context, sizeof(*arena));
	if (!arena)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	arena->context = context;
	arena->block_bytes = block_bytes ? block_bytes : 16384;
	arena->maximum = maximum_bytes;
	arena->base_cost = hx_os_i_allocation_bytes(arena);
	arena->reserved = arena->base_cost;
	if (maximum_bytes && arena->reserved > maximum_bytes) {
		hx_os_memory_free(arena);

		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	*out = arena;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_arena_alloc(hx_os_arena *arena, size_t bytes,
	size_t alignment, void **out, hx_os_error *error)
{
	hx_os_arena_block *block;
	uintptr_t address;
	size_t padding;
	size_t needed;
	size_t capacity;
	size_t cost;

	if (!arena || !out || !hx_os_i_alignment_valid(alignment))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!alignment)
		alignment = HX_OS_I_ALIGNMENT;

	if (!bytes)
		bytes = 1;

	block = arena->blocks;
	if (block) {
		address = (uintptr_t)(block + 1) + block->position;
		padding = (size_t)(((uintptr_t)0 - address) & (alignment - 1));
		if (padding <= block->capacity - block->position &&
			bytes <= block->capacity - block->position - padding) {
			block->position += padding + bytes;
			*out = (void *)(address + padding);

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
	}

	if (!hx_os_i_add_size(bytes, alignment - 1, &needed))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	capacity = needed > arena->block_bytes ? needed : arena->block_bytes;
	if (!hx_os_i_add_size(capacity, sizeof(*block), &needed))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	cost = hx_os_i_allocation_cost(needed, 0);
	if (!cost || cost > SIZE_MAX - arena->reserved)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (arena->maximum && cost > arena->maximum - arena->reserved)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	block = hx_os_i_zero(arena->context, needed);
	if (!block)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	block->capacity = capacity;
	block->cost = cost;
	block->next = arena->blocks;
	arena->blocks = block;
	arena->reserved += cost;
	address = (uintptr_t)(block + 1);
	padding = (size_t)(((uintptr_t)0 - address) & (alignment - 1));
	block->position = padding + bytes;
	*out = (void *)(address + padding);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_arena_reset(hx_os_arena *arena)
{
	hx_os_arena_block *block = arena->blocks;
	hx_os_arena_block *retained = NULL;

	arena->reserved = arena->base_cost;

	while (block) {
		hx_os_arena_block *next = block->next;

		if (!retained && block->capacity <= arena->block_bytes) {
			retained = block;
			retained->next = NULL;
			retained->position = 0;
			arena->reserved += retained->cost;
		}
		else
			hx_os_memory_free(block);

		block = next;
	}

	arena->blocks = retained;
}

void hx_os_arena_destroy(hx_os_arena *arena)
{
	hx_os_arena_block *block;

	if (!arena)
		return;

	block = arena->blocks;

	while (block) {
		hx_os_arena_block *next = block->next;

		hx_os_memory_free(block);
		block = next;
	}

	hx_os_memory_free(arena);
}
