# C containers

[简体中文](CONTAINERS.zh-CN.md) · [README](../README.md)

When a C99 project needs growable arrays, double-ended queues, doubly linked lists, binary heaps, ordered trees (AVL or red-black), or hash maps—with explicit element-count and memory-budget caps so untrusted or bursty workloads cannot exhaust the heap—include `<hx_container.h>` (or the C umbrella header `<hx_os.h>`). Every container is implemented in pure C99 behind opaque handles and typed option structs, requiring no C++ runtime and no global initialization.

A minimal runnable program is provided in [containers.c](../examples/containers.c): it initializes container options for an `int`, creates a bounded FIFO queue (`hx_os_queue`), pushes `42`, pops it into local storage, and destroys the container. Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build the `hx_os_containers` target (no arguments or input files are needed):

```text
Queue result: 42
```

For sorted key lookup and duplicate keys, build and run `hx_os_ordered_map` ([ordered-map program](../examples/ordered_map.c)), which traverses a red-black tree in key order (`a`, `b`, `b`) while retaining both values associated with `b`. If you are new to the library, start with [storing two integers and taking items in order](USAGE.md#containers), or drop the example source into the [minimal project](USAGE.md#c-project).

> [!NOTE]
> The container module is controlled by `HX_OS_ENABLE_CONTAINERS` (`ON` by default). Setting it to `OFF` excludes the sequence, tree, and `uthash` implementations from the build, clears `HX_OS_CAP_CONTAINERS`, and causes container constructors to return `HX_OS_NOT_SUPPORTED`.
>
> **Related data structures**:
> - Bounded recency eviction and TTL expiry: [LRU caches](CACHE.md)
> - Dense boolean flags and word-wise set operations: [dynamic bitsets](BITSET.md)
> - Zero-allocation fixed byte buffers with two-span borrowing: [fixed byte rings](RING.md)
> - Cross-thread message passing (controlled independently by `HX_OS_ENABLE_QUEUES` without a coroutine runtime): [message/ring queues](QUEUES.md)

## Available containers

When a single container needs to hold heterogeneous values (numbers, strings, and custom structs side by side), pair it with the [variant module](VARIANT.md): `hx_variant_container_options_init` populates deep-copy and drop callbacks for `vector`, `queue`/`deque`, and `map` values. Owning C `hx_variant` structs can be bitwise-relocated by these containers, whereas C++ `hx::Variant` wrapper objects must not be passed to raw byte-copy C container APIs.

| Container | Underlying representation and complexity |
|---|---|
| `vector` | Contiguous dynamic array; indexed access `O(1)`, amortized back append `O(1)`, middle insert/erase `O(n)` |
| `deque` | Circular array; indexed access `O(1)` and front/back push/pop `O(1)` when capacity is available |
| `queue` | FIFO convenience alias over `deque` (push at back, pop at front) |
| `stack` | LIFO convenience alias over `vector` (push and pop at back) |
| `list` | Doubly linked list; front/back operations and iterator steps `O(1)`, indexed lookup `O(n)` |
| `heap` | Contiguous max-heap; push and pop-top `O(log n)`, maximum element always at index `0` |
| ordered `map` / `set` | AVL tree by default or opt-in red-black tree (`HX_OS_MAP_RED_BLACK`); `O(log n)` find/insert/erase, sorted iteration |
| `multimap` / `multiset` | Bitwise-OR `HX_OS_MAP_MULTI` with AVL, red-black, or hash map kinds to retain duplicate keys |
| hash `map` / `set` | `uthash 2.4.0`; expected `O(1)` lookup/insert/erase (`O(n)` worst-case under hash collisions), insertion-order iteration |

Key operational characteristics:
- Complexities count element operations and exclude key/value byte copies and custom callbacks. Because indexed operations on a `list` require `O(n)` traversal, always use the iterator API for linear `O(n)` full scans.
- All sequence types share the `hx_os_sequence_*` functions; `queue` and `stack` are readable aliases rather than separate runtime types or DLL exports.
- A `heap` allows insertion at the back and removal at index `0`; arbitrary indexed modifications return `HX_OS_NOT_SUPPORTED` to protect the heap invariant.
- `hx_os_vector_sort` runs an in-place, unstable heapsort, and `hx_os_vector_lower_bound` requires the vector to be sorted with the same strict weak ordering comparator.
- Sequences and maps contain **no internal mutexes**. Synchronize shared access externally, or use the dedicated concurrent queues documented in [queue contracts](QUEUES.md).

## Options, allocation and bounds

Initialize `hx_os_container_options` with `hx_os_container_options_init(&options, sizeof(element_type))`. The container copies the options struct at creation time, so later edits to the local struct have no effect on existing containers.
- **Default bounds**: Each container defaults to at most `1,048,576` elements (`max_items`) and `16 MiB` of live library allocations (`max_bytes`). Both limits must be non-zero; tune them downward for memory-constrained embedded targets.
- **Alignment and bitwise relocation**: `element_alignment = 0` selects standard fundamental allocation alignment. Any explicit alignment must be a power of two that evenly divides `element_size`. Every stored element must support bitwise relocation (`memcpy`/`memmove` during growth or sorting). Self-referential structs and C++ objects requiring non-trivial move constructors cannot be stored by value—store stable pointers with explicit ownership instead.
- **Byte budget accounting (`max_bytes`)**: `max_bytes` caps the live bytes requested by the container from its `hx_os_context`, including the container header, node/bucket metadata, alignment padding, temporary staging values, and the simultaneous old and new buffers held during array growth. `hx_os_sequence_memory()` and `hx_os_map_memory()` report this exact accounting.
  - Note: Bookkeeping inside a custom allocator, heap allocations performed inside user `copy`/`drop` callbacks (such as the character buffer inside a deep-copied `hx_string` or `hx_variant`), and external object graphs behind stored pointers are **outside** `max_bytes`.
  - Replacing an element stages the new copy before retiring the old value so failures leave the container intact; consequently, an update can return `HX_OS_LIMIT` even when the element count does not grow.
- **Allocator context and destruction**: A container holds a lease on its `hx_os_context` while any container allocations remain alive (causing `hx_os_context_destroy` to return `HX_OS_BUSY`). Callback `user` pointers are borrowed and must outlive the container. Array-backed sequences (`vector`, `deque`, `heap`) support `hx_os_sequence_reserve`, whereas `list` returns `HX_OS_NOT_SUPPORTED`. `clear` drops all elements (keeping array capacity while freeing list/map nodes); `destroy` drops all remaining elements, frees all storage, and sets the caller's handle pointer to `NULL` (destroying an already `NULL` handle succeeds with `HX_OS_OK`).

## Copy, transfer and borrowed access

Containers support plain byte copies, deep resource management via `copy`/`drop` callbacks, and zero-copy ownership transfer via `_take` operations:

| Operation | Ownership on success | Behavior on failure |
|---|---|---|
| Insert without `copy`/`drop` | Copies plain element bytes into the container | Source and container unchanged |
| Insert with `copy` and `drop` | Calls `copy` to construct an independent owned value | Source remains with caller; container unchanged |
| Insert with `drop` but no `copy` | Rejected with `HX_OS_NOT_SUPPORTED` | Source remains with caller |
| `_take` insert (e.g., `push_back_take`) | Transfers bytes and the future `drop` obligation to the container | Caller retains ownership of the source value |
| `pop` / `erase` with non-`NULL` output | Moves element bytes and ownership into caller storage without calling `drop` | Output buffer unchanged; element stays in container |
| `pop` / `erase` with `NULL` output | Calls `drop` on the element and removes it | Element stays in container |
| `clear` / `destroy` | Calls `drop` on every remaining element | Container preserved if reentrant access returns `HX_OS_BUSY` |

> [!WARNING]
> **Critical ownership and iterator invalidation rules:**
> 1. **Handling moved-from `_take` sources**: A successful `_take` insertion does not zero the caller's source struct. Treat the source as moved-from (for example, reset a transferred `hx_string` to `HX_STRING_INIT`) and **never** call its destructor a second time. Never pass an element pointer borrowed from a container back into a `_take` call on the same container.
> 2. **External `pop` / `erase` buffers**: The destination buffer passed to `pop` or `erase` must be uninitialized external storage that does not overlap container memory. If a `copy` callback fails mid-construction, it must clean up its own partially constructed target; the container invokes `drop` only on values whose construction succeeded.
> 3. **No callback reentry**: `copy`, `drop`, and comparator callbacks must never throw exceptions, `longjmp`, or mutate the active container (even inside a comparator called during a read). Reentrant mutations are rejected with `HX_OS_BUSY`.
> 4. **Borrowed pointers and iterator invalidation**: `hx_os_sequence_at`, map lookups, and iterators return **borrowed pointers** (`hx_os_vector_data` returns a contiguous mutable array only for `vector`/`stack` and `NULL` for other containers). Any successful structural mutation, value replacement, sort, `clear`, or `reserve` invalidates borrowed pointers and iterators: using a stale iterator while the container is still alive returns `HX_OS_INVALID`, and using an iterator after container destruction is undefined (iterators do not keep the container alive). Stepping to the end of a sequence or map returns `HX_OS_EOF`, whereas popping an empty container or looking up a missing key returns `HX_OS_NOT_FOUND`.

## Keys and sets

Create maps with `HX_OS_MAP_HASH`, `HX_OS_MAP_ORDERED` (AVL tree), or `HX_OS_MAP_RED_BLACK`:
- **Binary key semantics**: Keys are copied byte sequences with an explicit length up to `UINT_MAX`; embedded `NUL` bytes and zero-length keys are valid. The map performs no UTF-8 validation, character encoding conversion, Unicode normalization, or locale collation—normalize text with `hx_string` before insertion when needed.
- **Key comparison rules**:
  - `HX_OS_MAP_HASH` compares keys by exact byte equality and rejects custom key comparators.
  - Ordered maps (`HX_OS_MAP_ORDERED` and `HX_OS_MAP_RED_BLACK`) use the supplied `key_compare` callback, or default to unsigned-byte lexicographic order (`memcmp` style) when `key_compare` is `NULL`. Supply a comparator when using binary integers as ordered keys so they sort numerically. Because stored keys have only byte alignment, copy typed keys into a local variable with `memcpy` inside your comparator rather than casting the unaligned key pointer directly.
- **Using a map as a `set`**: Set `options.element_size = 0`, leave `copy`/`drop` as `NULL`, and pass `NULL` for `value` on insertion. A successful lookup returns `HX_OS_OK` and sets the output value pointer to `NULL`.
- **Insert vs. upsert (`replace`)**: In single-key maps, `replace = 0` rejects existing keys with `HX_OS_EXISTS`, while `replace != 0` replaces the stored value while preserving the original key bytes and iteration order.

## Examples

The standalone [queue example](../examples/containers.c) is compiled as C99 and verified in installed-package builds. When storing dynamic strings (`hx_string`) in a container, initialize the options with `hx_os_string_container_options_init` so the container deep-copies and destroys each string properly rather than making shallow byte copies:

```c
hx_os_container_options options;
hx_os_vector *names = NULL;
hx_string source = HX_STRING_INIT;
hx_os_status status;

hx_os_string_container_options_init(&options, NULL);
options.max_items = 32;

status = hx_os_vector_create(NULL, &options, &names, NULL);
if (status == HX_OS_OK)
	status = hx_string_assign_cstr(&source, "中文", NULL);

if (status == HX_OS_OK)
	status = hx_os_sequence_push_back(names, &source, NULL);

// On HX_OS_OK the vector owns an independent copy of the string.
hx_string_destroy(&source);
(void)hx_os_sequence_destroy(&names, NULL);
```

When using a custom `hx_os_context`, pass the same context pointer to both `hx_os_string_container_options_init` and `hx_os_vector_create`. If you later extract an `hx_string` into caller storage via `pop` or `erase`, call `hx_string_destroy` on that extracted value when finished.

## Red-black maps and duplicate keys

Pass `HX_OS_MAP_RED_BLACK` to `hx_os_map_create` to select a red-black tree, or `HX_OS_MAP_ORDERED` to keep the AVL tree implementation. Bitwise-OR the map kind with `HX_OS_MAP_MULTI` to allow duplicate keys (`multimap` / `multiset`):
- In `HX_OS_MAP_MULTI` mode, `replace = 0` inserts an additional entry for the same key, while `replace = 1` updates one matching entry.
- Ordered trees keep duplicate keys in insertion order, and `hx_os_map_find` / `hx_os_map_erase` target the first matching entry. Lookup order among duplicate keys in a hash multimap is unspecified.

The complete [ordered-map program](../examples/ordered_map.c) inserts `b: 20`, `a: 10`, and `b: 21`, printing:

```text
a: 10
b: 20
b: 21
```

Creating the multimap and inserting duplicate keys requires only:

```c
hx_os_map_create(NULL, HX_OS_MAP_RED_BLACK | HX_OS_MAP_MULTI,
	&options, NULL, &map, &error);
hx_os_map_insert(map, "b", 1, &value, 0, &error);
```

Additional lookup and maintenance operations:
- **Ordered range queries**: `hx_os_map_lower_bound`, `hx_os_map_upper_bound`, and `hx_os_map_equal_range` operate on ordered trees (AVL and red-black).
- **Counting and bulk removal**: `hx_os_map_count` works across all backends; `hx_os_map_erase_all` removes every entry matching a key, even when the key slice borrows storage from inside the map itself.
- **Erase while iterating**: `hx_os_map_erase_at` (and `hx_os_sequence_erase_at` for sequences) removes the current entry and advances the supplied iterator to the next entry while invalidating other iterators.
- **Reverse iteration and validation**: `hx_os_map_last` and `hx_os_map_previous` traverse entries in reverse order; `hx_os_map_validate` verifies AVL/red-black tree invariants and node links without allocating memory.

## Traversal and algorithms

Declare your iterator and `hx_os_status` variables before invoking `HX_OS_SEQUENCE_FOR_EACH` or `HX_OS_MAP_FOR_EACH` (or their `_REVERSE` counterparts). Each macro evaluates the container pointer once, stops cleanly at `HX_OS_EOF` (setting `status` to `HX_OS_OK`), and preserves any error status; breaking out of the loop early leaves `status` as `HX_OS_OK`.

```c
hx_os_sequence_iterator iterator;
hx_os_status status;

HX_OS_SEQUENCE_FOR_EACH(iterator, values, status)
{
	const void *value;

	status = hx_os_sequence_value(&iterator, &value);
	if (status != HX_OS_OK)
		break;

	printf("%d\n", *(const int *)value);
}
```

The container module also provides sequence algorithms directly on container handles. For example, finding a single rank or the smallest `k` items does not require sorting the entire vector. In the table below, `n` and `m` are input lengths, `k` is a prefix length, `s` is `element_size`, and abbreviated function names omit the `hx_os_sequence_` prefix:

| Algorithm | API | When to use | Time and scratch storage |
| --- | --- | --- | --- |
| In-place unstable heapsort | `hx_os_vector_sort` | Sort a `vector` without allocating scratch memory | Worst-case `O(n log n)`; `O(1)` scratch |
| Bottom-up stable merge sort | `hx_os_sequence_stable_sort` | Sort while preserving the relative order of equal keys | `O(n log n)`; `vector`/`deque` use `O(n × s)` scratch, `list` relinks nodes in `O(1)` scratch |
| Bounded max-heap prefix sort | `hx_os_vector_partial_sort` | Obtain the smallest `k` entries in ascending order | `O(n log(k + 1))` for `k > 0` (`k == 0` is a no-op); `O(1)` scratch |
| Three-way rank selection with heapsort fallback | `hx_os_vector_nth_element` | Partition around a median or percentile without full sorting | Average linear, worst-case `O(n log n)`; 1 element of scratch (`vector` only) |
| Binary search | `hx_os_vector_lower_bound`, `hx_os_vector_upper_bound` | Locate keys or insertion indices in a sorted `vector` | `O(log n)`; `O(1)` scratch (must use the same comparator) |
| Linear predicate scan | `find_if`, `count_if`, `any_of`, `all_of`, `none_of` | Find/count matching items or test a predicate across the sequence | `O(n)` worst case; `O(1)` scratch; `find_if`/`any_of`/`all_of`/`none_of` short-circuit |
| Stable filter and adjacent deduplication | `remove_if`, `unique` | Erase unwanted entries or compact adjacent duplicates in place | `O(n)`; `O(1)` scratch; discarded elements invoke `drop` |
| Reverse, rotate, and predicate partition | `reverse`, `rotate`, `partition` | Reverse order, rotate left, or group matching items at the front | `O(n)`; `O(1)` scratch; `list` partition is stable, array partition is unstable |
| Clone, filter copy, and transform | `clone`, `copy_if`, `transform` | Produce a new independent `vector` without mutating the source | `O(n)`; up to `O(n × output element size)` in the result vector |
| Conditional replacement | `replace_if` | Replace every matching element with a new value | `O(n)`, 2 elements of scratch; copy failure preserves earlier replacements |
| Sequential visit and fold | `visit`, `fold` | Run a callback on each element or accumulate a summary | `O(n)`, `O(1)` internal scratch; stops on first failure and reports completed count |
| Two-input sorted merge and set operations | `merge` (`HX_OS_MERGE` and 4 `HX_OS_SET_*` modes) | Merge sorted sequences or compute multiset union/intersection/difference | `O(n + m)`; up to `O((n + m) × s)` in the result vector |
| Indexed list-range splice | `hx_os_list_splice` | Move existing nodes between compatible `list` containers without copying | `O(n + m)` to reach endpoints, then `O(1)` pointer relinking; zero allocation or `copy`/`drop` calls |

> [!TIP]
> - For dense integer flags (such as job IDs `0..N`), the [bitset algorithms](BITSET.md) (`hx_os_bitset_count`, `find_next`, `apply`, `intersects`, `is_subset`) process 32-bit words at a time in `O(ceil(bit_count / 32))` with `O(1)` scratch.
> - Temporary scratch memory used by sequence algorithms counts toward the container's `max_bytes` budget. If scratch allocation fails before sorting begins, the container contents and existing iterators remain valid. `hx_os_list_splice` checks that both lists share compatible contexts and element policies and transfers the accounted node bytes to the destination list.
