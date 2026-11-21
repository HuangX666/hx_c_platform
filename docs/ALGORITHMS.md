# General Array and Iterator Algorithms

[简体中文](ALGORITHMS.zh-CN.md) | [Containers](CONTAINERS.md)

When your program already holds data in plain C arrays or arrays of custom structs, you can sort, binary-search, filter, transform, and fold those records in place with `<hx_algorithm.h>`—without copying them into an `hx_os_vector` first. For C++11 codebases, `<hx/Algorithms.hpp>` exposes matching generic algorithms in the `hx::algo` namespace that operate on standard iterator ranges and node-based list containers.

To see the workflow in action, inspect [algorithms.c](../examples/algorithms.c). The program sorts an array of device records by ID, locates device `23` with `hx_os_array_lower_bound`, counts and copies devices with pending work using `hx_os_array_count_if` and `hx_os_array_copy_if`, and sums the total pending work via `hx_os_array_fold`.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build the `hx_os_algorithms` target. Running the executable (no command-line arguments or input files required) produces:

```text
Device 17, pending=0
Device 23, pending=1
Device 31, pending=1
Found device 23
Devices needing work: 2
Copied work items: 2
Pending work total: 2
```

You can also drop the example source directly into the [minimal project](USAGE.md#c-project) as `main.c` to experiment with your own record types.

## 1. Use the data you already have

The C99 functions in `<hx_algorithm.h>` operate directly on contiguous C arrays, while `<hx/Algorithms.hpp>` provides standard algorithm names in `hx::algo` for compatible C++11 iterators.

| Existing data | Recommended entry point |
| --- | --- |
| Fixed-width integer array (`int32_t` / `uint32_t` / `int64_t` / `uint64_t`) | `hx_os_sort_i32(values, count, error)` (plus `_u32`, `_i64`, `_u64` variants) |
| C array of scalars or custom structs | `hx_os_array_sort(data, count, sizeof(data[0]), compare, user, error)` |
| C++ raw array, `std::array`, `std::vector`, or random-access range | `hx::algo::sort(first, last, compare)` |
| C++ `std::list` or `std::forward_list` | `hx::algo::sort(list, compare)` (delegates to the container's stable node sort) |
| Existing `hx_os_vector` container | `hx_os_vector_sort` (shares the underlying array sorting implementation) |

> [!NOTE]
> The C algorithm functions require `HX_OS_ENABLE_CONTAINERS=ON` and linking against `hx_c_platform::hx_c_platform`. When that option is disabled, the C entry points return `HX_OS_NOT_SUPPORTED`. The header-only C++ templates in `<hx/Algorithms.hpp>` do not depend on that CMake switch; link `hx_c_platform::cpp` when consuming an installed package to obtain the include path.

## 2. Sort a C array

For 32-bit and 64-bit signed and unsigned integer arrays, built-in shortcuts handle sorting and binary search without requiring a custom comparator callback. This minimal program sorts four integers and prints `1 2 3 8`:

```c
#include <hx_algorithm.h>
#include <stdio.h>

int main(void)
{
	int32_t values[] = {8, 3, 1, 2};
	size_t count = sizeof(values) / sizeof(values[0]);
	size_t i;
	hx_os_status status;

	status = hx_os_sort_i32(values, count, NULL);
	if (status != HX_OS_OK)
		return 1;

	for (i = 0; i < count; ++i)
		printf("%d%c", (int)values[i], i + 1 == count ? '\n' : ' ');

	return 0;
}
```

When sorting custom struct arrays, your comparator callback inspects the target field and returns a negative, zero, or positive `int`. The algorithm relocates entire `sizeof(record)` blocks so all fields stay together. The [device example](../examples/algorithms.c) uses this pattern to sort `{id, pending}` structs by ID before querying and filtering them.

> [!TIP]
> Inside integer comparators, prefer `(a > b) - (a < b)` over `a - b` to prevent signed subtraction overflow from flipping the comparison sign.

## 3. Choose an operation

The table below summarizes the sorting, selection, and search functions in `<hx_algorithm.h>` (all names share the `hx_os_array_` prefix). Here `n` is the element count, `k` is the selected prefix length, and `s` is the element stride in bytes (`sizeof(element)`). Time complexities count element operations; comparator and byte-movement costs are additional.

| Function (`hx_os_array_*`) | Behavior | Implementation and space cost |
| --- | --- | --- |
| `sort` | Full in-place sort (unstable) | In-place heapsort; `O(n log n)` time, zero heap allocation |
| `stable_sort` | Stable full sort (equal keys keep their relative input order) | Bottom-up merge sort; `O(n log n)` time, `O(n × s)` scratch memory |
| `lower_bound` / `upper_bound` | First not-less / first greater insertion index in a sorted array | Binary search; requires sorted input, `O(log n)` comparisons, zero allocation |
| `binary_search` | Sets `found` to `1` if an equivalent key exists, or `0` otherwise | Binary search; requires sorted input, `O(log n)` time, zero allocation |
| `find_if` / `count_if` | First matching index / total number of matching elements | Linear scan `O(n)`; `find_if` stops at the first match |
| `minmax` | Indices of the first minimum and first maximum elements | Single-pass paired scan `O(n)`, zero allocation |
| `is_sorted` | Checks whether the array is in non-decreasing order | Linear scan stopping at the first inversion (`O(n)` worst case; true for empty arrays) |
| `reverse` / `rotate` | Reverse elements in place / rotate left by a given count | In-place swaps `O(n)`, zero allocation; rotation count is reduced modulo `n` |
| `partition` | Moves matching elements to the front and returns the prefix length | In-place two-pointer partition (unstable), `O(n)` time, zero allocation |
| `unique` | Compacts adjacent duplicate elements and returns the new length | Linear compaction `O(n)`, zero allocation; non-adjacent duplicates remain |
| `partial_sort` | Places the smallest `k` elements in sorted order at the front | Max-heap selection; `O(n log(k + 1))` for `k > 0` (`k == 0` is a no-op), zero allocation |
| `nth_element` | Places the element of rank `nth` in its final sorted position | Three-way partition with heapsort fallback; worst-case `O(n log n)`, 1 aligned element of scratch |

> [!IMPORTANT]
> **Boundary and return-code rules:**
> - `lower_bound` and `upper_bound` can return `count` when every element is smaller than the query key. Always verify `index < count` and check key equality before dereferencing, or call `hx_os_array_binary_search` when you only need a membership check.
> - `find_if` on a missing item and `minmax` on an empty array return `HX_OS_NOT_FOUND` without modifying output variables. The two index outputs passed to `minmax` must be distinct variables.
> - Out-of-range selection indices in `partial_sort` or `nth_element` return `HX_OS_RANGE`. Neither selection algorithm orders the remaining suffix or guarantees stability.

## 4. Scratch memory and record ownership

`hx_os_array_stable_sort` and `hx_os_array_nth_element` accept an `hx_os_context *context` and `size_t max_scratch_bytes`:
- Pass `NULL` for the default allocator and `0` for the default 16 MiB scratch budget.
- The budget covers library allocation headers and alignment slack (allocator-internal bookkeeping is separate), and all scratch memory is freed before the function returns.
- If the required scratch space exceeds the budget (`HX_OS_LIMIT`) or allocation fails (`HX_OS_NO_MEMORY`), the input array is left unchanged. Singleton `nth_element` calls allocate nothing; for larger inputs, the temporary pivot buffer preserves the alignment common to the array base address and element stride.

When calling the C array algorithms, keep these ownership and safety contracts in mind:
1. **Bitwise relocatable records**: Pass the full element stride, normally `sizeof(array[0])`. Every element must support raw byte relocation (`memcpy` / `memmove`). The C array APIs do not invoke C++ constructors, destructors, or container `copy`/`drop` callbacks. Use the C++ iterator algorithms in Section 5 for objects with non-trivial move or destruction semantics.
2. **Compacted tails in `unique`**: `hx_os_array_unique` overwrites plain records forward and returns the live prefix length `new_count`. Bytes in the `[new_count, count)` tail are unspecified and may still hold duplicate bit patterns—never treat the tail as additional live owned resources. If discarded elements own heap memory or handles that must be released, use the container API `hx_os_sequence_unique` with a `drop` callback instead.
3. **Comparator and concurrency rules**: Comparators must define a strict weak ordering (if floating-point data can contain `NaN`, handle it explicitly in the comparator). Comparators and predicates must not mutate the input array, throw C++ exceptions, or `longjmp`. All calls run synchronously on the calling thread without internal locks; synchronize shared mutations externally.

## 5. C++ calls follow standard iterator rules

Include `<hx/Algorithms.hpp>` to use the generic algorithms in `hx::algo`. Ranges use the standard half-open `[first, last)` convention and return standard types (`find` returns an iterator, `binary_search` returns `bool`, `sort` returns `void`):

```cpp
#include <hx/Algorithms.hpp>
#include <iterator>

int values[] = {8, 3, 1, 2};

hx::algo::sort(std::begin(values), std::end(values));
auto found = hx::algo::find(std::begin(values), std::end(values), 3);
```

| Operation | Iterator requirement and behavior |
| --- | --- |
| `sort`, `partial_sort`, `nth_element` | Random-access iterators (`RandomAccessIterator`); cannot be called on `std::list` iterators |
| `lower_bound`, `upper_bound`, `binary_search`, `equal_range` | Forward iterators (`ForwardIterator`) over a range ordered by the same comparator; non-random-access iterators still require `O(n)` increments |
| `reverse` | Bidirectional iterators (`BidirectionalIterator`) |
| `rotate`, `partition`, `unique`, `remove_if` | Forward iterators whose value types support move/swap assignment |
| `find`, `find_if`, `count`, `count_if` | Input iterators (`InputIterator`) |

`<hx/Algorithms.hpp>` also provides `min_element`, `max_element`, `minmax_element`, `is_sorted`, `copy_if`, `transform`, and `equal`:
- Like `std::unique` and `std::remove_if`, `hx::algo::unique` and `hx::algo::remove_if` return the new logical end iterator; call `container.erase(new_end, container.end())` to shrink a resizable container, or use `hx::algo::erase_if` (Section 8).
- Ensure the destination range has sufficient room when calling `copy_if` or `transform`, and ensure the second range is at least as long as the first when calling the three-iterator `equal(first1, last1, first2)` overload.
- To sort a `std::list` or `std::forward_list`, pass the container directly to `hx::algo::sort(items, compare)`. That overload calls the container's stable node-relinking sort without copying values or allocating nodes.
- Because `hx::algo` follows the library's no-exception policy and avoids hidden heap allocations that cannot report `hx_os_status`, it does not expose `stable_sort` or `inplace_merge` iterator wrappers. Use `hx_os_array_stable_sort` when you need a budgeted stable sort on a contiguous array. See the [C++ device example](../examples/algorithms_cpp.cpp) (target `hx_os_algorithms_cpp`) for a complete demonstration.

## 6. Filter, transform, compare, and accumulate C records

These functions in `<hx_algorithm.h>` also operate directly on plain C arrays without allocating library containers (the `hx_os_array_` prefix is omitted in the table):

| Function (`hx_os_array_*`) | Typical use case | Behavior and time cost |
| --- | --- | --- |
| `copy_if` | Copy offline devices into a work buffer | Writes matching elements in order into caller storage, `O(n)` |
| `transform` | Convert raw device structs into upload structs | Supports different input and output element strides, `O(n)` |
| `remove_if` | Compact out completed entries in place | Preserves order of kept entries and returns the remaining count, `O(n)` |
| `replace_if` | Overwrite matching plain records with a replacement value | Copies the replacement value over each match and returns the count, `O(n)` |
| `any_of` / `all_of` / `none_of` | Check whether any device is offline or all jobs finished | Writes `0` or `1` and short-circuits as soon as the answer is known, `O(n)` worst case |
| `fold` | Sum byte counts or update a summary struct | Invokes a callback sequentially to update the caller's accumulator, `O(n)` |
| `equal` / `mismatch` | Compare two record arrays or find their first difference | Takes explicit lengths for both arrays, `O(min(n, m))` |
| `equal_range` | Find all occurrences of a key in a sorted array | Returns the half-open index range `[first, last)`, `O(log n)` |
| `merge` (`HX_OS_MERGE` / `HX_OS_SET_*`) | Merge sorted arrays or compute union/intersection/difference | Linear merge `O(n + m)` preserving multiset duplicate semantics |
| `scan` | Compute packet starting offsets (prefix sums) or running totals | Writes inclusive or exclusive accumulator snapshots into caller storage, `O(n)` |

None of these functions perform heap allocations inside the library. Record copies and `scan` snapshots are shallow byte copies, and the replacement value passed to `replace_if` must reside outside the input array. `equal` and `mismatch` determine equality through the comparator rather than comparing struct padding bytes; `equal` returns false when array lengths differ, and `mismatch` returns the common prefix length when either array ends. On an empty array, `any_of`, `all_of`, and `none_of` produce `0`, `1`, and `1`, respectively.

Fixed-width integer binary search is equally direct:

```c
int found = 0;

status = hx_os_binary_search_i32(values, count, 3, &found, NULL);
if (status == HX_OS_OK && found)
	puts("Found 3");
```

The input array must already be sorted in ascending numerical order. The `_u32`, `_i64`, and `_u64` variants take the same arguments.

## 7. Capacity, progress, and callback failures

Functions that write variable or multi-element output (`copy_if`, `transform`, `merge`, and `scan`) take an output capacity measured in **records** alongside an `hx_os_array_progress` output struct:
- On entry, `left_consumed`, `right_consumed`, and `written` are reset to `0` (even if argument validation fails; single-input algorithms leave `right_consumed` at `0`). `fold` similarly resets its `completed` output to `0` on entry.
- **Resuming after `HX_OS_OUTPUT_FULL`**: When an operation returns `HX_OS_OUTPUT_FULL` with `progress.written == capacity`, the destination buffer is full and all `written` prefix elements are valid. Advance each input pointer by its reported `consumed` count and supply a fresh output buffer to continue. Because `copy_if` may inspect several non-matching elements before writing a match, an element that requires a new output slot is not marked consumed when the buffer fills; its predicate will be evaluated again when you resume.
- **Distinguishing callback `HX_OS_OUTPUT_FULL`**: If `transform` or `scan` returns `HX_OS_OUTPUT_FULL` while `progress.written < capacity`, that status code was returned by your callback rather than by destination buffer exhaustion—handle it using the callback-failure rules below.

> [!WARNING]
> **Non-overlapping buffers and callback failure recovery:**
> - **No overlapping buffers**: Output buffers, accumulators, and progress structs must never overlap the input arrays. The single exception is exact in-place `transform`, where the input and output share the exact same base address and element stride, and the callback is written to tolerate identical source and destination pointers.
> - **Partial effects on callback failure**: If a `transform` callback fails on an element, that element is not counted in `progress`, but its destination slot (or input slot during in-place transform) may have been partially modified; the callback must clean up any partial resources it created for that element. In `fold` and `scan`, accumulator changes made prior to (or during) the failing callback remain in place, while `progress` counts only completed callbacks. Resolve these partial effects before retrying.

`hx_os_array_scan` supports two snapshot modes:
- `HX_OS_SCAN_EXCLUSIVE`: Copies the accumulator into the output slot **before** invoking the fold callback for each element.
- `HX_OS_SCAN_INCLUSIVE`: Copies the accumulator into the output slot **after** invoking the fold callback for each element.

For input lengths `[2, 3, 5]` and an initial accumulator of `0`, `HX_OS_SCAN_EXCLUSIVE` writes packet offsets `[0, 2, 5]`, `HX_OS_SCAN_INCLUSIVE` writes running totals `[2, 5, 10]`, and the final accumulator is `10` in both cases. To guard against integer overflow during accumulation, check bounds inside your fold callback (for example, return `HX_OS_RANGE` if `value > UINT64_MAX - sum`).

## 8. Additional C++11 conveniences

`hx::algo` also includes `any_of`, `all_of`, `none_of`, `mismatch`, `merge`, the four sorted set operations (`set_union`, `set_intersection`, `set_difference`, `set_symmetric_difference`), `replace_if`, and the C++11 numeric algorithms (`accumulate`, `inner_product`, `partial_sum`, `adjacent_difference`, `iota`) with standard signatures and preconditions.

When you want to physically erase matching elements from a container instead of only computing a new logical end iterator, use the `hx::algo::erase_if` overloads provided for `std::vector`, `std::list`, and `std::forward_list`:

```cpp
std::vector<int> finished = {0, 4, 0, 8};

auto removed = hx::algo::erase_if(finished, [](int pending) noexcept {
	return pending == 0;
});

auto total = hx::algo::accumulate(finished.begin(), finished.end(), 0);
```

Here `removed` is `2`, `finished` retains `[4, 8]`, and `total` is `12`. These overloads invoke normal element destruction on the container and return the number of erased elements without allocating a temporary array. Predicates, element moves/assignments, and destructors must all satisfy `noexcept`.

## 9. Randomization and specialized searches

For Fisher–Yates array shuffling, reservoir/order-preserving sampling, stable LSD radix sorting for fixed-width integers, binary KMP search, Aho–Corasick multi-pattern matching, and Bloom filters, see [Binary buffers and search algorithms](FAST_TOOLS.md).
