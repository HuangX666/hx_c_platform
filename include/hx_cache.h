#ifndef HX_C_PLATFORM_CACHE_H
#define HX_C_PLATFORM_CACHE_H

#include "hx_container.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_cache hx_os_cache;

typedef struct hx_os_cache_stats {
	uint64_t hits;
	uint64_t misses;
	uint64_t evictions;
} hx_os_cache_stats;

/**
 * \brief Bounded LRU cache using binary, copied keys and fixed-size values.
 *
 * \details options uses the container copy/drop, alignment and ownership
 * contracts. max_items limits retained entries; successful insertion evicts the
 * least recently used entry when full. max_bytes includes all cache
 * allocations, metadata and temporary insertion storage, but not callback
 * allocations or resources referenced by values. Allocator bookkeeping is also
 * excluded. Insufficient staging/rehash headroom returns LIMIT without
 * pre-eviction. Failed put preserves logical entries and recency; capacity may
 * change. NULL context uses the default allocator. No internal synchronization.
 * Callbacks must not reenter this cache; status-returning calls report BUSY.
 * Available with HX_OS_ENABLE_CONTAINERS; disabled creation is NOT_SUPPORTED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_cache **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put(hx_os_cache *cache,
	const void *key, size_t key_bytes, const void *value,
	hx_os_error *error);

/**
 * \brief Transfers value ownership only on success.
 *
 * \details Source bytes are unchanged. A take source must not belong to this
 * cache, including another entry.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put_take(hx_os_cache *cache,
	const void *key, size_t key_bytes, void *value, hx_os_error *error);

/**
 * \brief Expire after write: get never extends lifetime.
 *
 * \details for uses milliseconds, sampled just before publishing the staged
 * value; until uses this library's absolute monotonic deadline. Zero
 * lifetime/NOW expires immediately; INFINITE in until means no expiry. for
 * overflow saturates to the largest finite deadline. Ordinary put/put_take
 * removes any previous expiry. Native clock failures preserve old entries and
 * are reported in error.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put_for(hx_os_cache *cache,
	const void *key, size_t key_bytes, const void *value, uint64_t ttl_ms,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put_until(hx_os_cache *cache,
	const void *key, size_t key_bytes, const void *value,
	hx_os_deadline expires, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put_take_for(hx_os_cache *cache,
	const void *key, size_t key_bytes, void *value, uint64_t ttl_ms,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_put_take_until(hx_os_cache *cache,
	const void *key, size_t key_bytes, void *value, hx_os_deadline expires,
	hx_os_error *error);

/**
 * \brief get counts hits/misses and marks a hit most recently used; peek does
 * neither.
 *
 * \details Both report expired entries as NOT_FOUND and clear *value. get drops
 * an expired entry; const peek leaves its storage retained. NULL key is valid
 * only for zero bytes. Borrowed values remain allocated until replacement,
 * erasure, eviction, expiry cleanup or cache destruction. Time alone does not
 * free a value, but it no longer represents fresh data. Exclude concurrent
 * mutation, including expiry cleanup and get.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_get(hx_os_cache *cache,
	const void *key, size_t key_bytes, const void **value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_peek(const hx_os_cache *cache,
	const void *key, size_t key_bytes, const void **value,
	hx_os_error *error);

/**
 * \brief Non-NULL out transfers the value without calling drop, as in
 * containers.
 *
 * \details Output must be uninitialized and outside cache-owned storage. This
 * is an explicit removal of retained storage, even if the value already
 * expired.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_erase(hx_os_cache *cache,
	const void *key, size_t key_bytes, void *out, hx_os_error *error);

/**
 * \brief Physical retained count, including expired entries not yet cleaned up.
 */
HX_OS_API size_t HX_OS_CALL hx_os_cache_size(const hx_os_cache *cache);
HX_OS_API size_t HX_OS_CALL hx_os_cache_memory(const hx_os_cache *cache);

/**
 * \brief Remove all entries expired at one clock sample, without a background
 * job.
 *
 * \details O(n), no allocations; removed is required. A failed clock read
 * changes nothing. Normal put only searches for expired victims when over
 * capacity; expiry cleanup does not increment the capacity-eviction statistic.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_purge_expired(
	hx_os_cache *cache, size_t *removed, hx_os_error *error);

/**
 * \brief Counters saturate at UINT64_MAX; clear preserves accumulated
 * statistics.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_get_stats(
	const hx_os_cache *cache, hx_os_cache_stats *stats);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_clear(
	hx_os_cache *cache, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cache_destroy(
	hx_os_cache **cache, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
