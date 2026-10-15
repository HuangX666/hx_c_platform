# Dynamic Bitset

[简体中文](BITSET.zh-CN.md) | [Containers](CONTAINERS.md)

**Start with a complete program.** Use a bitset when jobs have consecutive IDs and each only needs a completed/not-completed flag.

Open [bitset.c](../examples/bitset.c). `create(100, 1000)` allocates 100 flags with a growth limit → `set_range` marks 0–9 → `set` marks 42 → `find_next` finds an unfinished ID → `destroy`.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_bitset`. This program needs no arguments or input files.

**Expected result:** It prints `Completed: 11 of 100` and `Next unfinished job: 10`. Replace 100 with the job count and mark each completed index. Capacity is measured in bits.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Track many numbered items

`hx_os_bitset` stores one boolean flag per numbered item. Use it for completed jobs, occupied slots, selected devices, or membership in a bounded integer set. Bits are numbered from zero. The C++11 wrapper is `hx::Bitset` in `<hx/Bitset.hpp>`; its operations return `Result` and never throw.

Enable `HX_OS_ENABLE_CONTAINERS` and link `hx_c_platform::hx_c_platform` (or `hx_c_platform::cpp` for C++). No additional dependency is needed. With containers disabled, creation and operations return `HX_OS_NOT_SUPPORTED`.

The [complete C example](../examples/bitset.c) tracks 100 jobs, marks jobs 0–9 and 42 complete, then finds the first unfinished job. Its output is:

```text
Completed: 11 of 100
Next unfinished job: 10
```

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

| C entry | Purpose |
| --- | --- |
| `create(context, bit_count, max_bits, out, error)` | Create zero-valued bits; `max_bits=0` selects 1,048,576 bits |
| `resize(bits, bit_count, error)` | Grow with zero values or discard removed bits |
| `set(bits, index, value, error)` / `reset(bits, index, error)` | Set or clear one flag |
| `test(bits, index, out, error)` | Read one flag without confusing invalid indices with false |
| `set_range(bits, first, count, value, error)` | Change a half-open range in word-sized batches |
| `fill(bits, value, error)` / `clear(bits, error)` | Set every flag; `clear` preserves the size |
| `count(bits)` | Count set bits; `size(bits)` returns the number of addressable bits |
| `find_next(bits, start, value, out, error)` | Find a set or clear bit, including `start` |
| `apply(bits, other, operation, error)` | In-place AND, OR, XOR, or AND-NOT |
| `intersects(left, right, out, error)` | Check whether two sets share any set bit |
| `is_subset(left, right, out, error)` | Check whether every set bit in left is also in right |

The table abbreviates the common `hx_os_bitset_` prefix. C++ methods use camelCase, such as `findNext()` and `isSubsetOf()`.

## 3. Size, errors, and ownership

The maximum is a logical bit limit, not a promise about total allocator bytes. Storage uses 32-bit words and grows geometrically within that limit. Metadata and allocation overhead are additional; growth may temporarily hold old and new buffers. Shrinking retains capacity. `destroy` releases all storage and clears the caller's pointer; the C++ object does this automatically. A custom context must remain alive until destruction.

All bits added by growth are zero, including positions previously removed by shrinking. Padding beyond the logical size is never counted or searched. The internal representation is not a serialization format.

Invalid indices and ranges return `HX_OS_RANGE`; exceeding the configured limit returns `HX_OS_LIMIT`. Allocation failure returns `HX_OS_NO_MEMORY`. Failed mutations preserve size and contents. Output parameters change only on success, except creation initializes its handle output to `NULL`.

`find_next` returns `HX_OS_NOT_FOUND` when there is no match. `start == size` is an empty search; `start > size` is a range error. Set operations and relations require equal sizes and return `HX_OS_INVALID` for unequal sizes. Self operations are valid: XOR with itself clears the set, for example.

## 4. Concurrency and algorithms

This is an ordinary dynamic bitmap, **not an atomic shared bitmap**. Concurrent reads are allowed only while nobody mutates or destroys it; synchronize shared mutation externally. Allocator callbacks must not reenter the same object.

Single-bit access is constant-time. Counting, searching, ranges, and set operations process 32-bit words rather than inspecting every bit separately. GCC/Clang builtins implement population count and the first-set-bit search; other compilers use portable integer implementations. No runtime-specific instruction set is assumed. A sparse set of very large identifiers may fit the existing hash/tree containers better than a dense bitmap.
