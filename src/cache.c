#include "hx_cache.h"
#include "containers_private.h"

#if HX_OS_ENABLE_CONTAINERS

typedef struct hx_os_cache_entry {
	struct hx_os_cache_entry *previous;
	struct hx_os_cache_entry *next;
	void *value;
	const void *key;
	size_t key_bytes;
	hx_os_deadline expires;
} hx_os_cache_entry;

struct hx_os_cache {
	hx_os_container_base base;
	hx_os_context *storage;
	hx_os_map *index;
	hx_os_cache_entry *first;
	hx_os_cache_entry *last;
	size_t value_offset;
	size_t expiring;
	hx_os_deadline earliest;
	hx_os_cache_stats stats;
};

static void *hx_os_cache_allocate(size_t bytes, void *user)
{
	hx_os_cache *cache = user;

	return hx_os_i_container_alloc(&cache->base, bytes, 0);
}

static void hx_os_cache_deallocate(void *memory, void *user)
{
	hx_os_cache *cache = user;

	hx_os_i_container_free(&cache->base, memory);
}

static void hx_os_cache_count(uint64_t *counter)
{
	if (*counter != UINT64_MAX)
		++*counter;
}

static void hx_os_cache_unlink(hx_os_cache *cache, hx_os_cache_entry *entry)
{
	if (entry->previous)
		entry->previous->next = entry->next;
	else
		cache->first = entry->next;

	if (entry->next)
		entry->next->previous = entry->previous;
	else
		cache->last = entry->previous;
}

static void hx_os_cache_prepend(hx_os_cache *cache, hx_os_cache_entry *entry)
{
	entry->previous = NULL;
	entry->next = cache->first;
	if (cache->first)
		cache->first->previous = entry;
	else
		cache->last = entry;

	cache->first = entry;
}

static void hx_os_cache_release(
	hx_os_cache *cache, hx_os_cache_entry *entry, void *out)
{
	hx_os_cache_unlink(cache, entry);
	if (entry->expires != HX_OS_DEADLINE_INFINITE && !--cache->expiring)
		cache->earliest = HX_OS_DEADLINE_INFINITE;
	if (out)
		memcpy(out, entry->value, cache->base.options.element_size);
	else
		hx_os_i_container_drop(&cache->base, entry->value);

	hx_os_i_container_free(&cache->base, entry);
}

static int hx_os_cache_expired(const hx_os_cache_entry *entry, uint64_t now)
{
	return entry->expires != HX_OS_DEADLINE_INFINITE &&
		entry->expires <= now;
}

static void hx_os_cache_remove(hx_os_cache *cache, hx_os_cache_entry *entry)
{
	(void)hx_os_map_erase(
		cache->index, entry->key, entry->key_bytes, NULL, NULL);
	hx_os_cache_release(cache, entry, NULL);
}

static hx_os_cache_entry *hx_os_cache_victim(hx_os_cache *cache, uint64_t now)
{
	if (cache->expiring && cache->earliest <= now) {
		hx_os_cache_entry *entry;
		hx_os_deadline earliest = HX_OS_DEADLINE_INFINITE;

		for (entry = cache->last; entry; entry = entry->previous) {
			if (hx_os_cache_expired(entry, now))
				return entry;
			if (entry->expires < earliest)
				earliest = entry->expires;
		}

		/* Deletion may leave an early bound; this scan refreshes it. */
		cache->earliest = earliest;
	}

	return cache->last;
}

static hx_os_status hx_os_cache_lookup(const hx_os_cache *cache,
	const void *key, size_t key_bytes, hx_os_cache_entry **entry)
{
	const void *stored = NULL;
	hx_os_status status =
		hx_os_map_find(cache->index, key, key_bytes, &stored);

	*entry =
		status == HX_OS_OK ? *(hx_os_cache_entry *const *)stored : NULL;

	return status;
}

hx_os_status hx_os_cache_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_cache **out,
	hx_os_error *error)
{
	hx_os_container_base base;
	hx_os_cache *cache;
	hx_os_context_options storage_options;
	hx_os_container_options map_options;
	hx_os_status status;
	size_t alignment;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;

	status = hx_os_i_container_init(
		&base, context, options, sizeof(*cache), 0);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (options->max_items == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	alignment = options->element_alignment;
	if (alignment < HX_OS_I_ALIGNMENT)
		alignment = HX_OS_I_ALIGNMENT;
	if (alignment > SIZE_MAX - sizeof(hx_os_cache_entry))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	cache = hx_os_i_zero(context, sizeof(*cache));
	if (!cache)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	cache->base = base;
	cache->earliest = HX_OS_DEADLINE_INFINITE;
	cache->value_offset =
		(sizeof(hx_os_cache_entry) + alignment - 1) & ~(alignment - 1);

	hx_os_context_options_init(&storage_options);
	storage_options.allocator.allocate = hx_os_cache_allocate;
	storage_options.allocator.deallocate = hx_os_cache_deallocate;
	storage_options.allocator.user = cache;

	status = hx_os_context_create(&storage_options, &cache->storage, NULL);
	if (status == HX_OS_OK) {
		hx_os_container_options_init(
			&map_options, sizeof(hx_os_cache_entry *));
		map_options.max_items = options->max_items + 1;
		map_options.max_bytes = SIZE_MAX;

		status = hx_os_map_create(cache->storage, HX_OS_MAP_HASH,
			&map_options, NULL, &cache->index, NULL);
	}
	if (status != HX_OS_OK) {
		if (cache->base.allocation_status != HX_OS_OK)
			status = cache->base.allocation_status;

		(void)hx_os_context_destroy(&cache->storage, NULL);
		hx_os_memory_free(cache);

		return hx_os_i_error(error, status, 0, 0);
	}

	*out = cache;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_cache_store(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, int take, hx_os_deadline expires,
	uint64_t ttl_ms, int relative, hx_os_error *error)
{
	hx_os_cache_entry *entry, *old = NULL;
	hx_os_status status;
	size_t bytes;
	uint64_t now = 0;
	int copied, clock_failed = 0;

	if (!cache || (!key && key_bytes) || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (key_bytes > UINT_MAX ||
		!hx_os_i_add_size(cache->value_offset,
			cache->base.options.element_size, &bytes) ||
		!hx_os_i_add_size(bytes, key_bytes, &bytes))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_i_container_enter(&cache->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	(void)hx_os_cache_lookup(cache, key, key_bytes, &old);

	entry = hx_os_i_container_alloc(
		&cache->base, bytes, cache->base.options.element_alignment);
	if (!entry)
		return hx_os_i_container_leave(
			&cache->base, cache->base.allocation_status, error);

	entry->value = (unsigned char *)entry + cache->value_offset;
	entry->key = (unsigned char *)entry->value +
		cache->base.options.element_size;
	entry->key_bytes = key_bytes;
	if (key_bytes)
		memcpy((void *)entry->key, key, key_bytes);

	status =
		hx_os_i_container_copy(&cache->base, entry->value, value, take);
	copied = status == HX_OS_OK;
	if (copied &&
		(relative || expires != HX_OS_DEADLINE_INFINITE ||
			(!old && cache->expiring &&
				hx_os_map_size(cache->index) ==
					cache->base.options.max_items))) {
		status = hx_os_monotonic_read(&now, error);
		clock_failed = status != HX_OS_OK;
		if (relative && !clock_failed)
			expires = now >= UINT64_MAX - 1 ||
					ttl_ms >
						(UINT64_MAX - 1 - now) / 1000000
				? UINT64_MAX - 1
				: now + ttl_ms * 1000000;
	}

	entry->expires = expires;
	if (status == HX_OS_OK)
		status = hx_os_map_insert(
			cache->index, key, key_bytes, &entry, 1, NULL);
	if (status != HX_OS_OK) {
		if (copied && !take)
			hx_os_i_container_drop(&cache->base, entry->value);
		if (cache->base.allocation_status != HX_OS_OK)
			status = cache->base.allocation_status;

		hx_os_i_container_free(&cache->base, entry);

		return hx_os_i_container_leave(
			&cache->base, status, clock_failed ? NULL : error);
	}

	if (old)
		hx_os_cache_release(cache, old, NULL);

	hx_os_cache_prepend(cache, entry);
	if (expires != HX_OS_DEADLINE_INFINITE) {
		++cache->expiring;
		if (expires < cache->earliest)
			cache->earliest = expires;
	}

	if (hx_os_cache_expired(entry, now))
		hx_os_cache_remove(cache, entry);
	else if (hx_os_map_size(cache->index) > cache->base.options.max_items) {
		hx_os_cache_entry *victim = hx_os_cache_victim(cache, now);

		if (!hx_os_cache_expired(victim, now))
			hx_os_cache_count(&cache->stats.evictions);
		hx_os_cache_remove(cache, victim);
	}

	return hx_os_i_container_leave(&cache->base, HX_OS_OK, error);
}

hx_os_status hx_os_cache_put(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, hx_os_error *error)
{
	return hx_os_cache_store(cache, key, key_bytes, value, 0,
		HX_OS_DEADLINE_INFINITE, 0, 0, error);
}

hx_os_status hx_os_cache_put_take(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, hx_os_error *error)
{
	return hx_os_cache_store(cache, key, key_bytes, value, 1,
		HX_OS_DEADLINE_INFINITE, 0, 0, error);
}

hx_os_status hx_os_cache_put_for(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, uint64_t ttl_ms,
	hx_os_error *error)
{
	return hx_os_cache_store(
		cache, key, key_bytes, value, 0, 0, ttl_ms, 1, error);
}

hx_os_status hx_os_cache_put_until(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, hx_os_deadline expires,
	hx_os_error *error)
{
	return hx_os_cache_store(
		cache, key, key_bytes, value, 0, expires, 0, 0, error);
}

hx_os_status hx_os_cache_put_take_for(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, uint64_t ttl_ms, hx_os_error *error)
{
	return hx_os_cache_store(
		cache, key, key_bytes, value, 1, 0, ttl_ms, 1, error);
}

hx_os_status hx_os_cache_put_take_until(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, hx_os_deadline expires,
	hx_os_error *error)
{
	return hx_os_cache_store(
		cache, key, key_bytes, value, 1, expires, 0, 0, error);
}

hx_os_status hx_os_cache_peek(const hx_os_cache *cache, const void *key,
	size_t key_bytes, const void **value, hx_os_error *error)
{
	hx_os_cache_entry *entry;
	hx_os_status status;
	uint64_t now;

	if (!cache || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*value = NULL;
	if (cache->base.busy)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	status = hx_os_cache_lookup(cache, key, key_bytes, &entry);
	if (status == HX_OS_OK && entry->expires != HX_OS_DEADLINE_INFINITE) {
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;
		if (hx_os_cache_expired(entry, now))
			status = HX_OS_NOT_FOUND;
	}
	if (status == HX_OS_OK)
		*value = entry->value;

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_cache_get(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void **value, hx_os_error *error)
{
	hx_os_cache_entry *entry;
	hx_os_status status;
	uint64_t now;

	if (!cache || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*value = NULL;
	status = hx_os_i_container_enter(&cache->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_cache_lookup(cache, key, key_bytes, &entry);
	if (status == HX_OS_OK && entry->expires != HX_OS_DEADLINE_INFINITE) {
		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return hx_os_i_container_leave(
				&cache->base, status, NULL);
		if (hx_os_cache_expired(entry, now)) {
			hx_os_cache_remove(cache, entry);
			status = HX_OS_NOT_FOUND;
		}
	}
	if (status == HX_OS_OK) {
		hx_os_cache_unlink(cache, entry);
		hx_os_cache_prepend(cache, entry);
		hx_os_cache_count(&cache->stats.hits);
		*value = entry->value;
	}
	else if (status == HX_OS_NOT_FOUND)
		hx_os_cache_count(&cache->stats.misses);

	return hx_os_i_container_leave(&cache->base, status, error);
}

hx_os_status hx_os_cache_erase(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *out, hx_os_error *error)
{
	hx_os_cache_entry *entry;
	hx_os_status status;

	if (!cache)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_container_enter(&cache->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	status = hx_os_cache_lookup(cache, key, key_bytes, &entry);
	if (status == HX_OS_OK &&
		hx_os_i_container_overlap(out, cache->base.options.element_size,
			entry,
			cache->value_offset + cache->base.options.element_size +
				entry->key_bytes))
		status = HX_OS_INVALID;
	if (status == HX_OS_OK) {
		(void)hx_os_map_erase(cache->index, key, key_bytes, NULL, NULL);
		hx_os_cache_release(cache, entry, out);
	}

	return hx_os_i_container_leave(&cache->base, status, error);
}

size_t hx_os_cache_size(const hx_os_cache *cache)
{
	return cache ? hx_os_map_size(cache->index) : 0;
}

size_t hx_os_cache_memory(const hx_os_cache *cache)
{
	return cache ? cache->base.memory : 0;
}

hx_os_status hx_os_cache_purge_expired(
	hx_os_cache *cache, size_t *removed, hx_os_error *error)
{
	hx_os_cache_entry *entry;
	hx_os_status status;
	uint64_t now;

	if (!cache || !removed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*removed = 0;
	status = hx_os_i_container_enter(&cache->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (!cache->expiring)
		return hx_os_i_container_leave(&cache->base, HX_OS_OK, error);

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return hx_os_i_container_leave(&cache->base, status, NULL);

	cache->earliest = HX_OS_DEADLINE_INFINITE;
	entry = cache->last;
	while (entry) {
		hx_os_cache_entry *previous = entry->previous;

		if (hx_os_cache_expired(entry, now)) {
			hx_os_cache_remove(cache, entry);
			++*removed;
		}
		else if (entry->expires < cache->earliest)
			cache->earliest = entry->expires;

		entry = previous;
	}

	return hx_os_i_container_leave(&cache->base, HX_OS_OK, error);
}

hx_os_status hx_os_cache_get_stats(
	const hx_os_cache *cache, hx_os_cache_stats *stats)
{
	if (!cache || !stats)
		return HX_OS_INVALID;
	if (cache->base.busy)
		return HX_OS_BUSY;

	*stats = cache->stats;

	return HX_OS_OK;
}

hx_os_status hx_os_cache_clear(hx_os_cache *cache, hx_os_error *error)
{
	hx_os_status status;

	if (!cache)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_i_container_enter(&cache->base);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	(void)hx_os_map_clear(cache->index, NULL);
	while (cache->last)
		hx_os_cache_release(cache, cache->last, NULL);

	return hx_os_i_container_leave(&cache->base, HX_OS_OK, error);
}

hx_os_status hx_os_cache_destroy(hx_os_cache **cache, hx_os_error *error)
{
	hx_os_cache *value;
	hx_os_status status;

	if (!cache)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*cache)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *cache;
	status = hx_os_cache_clear(value, error);
	if (status != HX_OS_OK)
		return status;

	value->base.busy = 1;
	(void)hx_os_map_destroy(&value->index, NULL);
	(void)hx_os_context_destroy(&value->storage, NULL);
	hx_os_memory_free(value);
	*cache = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

#else

hx_os_status hx_os_cache_create(hx_os_context *context,
	const hx_os_container_options *options, hx_os_cache **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_cache_put(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, hx_os_error *error)
{
	(void)cache;
	(void)key;
	(void)key_bytes;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_cache_put_take(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, hx_os_error *error)
{
	return hx_os_cache_put(cache, key, key_bytes, value, error);
}

hx_os_status hx_os_cache_put_for(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, uint64_t ttl_ms,
	hx_os_error *error)
{
	(void)ttl_ms;

	return hx_os_cache_put(cache, key, key_bytes, value, error);
}

hx_os_status hx_os_cache_put_until(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void *value, hx_os_deadline expires,
	hx_os_error *error)
{
	(void)expires;

	return hx_os_cache_put(cache, key, key_bytes, value, error);
}

hx_os_status hx_os_cache_put_take_for(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, uint64_t ttl_ms, hx_os_error *error)
{
	return hx_os_cache_put_for(cache, key, key_bytes, value, ttl_ms, error);
}

hx_os_status hx_os_cache_put_take_until(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *value, hx_os_deadline expires,
	hx_os_error *error)
{
	return hx_os_cache_put_until(
		cache, key, key_bytes, value, expires, error);
}

hx_os_status hx_os_cache_peek(const hx_os_cache *cache, const void *key,
	size_t key_bytes, const void **value, hx_os_error *error)
{
	(void)cache;
	(void)key;
	(void)key_bytes;
	if (value)
		*value = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_cache_get(hx_os_cache *cache, const void *key,
	size_t key_bytes, const void **value, hx_os_error *error)
{
	return hx_os_cache_peek(cache, key, key_bytes, value, error);
}

hx_os_status hx_os_cache_erase(hx_os_cache *cache, const void *key,
	size_t key_bytes, void *out, hx_os_error *error)
{
	return hx_os_cache_put(cache, key, key_bytes, out, error);
}

size_t hx_os_cache_size(const hx_os_cache *cache)
{
	(void)cache;

	return 0;
}

size_t hx_os_cache_memory(const hx_os_cache *cache)
{
	(void)cache;

	return 0;
}

hx_os_status hx_os_cache_purge_expired(
	hx_os_cache *cache, size_t *removed, hx_os_error *error)
{
	(void)cache;
	if (removed)
		*removed = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_cache_get_stats(
	const hx_os_cache *cache, hx_os_cache_stats *stats)
{
	(void)cache;
	(void)stats;

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_os_cache_clear(hx_os_cache *cache, hx_os_error *error)
{
	(void)cache;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_cache_destroy(hx_os_cache **cache, hx_os_error *error)
{
	if (!cache)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_error(
		error, *cache ? HX_OS_NOT_SUPPORTED : HX_OS_OK, 0, 0);
}

#endif
