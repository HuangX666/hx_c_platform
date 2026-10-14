# Bounded LRU Cache

[简体中文](CACHE.zh-CN.md)

## 1. Keep recently used results

`hx_os_cache` stores copied binary keys and fixed-size values. A successful `get` makes its entry most recently used; `peek` reads without changing the order. When a successful insertion exceeds `max_items`, an expired entry is removed first if available; otherwise the least recently used entry is evicted. Updating an existing key also makes it recent.

Use this for recently queried device metadata or calculation results. It is not a database, a persistent store, or a thread-safe message queue. Optional per-entry expiry uses the monotonic clock and no background worker. Reading never renews the lifetime. Enable `HX_OS_ENABLE_CONTAINERS` to use it; disabled builds retain the cache API and return `HX_OS_NOT_SUPPORTED` on creation.

## 2. C usage

```c
#include <hx_cache.h>

hx_os_container_options options;
hx_os_cache *cache = NULL;
int temperature = 26;
const void *value = NULL;

hx_os_container_options_init(&options, sizeof(temperature));
options.max_items = 128;
options.max_bytes = 256 * 1024;

hx_os_status status = hx_os_cache_create(NULL, &options, &cache, NULL);
if (status == HX_OS_OK)
	status = hx_os_cache_put(cache, "sensor-01", 9, &temperature, NULL);
if (status == HX_OS_OK)
	status = hx_os_cache_get(cache, "sensor-01", 9, &value, NULL);

/* On success, read *(const int *)value before replacing/removing its entry. */
(void)hx_os_cache_destroy(&cache, NULL);
```

A key is a byte sequence with an explicit length, so embedded NUL bytes are supported and `"a"` differs from `"a\0"`. `NULL, 0` denotes an empty key. A missing key returns `HX_OS_NOT_FOUND` and clears the output pointer.

The complete [C example](../examples/lru_cache.c) reads sensor A before inserting C into a two-entry cache, demonstrating that B is evicted. It also stores a temporary value for 10 ms and reads it after expiry. Build the example target `hx_os_lru_cache` in your existing examples-enabled build directory.

## 3. C++11 usage

```cpp
#include <hx/Cache.hpp>
#include <cstdio>

int main()
{
	auto cache = hx::LruCache<int>::create(128, 256 * 1024);
	if (!cache)
		return 1;

	auto saved = cache.value().put("sensor-01", 26);
	if (!saved)
		return 1;

	auto value = cache.value().get("sensor-01");
	if (!value)
		return 1;

	std::printf("Temperature: %d\n", *value.value());

	return 0;
}
```

The wrapper uses `Result`, requires no exceptions and accepts trivially copyable values. Pointer values remain borrowed. Use the C `copy`/`drop` callbacks for resource-owning values; the existing string-container options also work for `hx_string` values.

## 4. Add a lifetime when writing

```c
/* Keep this result for five seconds from this write. */
status = hx_os_cache_put_for(cache, "sensor-01", 9, &temperature, 5000,
	NULL);

/* Alternatively, reuse an existing monotonic deadline. */
status = hx_os_cache_put_until(cache, "sensor-01", 9, &temperature,
	hx_os_deadline_after_ms(5000), NULL);

size_t removed = 0;
status = hx_os_cache_purge_expired(cache, &removed, NULL);
```

C++ equivalents are `putFor("sensor-01", 26, 5000)`, `putUntil(key, value, deadline)`, and `purgeExpired()`, which returns `Result<size_t>` containing the number removed. Check each result before continuing, as with ordinary `put`.

| Operation | Expiry behavior |
|---|---|
| `put` / `put_take` | Store without expiry; replacement removes a previous lifetime. |
| `put_for` / `put_take_for` | Relative lifetime in milliseconds, sampled after value copying and before index publication. Overflow saturates to the largest finite deadline. |
| `put_until` / `put_take_until` | Absolute deadline from this library's monotonic clock. `HX_OS_DEADLINE_INFINITE` disables expiry. Do not pass wall-clock timestamps. |
| `get` | An expired entry returns `NOT_FOUND`, counts as a miss, and is physically removed. Successful reads do not extend its lifetime. |
| `peek` | An expired entry returns `NOT_FOUND` without removing storage or changing statistics. |
| `purge_expired` | Explicitly remove every entry expired at one clock sample; return how many were removed. |
| `size` / `memory` | Report physically retained entries and allocated bytes, including expired entries not yet cleaned up. |
| `erase` | Explicitly remove retained storage, even when expired; a non-NULL output may take that value. |

Zero lifetime, `HX_OS_DEADLINE_NOW`, and already elapsed deadlines expire immediately. Such writes still honor copy/take and failure guarantees, but the new value is removed after successful insertion and never displaces a live item. There is no periodic timer. Time alone does not release memory: reads of expired keys, explicit purge, capacity cleanup, clear, or destruction perform the cleanup. Sleeping/suspending follows the library's monotonic-clock contract.

Capacity pressure prefers an expired entry before a live LRU entry. The cache keeps a conservative earliest-expiry bound to avoid rescanning while entries are still fresh; a full-cache insertion with TTL entries can nevertheless scan O(n). `purge_expired` is O(n) and does not allocate. Pure non-expiring caches retain the path without clock reads. Expiry cleanup does not increment `evictions`; that statistic continues to count capacity eviction of live entries.

Clock failures are reported before a logical write or expiry cleanup commits. Failed writes do not purge old entries first: if stale entries consume the byte budget, explicitly purge and retry. A successful `purge_expired` can change the count without changing any still-live value.

## 5. Ownership, failures and memory bounds

- `put` copies the key and constructs the value using the usual container callbacks. Without callbacks, the value is copied as bytes. `put_take` transfers the release obligation only on success; its source must not already belong to the cache.
- Eviction, replacement, clear and destruction call `drop` once per discarded value. `erase` with a non-NULL output transfers that value without calling `drop`; output storage must be uninitialized and outside the cache.
- Returned values are borrowed until their own entry is replaced, erased, evicted, cleaned up after expiry, or the cache is cleared/destroyed. The absence of a background worker keeps the pointer allocated when time passes, but does not make an expired value fresh. Reading another entry does not invalidate them. All concurrent access involving mutation, including `get`, requires external synchronization. Callbacks cannot reenter the cache.
- `max_bytes` is a hard limit for memory requested by the cache from its context: cache/context objects, index, copied keys, values, allocation headers, alignment slack, and temporary insertion storage. It excludes allocator-internal overhead, callback allocations and resources behind stored pointers. `memory()` reports currently charged bytes.
- The limit includes the old contents and a fully staged new entry. Leave room for one candidate and possible hash-bucket growth. If staging exceeds the budget, `put` returns `HX_OS_LIMIT` without evicting existing entries. An allocator failure returns `HX_OS_NO_MEMORY`. Failed insertions keep contents and recency unchanged, although index capacity may change.
- Eviction is triggered by `max_items`, not by the byte budget. This explicit policy avoids losing an old entry and then failing to allocate its replacement. An item larger than the budget cannot be stored even in an otherwise empty cache.
- `get_stats` returns saturating hit, miss and eviction counters. Only `get` updates hits/misses; only capacity eviction of a live entry updates evictions; expiry cleanup does not. `clear` preserves these counters.

The implementation reuses the existing hash map and maintains a doubly linked recency list. Lookup is expected O(1) excluding key hashing/comparison; a hit does not allocate. TTL lookup adds a clock read, and removal may execute the user drop callback. Key processing is O(key length), copying depends on the value callback, and hash growth may allocate. No unconditional speed claim is made against direct map access.
