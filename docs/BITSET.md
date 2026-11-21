# Dynamic Bitset

[简体中文](BITSET.zh-CN.md) | [Containers](CONTAINERS.md)

When your program tracks items numbered consecutively from `0`—such as job IDs, pool slots, or device indices—and only needs a boolean flag per item (e.g., completed vs. pending, occupied vs. free), `hx_os_bitset` (or `hx::Bitset` in `<hx/Bitset.hpp>` for C++11) packs each flag into a single bit and processes ranges, population counts, next-set/clear-bit searches, and set operations in 32-bit word batches rather than allocating individual structs or tree nodes.

To see a complete program, open [bitset.c](../examples/bitset.c): it calls `hx_os_bitset_create` to allocate `100` flags with a maximum growth ceiling of `1000` bits, marks jobs `0..9` complete in one call with `hx_os_bitset_set_range`, marks job `42` complete with `hx_os_bitset_set`, locates the first unfinished job index using `hx_os_bitset_find_next`, and destroys the bitset.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_bitset` (no arguments or input files are needed):

```text
Completed: 11 of 100
Next unfinished job: 10
```

You can also copy the example into the [minimal project](USAGE.md#c-project) as `main.c`.

## 1. Track many numbered items

`hx_os_bitset` stores one boolean flag per zero-based index, making it well suited for job completion bitmaps, slot allocators, device masks, and bounded non-negative integer sets. The C++11 RAII wrapper `hx::Bitset` in `<hx/Bitset.hpp>` returns `hx::Result<T>` on fallible operations and never throws exceptions.

Keep `HX_OS_ENABLE_CONTAINERS=ON` (the default) and link `hx_c_platform::hx_c_platform` (or `hx_c_platform::cpp` for C++). No third-party dependencies are required. When `HX_OS_ENABLE_CONTAINERS` is `OFF`, creation and operations return `HX_OS_NOT_SUPPORTED`.

Here is the C++11 equivalent of the [complete C example](../examples/bitset.c):

```cpp
#include <hx/Bitset.hpp>
#include <cstdio>

int main()
{
	auto result = hx::Bitset::create(100, 1000);
	if (!result)
		return 1;

	auto &completed = result.value();
	if (!completed.setRange(0, 10))
		return 1;
	if (!completed.set(42))
		return 1;

	auto next = completed.findNext(0, false);
	if (!next)
		return 1;

	std::printf("Next unfinished job: %zu\n", next.value());

	return 0;
}
```

## 2. Common operations

The table below lists the C entry points in `<hx_bitset.h>` (included automatically by `<hx_container.h>` and `<hx_os.h>`), omitting the common `hx_os_bitset_` prefix. C++ `hx::Bitset` methods use matching `camelCase` names such as `findNext()`, `setRange()`, and `isSubsetOf()`:

| C function (`hx_os_bitset_*`) | Purpose |
| --- | --- |
| `create(context, bit_count, max_bits, out, error)` | Allocate `bit_count` zero-initialized bits; `max_bits = 0` selects the default `1,048,576`-bit ceiling |
| `resize(bits, bit_count, error)` | Grow the logical bit count (zero-filling new bits) or shrink it (discarding removed high bits) |
| `set(bits, index, value, error)` / `reset(bits, index, error)` | Set or clear the flag at `index` (`reset` always clears to `0`) |
| `test(bits, index, out, error)` | Read the flag at `index` into `*out` (`0` or `1`), keeping out-of-range errors distinct from a `0` bit |
| `set_range(bits, first, count, value, error)` | Set or clear the half-open range `[first, first + count)` in 32-bit word batches |
| `fill(bits, value, error)` / `clear(bits, error)` | Set every logical bit to `value`; `clear` resets all bits to `0` without changing `size` |
| `count(bits)` / `size(bits)` | `count` returns the number of set (`1`) bits; `size` returns the number of addressable bits |
| `find_next(bits, start, value, out, error)` | Find the first bit equal to `value` (`0` or `1`) at or after `start` |
| `apply(bits, other, operation, error)` | Perform in-place bitwise `AND`, `OR`, `XOR`, or `AND_NOT` with another equal-sized bitset |
| `intersects(left, right, out, error)` | Check whether two equal-sized bitsets share at least one set bit |
| `is_subset(left, right, out, error)` | Check whether every set bit in `left` is also set in `right` |

## 3. Size, errors, and ownership

- **Capacity ceiling and memory management**: `max_bits` bounds the logical bit count (storage uses 32-bit words and grows geometrically within that ceiling; object headers and allocator metadata are separate, and growth temporarily holds both the old and new word buffers). Shrinking with `resize` retains allocated capacity; when grown again later, all newly exposed bits—including positions previously removed by shrinking—are zero-initialized. Unused padding bits past the logical `size` are never counted or matched by searches. `hx_os_bitset_destroy` frees all storage and sets the caller's handle pointer to `NULL` (`hx::Bitset` does this automatically in its destructor). Any custom `hx_os_context` must outlive the bitset.
- **Error codes and failure guarantees**:
  - Out-of-bounds indices or ranges return `HX_OS_RANGE`.
  - Creation or `resize` requests above `max_bits` return `HX_OS_LIMIT`.
  - Heap allocation failures return `HX_OS_NO_MEMORY`.
  - Failed mutations leave both the logical size and stored bits unchanged. Output variables are modified only on `HX_OS_OK` (except `create`, which initializes its output handle pointer to `NULL` on entry).
- **Search and set-operation boundaries**:
  - `hx_os_bitset_find_next` returns `HX_OS_NOT_FOUND` when no matching bit exists at or after `start`. Passing `start == size` is a valid empty search that returns `HX_OS_NOT_FOUND`, whereas `start > size` returns `HX_OS_RANGE`.
  - `apply`, `intersects`, and `is_subset` require both bitsets to have the exact same logical `size`, returning `HX_OS_INVALID` if their sizes differ. Self-operations are valid (for example, `apply` with `HX_OS_BITSET_XOR` on the same bitset clears every bit).

> [!NOTE]
> The internal word array of `hx_os_bitset` is an in-memory representation and should not be treated as a wire or disk serialization format.

## 4. Concurrency and algorithms

- **Thread safety**: `hx_os_bitset` is a standard dynamic bitmap, **not a lock-free atomic shared bitmap**. Concurrent reads are safe while no thread mutates or destroys the object; synchronize any shared mutation externally.
- **Word-level operations vs. sparse sets**: Single-bit `set`, `reset`, and `test` calls run in `O(1)` time. `count`, `find_next`, `set_range`, and set relations operate on 32-bit words (`O(ceil(bit_count / 32))` worst case), using compiler intrinsics (`__builtin_popcount`, `__builtin_ctz`) on GCC/Clang and portable word arithmetic elsewhere. If your identifiers span a huge, sparse range (such as arbitrary 64-bit IDs), use a hash or tree set from [Containers](CONTAINERS.md) rather than a dense bitmap.
