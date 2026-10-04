#ifndef HX_C_PLATFORM_MEMORY_H
#define HX_C_PLATFORM_MEMORY_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_memory_pool hx_os_memory_pool;
typedef struct hx_os_object_pool hx_os_object_pool;
typedef struct hx_os_arena hx_os_arena;

typedef struct hx_os_memory_pool_options {
	uint32_t size;
	uint32_t version;
	size_t block_bytes;
	size_t maximum_bytes;
	size_t cached_bytes;
} hx_os_memory_pool_options;

typedef struct hx_os_memory_stats {
	size_t reserved_bytes;
	size_t requested_bytes;
	size_t live_allocations;
	size_t peak_reserved_bytes;
} hx_os_memory_stats;

/*
 * Thread-safe, nonmoving segregated allocation. maximum_bytes=0 allows
 * growth. Accounting includes pool-controlled blocks and metadata, not
 * opaque upstream allocator overhead or process RSS. Free belongs to the
 * originating pool. Destruction requires quiescence and zero live blocks.
 */
HX_OS_API void HX_OS_CALL hx_os_memory_pool_options_init(
	hx_os_memory_pool_options *options);

HX_OS_API hx_os_status HX_OS_CALL hx_os_memory_pool_create(
	hx_os_context *context, const hx_os_memory_pool_options *options,
	hx_os_memory_pool **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_memory_pool_alloc(
	hx_os_memory_pool *pool, size_t bytes, size_t alignment, void **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_memory_pool_realloc(
	hx_os_memory_pool *pool, void **memory, size_t bytes,
	hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_memory_pool_free(
	hx_os_memory_pool *pool, void *memory);

HX_OS_API void HX_OS_CALL hx_os_memory_pool_collect(hx_os_memory_pool *pool);

HX_OS_API void HX_OS_CALL hx_os_memory_pool_stats(
	hx_os_memory_pool *pool, hx_os_memory_stats *stats);

HX_OS_API hx_os_status HX_OS_CALL hx_os_memory_pool_destroy(
	hx_os_memory_pool **pool, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_object_pool_create(
	hx_os_context *context, size_t object_bytes, size_t alignment,
	size_t maximum_objects, hx_os_object_pool **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_object_pool_acquire(
	hx_os_object_pool *pool, void **out, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_object_pool_release(
	hx_os_object_pool *pool, void *object);
HX_OS_API hx_os_status HX_OS_CALL hx_os_object_pool_destroy(
	hx_os_object_pool **pool, hx_os_error *error);

/* Arena operations require caller serialization. Reset invalidates all data. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_arena_create(hx_os_context *context,
	size_t block_bytes, size_t maximum_bytes, hx_os_arena **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_arena_alloc(hx_os_arena *arena,
	size_t bytes, size_t alignment, void **out, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_arena_reset(hx_os_arena *arena);

HX_OS_API void HX_OS_CALL hx_os_arena_destroy(hx_os_arena *arena);

#ifdef __cplusplus
}
#endif
#endif
