# General Array and Iterator Algorithms

[简体中文](ALGORITHMS.zh-CN.md) | [Containers](CONTAINERS.md)

**Start with a complete program.** Use the algorithms on an existing C array or structure array to sort, search or filter without moving it into another container.

Open [algorithms.c](../examples/algorithms.c). Sort by device ID → lower_bound locates 23 → count_if counts pending records → copy_if copies them → fold totals work.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_algorithms`. This program needs no arguments or input files.

**Expected result:** IDs appear as 17, 23, 31; device 23 is found, with two pending devices and two total work items. Adapt comparators and predicates with the record fields.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Use the data you already have

The C99 functions in `<hx_algorithm.h>` work directly on ordinary arrays, including arrays of your structures. Creating an `hx_os_vector` is unnecessary. The C++11 header `<hx/Algorithms.hpp>` provides standard algorithm names in `hx::algo` and works with compatible standard iterators.

| Input | Entry style |
| --- | --- |
| Common integer array | `hx_os_sort_i32(values, count, error)`; also u32/i64/u64 |
| C array of integers or structures | `hx_os_array_sort(data, count, sizeof(data[0]), compare, user, error)` |
| C++ array, `std::array`, vector or another random-access range | `hx::algo::sort(first, last, compare)` |
| C++ list or forward_list | `hx::algo::sort(list, compare)` uses the container's stable node sort |
| Existing C library vector | Existing `hx_os_vector_sort` remains available and shares the array sorting implementation |

Enable `HX_OS_ENABLE_CONTAINERS` for the C functions and link `hx_c_platform::hx_c_platform`. When disabled, they return `HX_OS_NOT_SUPPORTED`. The C++ standard-algorithm header is independent of that switch; `hx_c_platform::cpp` supplies the installed include path when using the package.

## 2. Sort a C array

This complete program prints `1 2 3 8`. Common integer arrays need no comparator callback: `i32`, `u32`, `i64`, and `u64` each have sorting and binary-search shortcuts.

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

For records, the comparator reads the desired field and returns negative/zero/positive; use `(a > b) - (a < b)` for integers rather than subtracting potentially overflowing values. The algorithm moves the whole record, keeping its fields together. The [device example](../examples/algorithms.c) sorts `{id, pending}` records by ID, finds device 23 with a lower-bound query, filters pending devices into a work array, and sums their pending counts. Build target `hx_os_algorithms` using the existing example configuration. It prints:

```text
Device 17, pending=0
Device 23, pending=1
Device 31, pending=1
Found device 23
Devices needing work: 2
Copied work items: 2
Pending work total: 2
```

## 3. Choose an operation

Names below have the common `hx_os_array_` prefix. `n` is the input count, `k` is a selected prefix length, and `s` is the complete element size. Complexity counts element operations; comparison and byte-movement costs are additional.

| Entry | Result | Current implementation and cost |
| --- | --- | --- |
| `sort` | Fully ordered array, unstable | In-place heapsort; O(n log n), no allocation |
| `stable_sort` | Equal keys retain their input order | Bottom-up merge sort; O(n log n), O(n × s) scratch |
| `lower_bound` / `upper_bound` | First not-less / first greater insertion index | Sorted input required; O(log n) comparisons, no allocation |
| `binary_search` | `found` is 0 or 1 | Sorted input required; O(log n), no allocation |
| `find_if` / `count_if` | First matching index / match count | O(n); find stops at the first match |
| `minmax` | First minimum and first maximum indices | One traversal, O(n), no allocation |
| `is_sorted` | Whether the range is nondecreasing | O(n) worst; stops at the first inversion |
| `reverse` / `rotate` | Reverse records / rotate left by a count | O(n), no allocation; rotation count is reduced modulo n |
| `partition` | Matches form a prefix; returns its length | O(n), no allocation, not stable |
| `unique` | Compact adjacent equivalents; return new length | O(n), no allocation; separated duplicates remain |
| `partial_sort` | The smallest k records sorted at the front | O(n log(k + 1)) for k > 0, no allocation |
| `nth_element` | Record of one sorted rank in its final position | Three-way partition with heapsort fallback; worst O(n log n), one aligned record of scratch |

Bounds can return `count`; that is a valid insertion position, not a matching record. Check the returned position and key before dereferencing, or use `binary_search` when only membership is needed. Missing `find_if` and empty `minmax` return `HX_OS_NOT_FOUND` without changing outputs. `minmax` requires distinct output variables. Empty `is_sorted` is true. Invalid selection indices return `HX_OS_RANGE`; a zero prefix for `partial_sort` does no work. Neither selection operation promises a sorted suffix or stable ordering.

## 4. Scratch memory and record ownership

`stable_sort` and `nth_element` accept `context` and `max_scratch_bytes`. Pass `NULL` for the default allocator and `0` for the default 16 MiB scratch budget. The budget includes library allocation headers and alignment overhead; the allocator's own bookkeeping is additional. Scratch is released before return. `HX_OS_LIMIT` and `HX_OS_NO_MEMORY` leave the array unchanged. A singleton rank selection needs no allocation. The pivot preserves the alignment common to the array address and element stride.

Use the complete element stride, normally `sizeof(array[0])`. The caller supplies a valid contiguous range; the library checks multiplication and pointer-range overflow but cannot verify the allocation behind a pointer. All records must support byte relocation. No constructors, destructors, copy/drop callbacks or resource transfers are inferred. C++ objects with nontrivial move/destruction belong in the C++ iterator interfaces.

`unique` compacts plain records and returns the live prefix length. The tail is unspecified; it can contain duplicate byte representations and must not be treated as additional owned values. For owning values that must release discarded resources, use the existing `hx_os_sequence_unique` with its drop policy. Sorting itself preserves every record.

Comparators must define a strict weak order, predicates must not mutate the input, and callbacks must not throw or longjmp. Floating-point NaN values need an explicitly suitable comparison policy. Access is synchronous, with no worker or internal lock. Serialize mutations externally, keep callback user data alive, and keep result outputs outside the input range.

## 5. C++ calls follow standard iterator rules

```cpp
#include <hx/Algorithms.hpp>
#include <iterator>

int values[] = {8, 3, 1, 2};

hx::algo::sort(std::begin(values), std::end(values));
auto found = hx::algo::find(std::begin(values), std::end(values), 3);
```

The range is `[first, last)`. Return values are the standard results: `find` returns an iterator, `binary_search` a bool, and `sort` returns void. The wrapper does not catch exceptions or translate them into `Result`. Element comparisons, moves, swaps, assignments, and output-iterator operations must satisfy this project's no-exception contract.

| Operation | Iterator requirement |
| --- | --- |
| `sort`, `partial_sort`, `nth_element` | Random access; list iterators cannot be used |
| `lower_bound`, `upper_bound`, `binary_search`, `equal_range` | At least forward iterators and correctly ordered input; non-random-access traversal can still take O(n) iterator increments |
| `reverse` | Bidirectional |
| `rotate`, `partition`, `unique`, `remove_if` | At least forward iterators; values must support the required modification |
| `find`, `find_if`, `count`, `count_if` | Input iterators |

The header also exposes `min_element`, `max_element`, `minmax_element`, `is_sorted`, `copy_if`, `transform`, and `equal`, subject to their C++11 requirements. `unique` and `remove_if` return a logical new end; erase the tail when using a resizable container. Ensure output capacity for `copy_if`/`transform`. The C++11 three-iterator `equal` overload requires a sufficiently long second range.

For `std::list` and `std::forward_list`, use `hx::algo::sort(items, compare)`. This specific overload delegates to the stable member sort, relinking existing nodes without copying values or allocating nodes. It does not make arbitrary list iterator pairs random-access sortable.

There is currently no `hx::algo::stable_sort` or `inplace_merge` alias, avoiding an implicit temporary allocation path with no allocation-error result. C arrays have the explicitly budgeted stable-sort function above. The [C++ device example](../examples/algorithms_cpp.cpp), target `hx_os_algorithms_cpp`, shows array sorting/search and list sorting. These facilities provide direct calls and shared implementations; no general performance advantage over the standard library is claimed.

## 6. Filter, transform, compare, and accumulate C records

These operations also work on ordinary arrays; they do not create a library container. Names below omit `hx_os_array_`.

| Entry | Typical job | Result and cost |
| --- | --- | --- |
| `copy_if` | Copy offline devices to a work list | Stable selected prefix in caller storage, O(n) |
| `transform` | Convert device records into upload records | Different input/output strides supported, O(n) |
| `remove_if` | Remove completed entries in place | Stable valid prefix; returns remaining count, O(n) |
| `replace_if` | Reset all matching plain records to one value | Returns replacement count, O(n) |
| `any_of` / `all_of` / `none_of` | Is any device offline? Have all jobs completed? | Bool output with short circuit, O(n) worst |
| `fold` | Sum bytes or update a statistics structure | Sequential callbacks update caller accumulator, O(n) |
| `equal` / `mismatch` | Compare device lists or locate their first difference | Both input lengths are explicit, O(min(n,m)) |
| `equal_range` | Find all occurrences of a key in sorted input | Half-open range `[first,last)`, O(log n) |
| `merge` with MERGE/SET_* modes | Merge sorted data; determine added/deleted IDs | O(n+m), preserves the existing multiset rules |
| `scan` | Calculate every packet's starting offset or running total | Inclusive/exclusive accumulator snapshots, O(n) |

All these functions avoid library allocations. Record copies and snapshots are shallow byte copies, with no implicit clone/drop. For discarded owning values, use the container APIs and their ownership callbacks. `replace_if` requires its replacement record outside the input array. `equal`/`mismatch` use the comparator's logical equality, rather than comparing structure padding bytes. `mismatch` returns the common-prefix length when one or both inputs end; different lengths make `equal` false. Empty any/all/none results are false/true/true.

Integer lookup is similarly direct:

```c
int found = 0;

status = hx_os_binary_search_i32(values, count, 3, &found, NULL);
if (status == HX_OS_OK && found)
	puts("Found 3");
```

The array must already be sorted numerically. The unsigned and 64-bit variants have the same argument order and handle the complete ranges of their integer types.

## 7. Capacity, progress, and callback failures

`copy_if`, `transform`, `merge`, and `scan` require a destination capacity in **records** and an `hx_os_array_progress`. Its `left_consumed`, `right_consumed`, and `written` fields reset to zero on entry, even when validation fails. Single-input operations leave `right_consumed` zero. `fold` likewise resets its `completed` output; other ordinary queries change outputs only on success.

`HX_OS_OUTPUT_FULL` with `progress.written == capacity` means the destination filled; the completed prefix is retained. Resume by advancing each input by its consumed count and providing space for the remaining output. Filtering can consume more records than it writes. No record requiring another output slot is consumed when the buffer fills; callbacks for a retried filtered record may be evaluated again. If transform/scan returns OUTPUT_FULL with `written < capacity`, the status came from its callback; use the callback-failure rules below instead of assuming buffer exhaustion. Neither capacity exhaustion nor validation failure triggers hidden allocation.

Output must not overlap input. The one exception is `transform` with the exact same base address and element size; its callback must tolerate identical input and destination addresses. Partial overlap is rejected. Scan's accumulator must be separate from both the input and output. Progress/count objects must be outside all data buffers.

A failing transform record is excluded from progress, but its destination may have been modified; the callback must clean up its partial construction. With exact in-place transformation this can also modify the failing input record. Successful earlier outputs remain usable. Fold/scan preserve the accumulator changes already made, including partial changes by the failing callback. Their progress counts successful callbacks only. Native error details supplied by callbacks are retained. Retrying after a callback error requires the caller to resolve these partial effects; it is not the automatic resume case provided for OUTPUT_FULL.

`scan` copies the accumulator before each fold in `HX_OS_SCAN_EXCLUSIVE` mode and after each fold in `HX_OS_SCAN_INCLUSIVE` mode. For lengths `[2,3,5]` and initial sum zero, the outputs are `[0,2,5]` or `[2,5,10]`, and the final accumulator is 10. The fold callback defines overflow/error behavior; it should check `value > UINT64_MAX - sum` before adding unsigned byte counts. Snapshots of pointer fields borrow their pointees and are not independent deep copies.

## 8. Additional C++11 conveniences

`hx::algo` also exposes `any_of`, `all_of`, `none_of`, `mismatch`, `merge`, the four `set_*` operations, `replace_if`, and the C++11 numeric algorithms `accumulate`, `inner_product`, `partial_sum`, `adjacent_difference`, and `iota`. These preserve standard signatures and preconditions. In particular, C++11 three-iterator `mismatch` needs a sufficiently long second range, sorted set operations require sorted inputs, and numeric algorithms use the selected type's arithmetic rather than providing checked overflow.

For removing elements rather than just computing a logical new end, use the explicit `erase_if` overloads for `std::vector`, `std::list`, and `std::forward_list`:

```cpp
std::vector<int> finished = {0, 4, 0, 8};

auto removed = hx::algo::erase_if(finished, [](int pending) noexcept {
	return pending == 0;
});

auto total = hx::algo::accumulate(finished.begin(), finished.end(), 0);
```

Here `removed` is 2, `finished` contains `[4,8]`, and `total` is 12. These overloads return the erased count, perform the container's normal destruction, and allocate no result array. Iterator invalidation follows the specific container. Predicates, element movement/assignment, destruction and iterator operations must not throw; no exception-to-status conversion is installed. Arbitrary container types are not accepted by guessing their erase behavior.

## 9. Randomization and specialized searches

C shuffle/sample and stable integer radix sorting, C++11 iterator sampling, binary KMP, multi-pattern matching and Bloom filters are documented in [binary/search tools](FAST_TOOLS.md). They supplement the general comparison algorithms; callers retain the same arrays or iterator ranges.
