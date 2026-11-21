# Bounded LRU Cache

[简体中文](CACHE.zh-CN.md)

When your application repeatedly looks up device metadata, routing records, or expensive calculation results and needs to keep only the most recently accessed entries within strict item-count and memory-budget limits (with optional monotonic-clock TTL expiry), use the bounded LRU cache `hx_os_cache` (or `hx::LruCache<T>` in `<hx/Cache.hpp>` for C++11).

To see it in action, open [lru_cache.c](../examples/lru_cache.c): the program creates a two-entry cache, inserts `sensor-a` and `sensor-b`, reads `sensor-a` with `hx_os_cache_get` (promoting it to most recently used), inserts `sensor-c` (which evicts the least recently used `sensor-b`), inserts a `temporary` entry with a `10 ms` TTL and queries it after expiry, and finally destroys the cache.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_lru_cache` (no command-line arguments or input files are required):

```text
Recent sensor-a: 20
sensor-b evicted
temporary expired
```

You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

## 1. Keep recently used results

`hx_os_cache` combines a hash table (expected `O(1)` lookup) with a doubly linked recency list to store copied binary keys and fixed-size values:
- **Recency ordering**: A successful `hx_os_cache_get` promotes the entry to the most-recently-used (MRU) position; `hx_os_cache_peek` inspects the value without changing recency order or hit/miss counters. Updating an existing key also promotes it to MRU.
- **Eviction priority**: When inserting a new key would exceed `options.max_items`, the cache first removes an expired entry if one exists; otherwise it evicts the least-recently-used (LRU) live entry at the tail of the list.
- **Monotonic TTL and CMake switch**: Per-entry lifetimes use the library's monotonic clock, **reads do not extend an entry's TTL**, and no background worker thread is spawned. Enable `HX_OS_ENABLE_CONTAINERS=ON` to use the cache; when disabled, the cache symbols remain exported and creation returns `HX_OS_NOT_SUPPORTED`.

## 2. C usage

Include `<hx_cache.h>` and initialize `hx_os_container_options` with the value size, `max_items`, and `max_bytes`:

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

// On HX_OS_OK, read *(const int *)value before replacing/removing its entry.
(void)hx_os_cache_destroy(&cache, NULL);
```

- **Binary keys**: Keys are byte sequences with an explicit length, so embedded `NUL` bytes are preserved (`"a"` of length 1 and `"a\0"` of length 2 are distinct keys, and `NULL, 0` represents an empty key).
- **Missing or expired lookups**: `hx_os_cache_get` and `hx_os_cache_peek` return `HX_OS_NOT_FOUND` and set the output `value` pointer to `NULL` when a key is absent or expired. See the complete [C example](../examples/lru_cache.c) for eviction and TTL expiry in context.

## 3. C++11 usage

Include `<hx/Cache.hpp>` to use `hx::LruCache<T>`, whose methods return `hx::Result` without throwing exceptions:

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

`hx::LruCache<T>` accepts trivially copyable value types `T` (pointer types are stored as borrowed addresses). To store values that own dynamic resources, use the C API with `options.copy` and `options.drop` callbacks (for example, `hx_os_string_container_options_init` configures deep-copy and drop callbacks for `hx_string` values).

## 4. Add a lifetime when writing

Alongside non-expiring `hx_os_cache_put`, you can attach a relative duration in milliseconds (`put_for`) or an absolute monotonic deadline (`put_until`) when writing an entry, and call `hx_os_cache_purge_expired` at any time to sweep expired entries:

```c
// Keep this result for five seconds from this write.
status = hx_os_cache_put_for(cache, "sensor-01", 9, &temperature, 5000,
	NULL);

// Alternatively, reuse an existing monotonic deadline.
status = hx_os_cache_put_until(cache, "sensor-01", 9, &temperature,
	hx_os_deadline_after_ms(5000), NULL);

size_t removed = 0;
status = hx_os_cache_purge_expired(cache, &removed, NULL);
```

In C++11, call `putFor("sensor-01", 26, 5000)`, `putUntil(key, value, deadline)`, and `purgeExpired()` (which returns `hx::Result<size_t>` with the number of purged entries).

| Operation (`hx_os_cache_*`) | Expiry behavior |
|---|---|
| `put` / `put_take` | Stores the entry without an expiry deadline; updating an existing key removes any previous TTL |
| `put_for` / `put_take_for` | Sets a relative lifetime in milliseconds, sampled after value construction and before index publication (overflow saturates to the largest finite deadline) |
| `put_until` / `put_take_until` | Sets an absolute deadline from the library's monotonic clock (`HX_OS_DEADLINE_INFINITE` disables expiry; never pass wall-clock Unix timestamps) |
| `get` | If the entry has expired, returns `HX_OS_NOT_FOUND`, increments the miss counter, and physically removes the entry; hits do not extend TTL |
| `peek` | If the entry has expired, returns `HX_OS_NOT_FOUND` without freeing storage or updating hit/miss counters |
| `purge_expired` | Samples the monotonic clock once, removes every expired entry (`O(n)` scan, zero allocation), and outputs the removed count |
| `size` / `memory` | Reports physically retained entries and charged bytes, including expired entries that have not been swept yet |
| `erase` | Removes a key's storage even if expired; passing a non-`NULL` output buffer transfers ownership of the stored value |

> [!NOTE]
> **Lazy cleanup and immediate expiry details:**
> - Because the cache runs no background timer thread, the passage of time alone does not free memory; expired entries are reclaimed when accessed by `get`, displaced during a full-cache insertion, swept by `purge_expired`, or cleared/destroyed. If uncollected expired entries fill `max_bytes` and cause a write to return `HX_OS_LIMIT`, call `hx_os_cache_purge_expired` and retry the write.
> - A lifetime of `0`, `HX_OS_DEADLINE_NOW`, or a deadline already in the past expires immediately: the call still validates and copies/takes the input value, then removes the newly staged entry immediately without evicting any live item.
> - The cache tracks a conservative lower bound on the earliest expiry timestamp so insertions do not rescan the table while all TTL entries are still fresh, and caches that never use TTL entries skip clock reads entirely.

## 5. Ownership, failures and memory bounds

- **Value ownership**: `put` copies the key and constructs the value using `options.copy` (or raw bytes when no callback is set). `put_take` transfers the `drop` obligation only on success (never pass a pointer borrowed from inside the cache back to `put_take`). Eviction, replacement, expiry removal, `clear`, and `destroy` invoke `drop` once per discarded value; `erase` with a non-`NULL` uninitialized external output buffer transfers ownership to the caller without calling `drop`.
- **Borrowed pointer lifetime and concurrency**: Pointers returned by `get` and `peek` are borrowed until that entry is replaced, erased, evicted, purged after expiry, or the cache is cleared/destroyed (reading other keys does not invalidate them). Because `get` mutates the recency list and removes expired entries, synchronize all concurrent access—including concurrent `get` calls—externally.
- **Hard memory budget (`max_bytes`) vs. item eviction (`max_items`)**:
  - `max_bytes` caps the memory requested by the cache from its `hx_os_context` (cache/context headers, hash index, copied keys, values, allocation headers, alignment slack, and temporary staging storage during insertion; allocator-internal overhead, nested callback allocations, and external pointees are excluded).
  - Every `put` stages the new candidate entry before modifying the index or evicting an old item. Consequently, **eviction is triggered by `max_items`, not by `max_bytes`**—if staging the new entry would exceed `max_bytes`, `put` returns `HX_OS_LIMIT` without evicting existing entries (allocator failures return `HX_OS_NO_MEMORY`). Size `max_bytes` with room for at least one staged candidate and hash-bucket growth.
- **Cache statistics (`hx_os_cache_get_stats`)**: Reports saturating `hits`, `misses`, and `evictions` counters (`UINT64_MAX` ceiling). Only `get` updates `hits` and `misses`, and only capacity eviction of a **live** entry increments `evictions` (purging expired entries does not). `clear` resets stored entries while preserving cumulative statistics.
