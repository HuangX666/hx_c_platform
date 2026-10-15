# Fixed byte ring buffers

[简体中文](RING.zh-CN.md) · [ByteQueue](BYTE_QUEUE.md) · [README](../README.md)

**Start with a complete program.** Use a fixed ring when you already have storage and want to fill its free regions directly.

Open [byte_queue_ring.c](../examples/byte_queue_ring.c). `example_ring` initializes eight bytes → writes abcdef → consumes abcd → fills two free spans with x → `commit` → borrows read spans → `consume`.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_byte_queue_ring`. This program needs no arguments or input files.

**Expected result:** The ring section prints `Ring: efxxxxxx`. Concatenating the spans gives the logical sequence. New bytes become readable after commit; consume makes their space reusable.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

## 1. Choose the storage model

`hx_os_ring` uses an array supplied by the caller. Both the ring object and its bytes can live on the stack or in static storage. It never allocates, grows, overwrites unread bytes, starts a thread, or waits for data. It uses the `HX_OS_ENABLE_CONTAINERS` build switch; disabled status-returning operations report `HX_OS_NOT_SUPPORTED`.

Use `hx_os_byte_queue` when growable storage and copied input/output are convenient. Use this fixed ring for a known memory budget or direct access to its two segments. Neither provides concurrent access: a lock must protect the complete operation or borrow when sharing across threads. A ring without an internal mutex is not a lock-free concurrent queue.

## 2. Ordinary reads and writes

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

This prints `hello`. No destruction call is needed. Keep `storage` alive while the ring or any borrowed segment uses it. An initialized live C ring must not be copied or have its fields edited. Reinitialization discards data and is allowed only after all operations and borrows finish; initialization accepts an uninitialized object and cannot inspect its old state.

`size()` counts queued bytes, `capacity()` is the exact array length, and `space()` is their difference. `clear()` discards queued bytes while keeping the array. The object adds metadata outside this array. A zero-length ring is valid; it accepts zero-byte copies but cannot hold data.

| Operation | Insufficient data/space | Effect on failure |
| --- | --- | --- |
| `hx_os_ring_write` | `HX_OS_OUTPUT_FULL` | No queued bytes change |
| `hx_os_ring_read` | `HX_OS_NEED_INPUT` | Queue and destination remain unchanged |
| `hx_os_ring_read(..., NULL, bytes, ...)` | `HX_OS_NEED_INPUT` | Discards exactly `bytes` only on success |

Ordinary copy buffers must not overlap the ring object or its backing array. There is no EOF or producer-close state. All sizes are byte counts, independent of text encoding.

## 3. Borrow two segments without an intermediate copy

When data wraps around the array end, logical bytes occupy two segments. The first segment comes before the second in stream order. Borrowing exposes both at once; unused second segments are `{NULL, 0}`.

```c
hx_os_ring_write_span parts[2];
hx_os_status status = hx_os_ring_write_spans(&ring, parts, NULL);

if (status == HX_OS_OK) {
	// Example: initialize one byte directly in available ring storage.
	parts[0].data[0] = 'A';
	status = hx_os_ring_commit(&ring, 1, NULL);
}
```

The matching read side is `hx_os_ring_read_spans()` followed by `hx_os_ring_consume()`. A parser can inspect the borrowed segments and consume only the recognized prefix. `commit()` publishes only the prefix that the caller actually initialized; the remaining space stays free. This removes an intermediate buffer copy, not every copy performed by the operating system or application.

Important lifetime rules:

- At most one borrow exists, of either direction. Other borrows, copied reads/writes and `clear()` return `HX_OS_BUSY` until it ends.
- `commit(0)` or `consume(0)` explicitly abandons the matching borrow without changing the byte count.
- Successful commit/consume invalidates both segments, even when only part was committed/consumed. Acquire fresh segments for further work.
- A count above the offered size returns `HX_OS_RANGE` and retains the borrow for retry. Using the wrong completion direction returns `HX_OS_INVALID` and also retains it.
- An empty read borrow returns `HX_OS_NEED_INPUT`; a full write borrow returns `HX_OS_OUTPUT_FULL`. These failures do not create a borrow.
- Release every borrow before clearing, reinitializing or ending the ring/storage lifetime. No function or destructor commits data automatically.

## 4. C++11 and complete example

`<hx/Ring.hpp>` provides `hx::Ring::create(storage, bytes)`, `write()`, `read()`, `discard()`, `clear()` and capacity queries. Operations return `hx::Result`. `readSpans()` and `writeSpans()` return an object whose `parts[0]` and `parts[1]` hold the two segments; call `consume()` or `commit()` explicitly. The wrapper borrows storage and is move-constructible, not copyable or assignable. Moving transfers a pending borrow to the new object; the moved-from object is empty. Destruction does not commit or release caller storage.

Build `hx_os_byte_queue_ring` with examples enabled to run the complete [C example](../examples/byte_queue_ring.c). It shows growable ByteQueue reads and a fixed ring spanning the array boundary. Expected output:

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```
