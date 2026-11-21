# Binary buffers and search algorithms

[简体中文](FAST_TOOLS.zh-CN.md) | [General algorithms](ALGORITHMS.md)

When your program needs an owning, growable binary byte buffer (`hx_bytes`), Fisher–Yates array shuffling or order-preserving random sampling, linear-time stable radix sorting for 32/64-bit integer keys, or fast pattern matching and membership filtering (KMP single-pattern search, Aho–Corasick multi-pattern scanning, and Bloom filters), use the utilities documented on this page.

A complete runnable program combining all of these features is available in [fast_algorithms.c](../examples/fast_algorithms.c): it seeds a deterministic `hx_os_rng`, shuffles and samples an array, sorts signed integers with `hx_os_radix_sort_i32`, locates a byte substring with `hx_os_bytes_find`, scans a message for multiple alert keywords at once using a compiled `hx_os_matcher`, and queries a `hx_os_bloom` filter before releasing all resources.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_fast_algorithms` (no arguments or input files are required). The output shows the radix-sorted endpoints `-3 .. 9`, `"error"` found at byte offset `16`, callbacks fired for both `"warning"` and `"error"`, and the Bloom filter lookup results. You can also drop the example source into the [minimal project](USAGE.md#c-project) as `main.c`.

## 1. Owned binary bytes

`hx_bytes` is the binary counterpart to `hx_string`. Initialize it with `HX_BYTES_INIT`, then call `hx_bytes_assign`, `hx_bytes_append`, `hx_bytes_reserve`, or `hx_bytes_resize`:
- Accepts arbitrary binary bytes including embedded `NUL` (`0x00`), performs no UTF-8 validation, and appends no trailing terminator.
- Growing a buffer with `hx_bytes_resize` zero-fills the newly added tail.
- `assign` and `append` safely accept input views borrowed from the target `hx_bytes` itself, even when reallocation occurs; if allocation fails or exceeds the `64 MiB` maximum capacity, the existing contents remain unchanged.

```c
hx_bytes packet = HX_BYTES_INIT;
const unsigned char header[] = {0, 1, 2, 3};

hx_os_status status = hx_bytes_assign(&packet,
	(hx_os_bytes_view){header, sizeof(header)}, NULL);
if (status == HX_OS_OK)
	status = hx_bytes_append(&packet, (hx_os_bytes_view){"body", 4}, NULL);

hx_bytes_destroy(&packet);
```

In C++11, `hx::Bytes` is a move-only RAII wrapper whose `view()` method borrows the underlying byte span until the next mutation or destruction. `hx_bytes` is always built into the core library; for FIFO stream buffering and consumption, use [ByteQueue](BYTE_QUEUE.md) or [Ring](RING.md).

## 2. Shuffle, sample and integer radix sorting

Like the [general array algorithms](ALGORITHMS.md), these C functions operate directly on plain arrays and struct arrays without requiring a library container:

```c
int32_t values[] = {5, -9, 0, 12, 2};
int32_t selected[2];
size_t written = 0;
hx_os_rng rng;

hx_os_rng_seed(&rng, 1234);
hx_os_status status = hx_os_array_shuffle(values, 5,
	sizeof(values[0]), &rng, NULL);
if (status == HX_OS_OK)
	status = hx_os_array_sample(values, 5, sizeof(values[0]),
		selected, 2, &rng, &written, NULL);
if (status == HX_OS_OK)
	status = hx_os_radix_sort_i32(NULL, values, 5, 0, NULL);
```

- **Shuffle and sampling (`hx_os_array_shuffle` / `hx_os_array_sample`)**:
  - `hx_os_array_shuffle` runs an in-place Fisher–Yates shuffle. `hx_os_array_sample` selects up to the destination capacity without replacement while **preserving the relative source order** of selected records (input and output buffers must not overlap, and elements must be bitwise-relocatable).
  - Passing a caller-owned `hx_os_rng` avoids per-call OS entropy requests; passing `NULL` seeds a temporary generator once from OS randomness (requiring `HX_OS_ENABLE_RANDOM=ON`).
  - `hx_os_rng` uses `xoshiro256**` with `SplitMix64` seeding and rejection sampling for unbiased bounded integers. It is a fast deterministic PRNG, **not a cryptographic RNG**—use `hx_os_random_bytes` when generating cryptographic keys or nonces.
- **Stable integer radix sort (`hx_os_radix_sort_i32` / `u32` / `i64` / `u64`)**:
  - Performs a stable least-significant-digit (LSD) radix sort in 4 or 8 byte passes, ordering integers in ascending numerical order (handling negative signed integers properly and remaining independent of host endianness).
  - Requires one array-sized scratch buffer (default budget `16 MiB` including allocation headers). Exceeding the budget returns `HX_OS_LIMIT` and allocation failure returns `HX_OS_NO_MEMORY`, leaving the input array untouched.
- **C++11 iterator shuffle and sample**: In `<hx/Algorithms.hpp>`, `hx::algo::shuffle` accepts random-access iterators, and `hx::algo::sample` supports forward input iterators with any output iterator (or single-pass input iterators with a random-access output range for reservoir sampling):

```cpp
#include <hx/Algorithms.hpp>

int values[] = {5, -9, 0, 12, 2};
int selected[2];
std::mt19937 generator(1234);

hx::algo::shuffle(values, values + 5, generator);
auto end = hx::algo::sample(values, values + 5, selected, 2, generator);
```

Here `end - selected` gives the number of sampled elements written.

## 3. Find a binary pattern

`hx_os_bytes_find` searches a byte span for a single pattern using the Knuth–Morris–Pratt (KMP) algorithm, supporting embedded `NUL` bytes:

```c
size_t offset = 0;
const unsigned char bytes[] = {'a', 0, 'b', 'c'};

hx_os_status status = hx_os_bytes_find(NULL,
	(hx_os_bytes_view){bytes, sizeof(bytes)},
	(hx_os_bytes_view){"\0b", 2}, 0, 0, &offset, NULL);
```

- The call above searches from offset `0` for the 2-byte pattern `"\0b"` and sets `offset` to `1`.
- An empty pattern matches immediately at `start_offset`; if the pattern is not found, `hx_os_bytes_find` returns `HX_OS_NOT_FOUND` and leaves `offset` unchanged.
- Prefix-table preprocessing uses `O(pattern length)` scratch memory (bounded by the default `16 MiB` budget), and the scan runs in `O(input + pattern)` time with no per-byte allocations.

## 4. Match many patterns

When you need to scan messages or buffers for dozens or thousands of patterns simultaneously, compile the pattern list once into an immutable `hx_os_matcher` (a sparse Aho–Corasick automaton, wrapped in C++11 by `hx::Matcher`) and reuse it across searches:
- **Overlapping and duplicate patterns**: Each match invokes your callback synchronously with the pattern index (matching the compilation order), start offset, and byte length.
- **Concurrent read sharing**: A compiled `hx_os_matcher` is immutable and **can be shared across concurrent search threads** without locking, provided no thread destroys the matcher while a scan is in flight. Callbacks must not destroy the matcher, throw exceptions, or `longjmp`.
- **Bounds**: A matcher accepts up to `4096` patterns, `1 MiB` of combined pattern bytes, and a `16 MiB` default compilation budget, delivering up to `65,536` matches per scan. Hitting the match-delivery ceiling returns `HX_OS_LIMIT` alongside the count of matches delivered so far.

## 5. Bloom membership filter

`hx_os_bloom_create(context, expected_items, false_positive_rate, budget, &bloom, error)` (wrapped in C++11 as `hx::Bloom`) creates a bounded-memory probabilistic membership filter:
- **Query semantics (no false negatives)**: After inserting byte keys, test membership with `hx_os_bloom_contains`. A false (`0`) result guarantees the key was **never inserted**; a true (`1`) result means the key is *possibly present* and should be verified against the authoritative data store.
- **Sizing and reset**: `expected_items` sizes the bit array and hash-function count at creation time rather than imposing a hard insertion cap (inserting many more items than expected increases the false-positive rate). Individual keys cannot be deleted, and the filter does not resize automatically; `hx_os_bloom_clear` resets all bits to zero.
- **Budget and concurrency**: Target false-positive rates between `1e-9` and `0.5` are accepted, with a `16 MiB` default memory budget (`64 MiB` maximum). Double hashing uses non-cryptographic hashes and performs zero allocations during insertion or lookup. Serialize mutations externally; concurrent lookups on an unchanging filter are safe.

## 6. Run and configure

Build and run `hx_os_fast_algorithms` ([fast_algorithms.c](../examples/fast_algorithms.c)) with `-DHX_OS_BUILD_EXAMPLES=ON` to exercise shuffling, sampling, radix sorting, KMP/Aho–Corasick pattern matching, and Bloom filtering end to end.

> [!NOTE]
> **CMake feature switches**:
> - `hx_bytes` and the deterministic PRNG `hx_os_rng` are always available in the core library without global initialization.
> - Array shuffle/sample, radix sort, `hx_os_bytes_find`, `hx_os_matcher`, and `hx_os_bloom` require `HX_OS_ENABLE_CONTAINERS=ON` (returning `HX_OS_NOT_SUPPORTED` when disabled).
> - Passing `NULL` for the RNG in `shuffle` or `sample` to seed from OS entropy additionally requires `HX_OS_ENABLE_RANDOM=ON`.
