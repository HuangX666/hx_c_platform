# Fixed byte ring buffers

[简体中文](RING.zh-CN.md) · [ByteQueue](BYTE_QUEUE.md) · [README](../README.md)

When you already have a fixed-size byte array (on the stack, in static storage, or in a pre-allocated memory pool) and want to reuse that buffer circularly with **zero heap allocation**—or borrow its two contiguous free/readable segments (spans) directly so producers and parsers can work in place without an intermediate buffer copy—use `hx_os_ring` (or `hx::Ring` in `<hx/Ring.hpp>` for C++11).

To see how it works, open [byte_queue_ring.c](../examples/byte_queue_ring.c): `example_ring` initializes a ring over an 8-byte array, writes `"abcdef"`, consumes `"abcd"` (leaving `"ef"` near the end of the array), borrows the two remaining free spans across the wrap-around boundary to fill them with `'x'`, commits those writes with `hx_os_ring_commit`, and then borrows and consumes the readable spans.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_byte_queue_ring` (no arguments or input files are required). The second output line comes from the fixed ring:

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```

You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

## 1. Choose the storage model

`hx_os_ring` operates entirely on a byte array supplied by the caller. Both the `hx_os_ring` struct and its backing array can live on the stack or in static storage: the ring never allocates heap memory, grows, overwrites unread bytes, spawns worker threads, or blocks waiting for data. It shares the `HX_OS_ENABLE_CONTAINERS` CMake switch; when disabled, status-returning calls report `HX_OS_NOT_SUPPORTED`.

| Aspect | `hx_os_ring` (Fixed byte ring) | `hx_os_byte_queue` ([Growable byte queue](BYTE_QUEUE.md)) |
| --- | --- | --- |
| Memory backing | Borrows a caller-supplied fixed array; zero heap allocation | Managed by the container allocator; grows on demand up to a limit |
| Access style | Copied reads/writes + **direct two-span borrow and commit/consume** | Exact copied reads/writes, peeks, and `read_some`/`write_some` (no raw span borrows because growth can relocate storage) |
| Best fit | Fixed memory budgets, zero-allocation embedded paths, in-place packet assembly or parsing | Variable burst sizes where automatic buffer growth is preferred |

> [!IMPORTANT]
> Neither `hx_os_ring` nor `hx_os_byte_queue` is thread-safe on its own (having no internal mutex does not make it a lock-free concurrent queue). When sharing a ring across threads, protect each complete operation—or the entire borrow window from `write_spans`/`read_spans` through `commit`/`consume`—with an external mutex.

## 2. Ordinary reads and writes

For copied reads and writes, include `<hx_ring.h>` and call `hx_os_ring_init`, `hx_os_ring_write`, and `hx_os_ring_read`:

```c
#include <hx_ring.h>
#include <stdio.h>

int main(void)
{
	unsigned char storage[256];
	hx_os_ring ring;
	char text[6] = {0};
	hx_os_status status;

	status = hx_os_ring_init(&ring, storage, sizeof(storage), NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_write(&ring, "hello", 5, NULL);
	if (status == HX_OS_OK)
		status = hx_os_ring_read(&ring, text, 5, NULL);
	if (status == HX_OS_OK)
		puts(text);

	return status == HX_OS_OK ? 0 : 1;
}
```

This prints `hello`, and no destruction call is required.
- **Backing array lifetime**: Keep `storage` alive for as long as `ring` or any borrowed span uses it. Never copy an initialized `hx_os_ring` struct by value or edit its fields directly. Reinitializing an existing `hx_os_ring` with `hx_os_ring_init` discards queued data and is valid only when no operation or span borrow is active.
- **Capacity queries and reset**: `hx_os_ring_size(&ring)` returns the number of queued readable bytes, `hx_os_ring_capacity(&ring)` returns the exact backing array length, and `hx_os_ring_space(&ring)` returns `capacity - size`. `hx_os_ring_clear(&ring, error)` discards queued bytes while keeping the backing array attached. A zero-capacity ring is valid and accepts zero-byte copies.

| Operation | Insufficient space or data | Effect on failure |
| --- | --- | --- |
| `hx_os_ring_write(&ring, data, bytes, error)` | `HX_OS_OUTPUT_FULL` (free space `< bytes`) | Queued bytes remain unchanged; no partial write |
| `hx_os_ring_read(&ring, out, bytes, error)` | `HX_OS_NEED_INPUT` (queued bytes `< bytes`) | Both the ring and `out` buffer remain unchanged |
| `hx_os_ring_read(&ring, NULL, bytes, error)` | `HX_OS_NEED_INPUT` (queued bytes `< bytes`) | Discards exactly `bytes` only when enough data is present |

Source and destination buffers passed to `hx_os_ring_write` and `hx_os_ring_read` must not overlap the `hx_os_ring` struct or its backing `storage` array.

## 3. Borrow two segments without an intermediate copy

When queued data or free space wraps around the end of the backing array, the logical sequence occupies at most two contiguous physical segments: `parts[0]` at the tail of the array followed in stream order by `parts[1]` at the front of the array (if no wrap-around occurs, `parts[1]` is `{NULL, 0}`).

Borrowing exposes both segments at once so you can format bytes directly into free space or run a parser directly over readable bytes without copying through an intermediate scratch buffer:

```c
hx_os_ring_write_span parts[2];
hx_os_status status = hx_os_ring_write_spans(&ring, parts, NULL);

if (status == HX_OS_OK) {
	// Example: initialize one byte directly in available ring storage.
	parts[0].data[0] = 'A';
	status = hx_os_ring_commit(&ring, 1, NULL);
}
```

The read side works symmetrically: call `hx_os_ring_read_spans(&ring, parts, error)` to inspect `parts[0]` and `parts[1]`, and then call `hx_os_ring_consume(&ring, bytes, error)` to advance the read cursor by the number of bytes your parser actually consumed. Similarly, `hx_os_ring_commit(&ring, bytes, error)` publishes only the prefix that the caller initialized, leaving the remaining space free.

> [!WARNING]
> **Span borrow lifetime and state-machine rules:**
> 1. **One active borrow at a time**: At most one read borrow or write borrow may be active on a ring. Attempting another borrow, a copied read/write, or `clear()` while a borrow is open returns `HX_OS_BUSY`.
> 2. **Abandoning a borrow**: Calling `commit(0)` or `consume(0)` ends the matching borrow cleanly without changing the queued byte count.
> 3. **Spans expire on commit/consume**: A successful `commit` or `consume` invalidates both `parts[0]` and `parts[1]` immediately, even if only part of the offered span was committed or consumed. Call `write_spans` or `read_spans` again for subsequent work.
> 4. **Recoverable range/direction errors**: Passing a byte count larger than the offered span total returns `HX_OS_RANGE`, and calling `commit` on a read borrow (or `consume` on a write borrow) returns `HX_OS_INVALID`. Both errors keep the active borrow open so the caller can retry with valid arguments.
> 5. **Empty or full borrow attempts**: Calling `read_spans` on an empty ring returns `HX_OS_NEED_INPUT`, and calling `write_spans` on a full ring returns `HX_OS_OUTPUT_FULL`; neither failure opens a borrow.
> 6. **No implicit commit**: Always complete or abandon any active borrow before clearing, reinitializing, or ending the lifetime of the ring or its backing array; destructors never commit unacknowledged bytes automatically.

## 4. C++11 and complete example

`<hx/Ring.hpp>` provides `hx::Ring::create(storage, bytes)` along with `write()`, `read()`, `discard()`, `clear()`, `size()`, `capacity()`, and `space()`, returning `hx::Result<T>`.
- `readSpans()` and `writeSpans()` return a two-segment struct (`parts[0]` and `parts[1]`); call `consume(bytes)` or `commit(bytes)` explicitly when done.
- `hx::Ring` borrows the caller's array and is move-constructible (moving transfers any open borrow to the destination object and leaves the moved-from wrapper empty) but non-copyable. Its destructor neither commits pending writes nor frees the caller's backing storage.

Build and run `hx_os_byte_queue_ring` with `-DHX_OS_BUILD_EXAMPLES=ON` to execute the complete [C example](../examples/byte_queue_ring.c):

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```
