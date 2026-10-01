# C containers

[简体中文](CONTAINERS.zh-CN.md) · [README](../README.md)

Include `hx_container.h`, or the C umbrella `hx_os.h`. The implementation is C99 and exposes only C types and opaque handles. No C++ runtime is required. `HX_OS_ENABLE_CONTAINERS=ON` is the default. OFF excludes sequence, tree and uthash implementation code; constructors return `HX_OS_NOT_SUPPORTED` and `HX_OS_CAP_CONTAINERS` is cleared. No global initialization is needed.

## Available containers

| Container | Representation and operations |
|---|---|
| vector | Contiguous array; indexed access O(1), amortized append O(1) |
| deque | Circular array; indexed access and reserved end operations O(1) |
| queue | FIFO names over deque; push at back, pop at front |
| stack | LIFO names over vector; push/pop at back |
| list | Doubly linked nodes; end operations and iterator steps O(1) |
| heap | Max-heap; push/pop O(log n), top at index zero |
| ordered map/set | AVL tree; find/insert/erase O(log n), sorted iteration |
| hash map/set | uthash 2.4.0; expected O(1) lookup, insertion-order iteration |

Complexities exclude element/key byte costs and callbacks. A list's indexed access/insertion/removal needs O(n) traversal; iterate with the iterator API for linear total traversal. Vector/deque middle operations are O(n). Growth moves live bytes and may allocate. Hash collisions can make operations O(n); the current hash is not a defense against deliberately colliding input.

The sequence types share `hx_os_sequence_*` operations. Queue/stack aliases are convenience names, not separate runtime implementations or DLL exports. A heap accepts insertion at the end and removal at index zero; arbitrary indexed changes return NOT_SUPPORTED. `hx_os_vector_sort` is an in-place, unstable heapsort. `hx_os_vector_lower_bound` requires an already sorted vector using the same strict weak ordering comparator.

These are general-purpose container building blocks, not the complete C++ STL: there is no template type checking, multimap, concurrent queue, splice, or general algorithm/iterator-category framework in this version.

## Options, allocation and bounds

Initialize `hx_os_container_options` with `hx_os_container_options_init(&options, sizeof(element_type))`. The options are copied at creation and cannot be changed through the original structure. Default limits are 1,048,576 elements and 16 MiB per container. Set smaller limits for an embedded workload. Both limits must be nonzero.

`element_alignment=0` requests normal fundamental allocation alignment. An explicit alignment must be a power of two, and element size must be a multiple of it. Use an appropriate alignment for over-aligned types. Vector stride is exactly `element_size`; passing `sizeof(T)` maintains T's stride. All values must support bitwise relocation, including during growth/sort. Self-referential values and live C++ objects requiring move constructors cannot be stored as values by this API. Store stable external pointers when their ownership model requires that.

`max_bytes` bounds the library's live allocation requests, including the container object, node/bucket metadata, alignment overhead, temporary values and simultaneous old/new buffers during growth. `sequence_memory` and `map_memory` report this same accounting. They do not include a custom allocator's own bookkeeping, allocations made by copy/drop callbacks, or heap graphs behind stored pointers. In particular, cloned hx_string contents are outside the container's byte budget. A replacement can fail LIMIT while the previous value remains valid because transactional copying needs space.

The supplied context owns allocation policy and remains busy while its container allocations exist. There is no global allocator replacement. Callback `user` data and any different callback context are borrowed and must remain valid through destruction. The allocator must support calls/releases on whichever threads the consumer uses. Containers themselves have no locks; exclude readers during mutation and externally serialize mutations/destroy. Different containers can be used independently, subject to callback safety.

Reserve is supported by arrays, including deque/heap; list returns NOT_SUPPORTED. Clear keeps array capacity and releases list/map nodes. Destroy releases all remaining values/storage and clears the owner's pointer; destroying an already NULL owner succeeds. No automatic shrinking is used.

## Copy, transfer and borrowed access

| Operation | Ownership on success | On failure |
|---|---|---|
| Insert without copy/drop | Copies plain bytes | Source unchanged |
| Insert with copy callback | Constructs an independent owned value | Source retained |
| Insert with drop but no copy | NOT_SUPPORTED | Source retained |
| `_take` insertion | Transfers the release obligation | Caller still owns value |
| Pop/erase with non-NULL output | Transfers bytes/ownership to caller | Output unchanged |
| Pop/erase with NULL output | Calls drop and removes value | Old value retained |
| Clear/destroy | Drops all retained values | BUSY preserves container |

A successful `_take` does not zero its source bytes. Treat the source as moved-from and do not drop it a second time. The source cannot be a value already owned by that container. A pop/erase output must be suitably sized, uninitialized external storage, outside all storage owned by the container. Overlap checks catch common errors but are not a complete pointer validator. Pointer elements are copied as pointers unless copy/drop implement deep ownership. Callbacks must clean their own partial destination on copy failure; the container calls drop only for successfully constructed live values.

Callbacks must not throw, longjmp or mutate the same container, including from a comparison used for a read. Comparators must remain consistent. Mutation paths reject reentrant writes with BUSY; this is not a lock or a general callback reentry guarantee. A callback may use a different container if it does not create recursion back into the active one.

`sequence_at`, map lookup and iterator entries return borrowed pointers. `vector_data` exposes a contiguous mutable array only for vector/stack; other representations return NULL. Direct edits must preserve valid owned values and ordering preconditions. Conservatively discard borrowed pointers after mutation. All successful structural operations, replacement, sort, clear and reserve invalidate iterators; a stale generation returns INVALID while the owner still exists. Iterators never retain the owner: using one after container destruction is invalid. Iterator begin/next return EOF at the end; empty pop/missing key returns NOT_FOUND, not EOF. Outputs are only usable on OK. Failed mutations preserve logical contents, although capacity or hash metadata can change.

## Keys and sets

Create maps with `HX_OS_MAP_HASH` or `HX_OS_MAP_ORDERED`. Keys are copied byte sequences, with explicit byte lengths; embedded NUL and zero-length keys are valid. The common maximum key length is UINT_MAX. No locale, UTF-8 validation, normalization or character conversion is applied to keys. Use hx_string and text conversion before insertion when these semantics are needed.

Hash keys compare by exact bytes and reject a custom key comparator. Ordered maps use a supplied comparator or unsigned-byte lexicographic ordering. Integer binary keys therefore need a comparator for numeric sorting. Keys have only byte alignment: read a typed key with memcpy, not an aligned cast. The comparator receives the options' borrowed user data. Replacing a value preserves the original key bytes and iteration position.

Set `element_size=0`, with no copy/drop and NULL values, for a set. Use the same map operations; successful set lookup returns OK with a NULL value. `replace=0` rejects an existing key with EXISTS. `replace!=0` permits upsert. Map values follow the same copy/take/drop contracts as sequence values.

## Examples

The standalone [queue example](../examples/containers.c) is compiled as C99 and is also built by the installed-consumer checks. For strings, use the supplied deep-copy preset rather than a byte copy of an owning hx_string:

~~~c
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
/* On OK the vector owns an independent copy. Inspect status in the host. */
hx_string_destroy(&source);
(void)hx_os_sequence_destroy(&names, NULL);
~~~

For a custom context, pass the same context to the preset and constructor. Extracting a string into external storage transfers its ownership; call `hx_string_destroy` on the extracted value when finished. If transferring a string into the container with `_take`, reset the source to `HX_STRING_INIT` without destroying the transferred contents.
