# Binary buffers and search algorithms

[简体中文](FAST_TOOLS.zh-CN.md) | [General algorithms](ALGORITHMS.md)

**Start with a complete program.** Start here for sampling records, sorting integer keys or finding several alert words in one message.

Open [fast_algorithms.c](../examples/fast_algorithms.c). Seed the RNG → shuffle/sample → radix_sort → bytes_find → matcher_scan callbacks → bloom insertion/query → release objects.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_fast_algorithms`. This program needs no arguments or input files.

**Expected result:** Sorted endpoints are `-3 .. 9`; error starts at byte 16, with callbacks for warning and error. Bloom `possibly present` requires a real lookup; the seeded RNG is not a key generator.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Owned binary bytes

`hx_bytes` is the binary counterpart to `hx_string`: initialize with `HX_BYTES_INIT`, then assign, append, reserve or resize. It accepts NUL and arbitrary encoding, and adds no terminator. Resize zero-fills an extension. Assign/append may borrow the current value; failure leaves it unchanged. Capacity is at most 64 MiB, with old+new storage during growth. Use move/copy operations instead of copying its owning structure. Mutations require external serialization.

```c
hx_bytes packet = HX_BYTES_INIT;
const unsigned char header[] = {0, 1, 2, 3};

hx_os_status status = hx_bytes_assign(&packet,
	(hx_os_bytes_view){header, sizeof(header)}, NULL);
if (status == HX_OS_OK)
	status = hx_bytes_append(&packet, (hx_os_bytes_view){"body", 4}, NULL);

hx_bytes_destroy(&packet);
```

C++ `hx::Bytes` is move-only. Its `view()` borrows storage until mutation or destruction. This buffer is always built in the core and is not a queue; use ByteQueue/Ring for buffered consumption.

## 2. Shuffle, sample and integer radix sorting

The C operations work directly on ordinary arrays and record arrays; copying into a library container is unnecessary.

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

Shuffle uses Fisher–Yates. Sampling chooses up to output capacity without replacement, preserving source order; input and output cannot overlap. Records must be byte-relocatable. A caller-owned RNG avoids system calls per element. With RNG `NULL`, the operation seeds a local generator once using system randomness. Explicit seed 1234 above makes the example reproducible.

`hx_os_rng` uses xoshiro256** with SplitMix64 seeding and rejection for unbiased bounded integers. It is a fast deterministic PRNG, not cryptographic randomness. Use `hx_os_random_bytes` for OS-provided cryptographically secure bytes. That API does not promise a physical hardware true-random source on every call.

`hx_os_radix_sort_i32/u32/i64/u64` uses stable LSD radix sorting: four/eight byte passes, ascending numeric order, including negative signed values. The implementation reads numeric values with `memcpy` and does not assume host byte order. One array-sized scratch allocation is required; default budget is 16 MiB including allocation overhead. Allocation/limit failures preserve the array. Small arrays may be faster with ordinary sort; complexity alone is not a benchmark.

C++ `hx::algo::shuffle` follows standard random-access iterator requirements. `sample` is available in C++11: forward input supports a general output iterator, while single-pass input needs random-access output for reservoir sampling. Capacity and nonthrowing element/iterator operations are caller contracts.

```cpp
#include <hx/Algorithms.hpp>

int values[] = {5, -9, 0, 12, 2};
int selected[2];
std::mt19937 generator(1234);

hx::algo::shuffle(values, values + 5, generator);
auto end = hx::algo::sample(values, values + 5, selected, 2, generator);
```

`end - selected` is the actual selected count. These C++ iterator operations are separate from the C byte-relocation interfaces.

## 3. Find a binary pattern

`hx_os_bytes_find` uses KMP and accepts counted arbitrary bytes, including NUL. Pass a start offset and optional scratch budget; result is the first match offset. Empty pattern matches at start. Missing pattern returns `NOT_FOUND` and preserves the previous offset. Input is borrowed for the synchronous call.

```c
size_t offset = 0;
const unsigned char bytes[] = {'a', 0, 'b', 'c'};

hx_os_status status = hx_os_bytes_find(NULL,
	(hx_os_bytes_view){bytes, sizeof(bytes)},
	(hx_os_bytes_view){"\0b", 2}, 0, 0, &offset, NULL);
```

This finds offset 1. Preprocessing uses O(pattern length) scratch, bounded by the default 16 MiB budget. Search is O(input + pattern) and performs no allocation per byte.

## 4. Match many patterns

Compile an immutable `hx_os_matcher` once from byte views, then search multiple messages. The sparse Aho–Corasick automaton supports duplicate and overlapping patterns. Callbacks report pattern index, start and length; views/indices refer to caller input and the compiled pattern order. Different searches may use the same matcher concurrently, but cannot race its destruction. Callbacks cannot destroy it or throw/longjmp.

Limits: 4096 patterns, 1 MiB combined pattern bytes, default 16 MiB compile budget and 65536 delivered matches per search. A result limit reports `LIMIT` with the delivered prefix count. Failure links and output chains are retained once; matched suffix lists are not duplicated per state. Runtime has no background work or per-match allocation. C++ `hx::Matcher` wraps the same object.

## 5. Bloom membership filter

`hx_os_bloom_create(context, expected_items, false_positive_rate, budget, ...)` creates a bounded membership filter. Add arbitrary byte keys, then query `hx_os_bloom_contains`. False means the key was not inserted; true can be a false positive and must be checked in the real store. Expected items is sizing information, not an insertion limit; overfilling raises false positives. There is no deletion or automatic resize. `clear` resets all bits.

The accepted target rate is 1e-9..0.5, with default memory budget 16 MiB and maximum 64 MiB. Double hashing performs no insertion allocation. Hashes are noncryptographic; the filter does not authenticate keys or isolate untrusted participants. Serialize mutations; concurrent immutable queries are allowed. C++ uses `hx::Bloom`.

## 6. Run and configure

Build/run `hx_os_fast_algorithms`: it shuffles and samples an array, radix-sorts it, searches warning patterns and uses a Bloom filter. See [fast_algorithms.c](../examples/fast_algorithms.c). C algorithms, matching and Bloom require `HX_OS_ENABLE_CONTAINERS`; disabled calls report `NOT_SUPPORTED`. System-seeded randomness additionally requires RANDOM. The core byte buffer and deterministic RNG do not require a global initialization step.
