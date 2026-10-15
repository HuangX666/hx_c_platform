# Bulk byte queues

[简体中文](BYTE_QUEUE.zh-CN.md) · [README](../README.md)

**Start with a complete program.** Use ByteQueue to accumulate partial serial/network bytes and consume them in order. It does not parse messages.

Open [byte_queue_ring.c](../examples/byte_queue_ring.c). `example_queue` creates a bounded queue → `write` adds hello → `read_some` returns available bytes and their count → `destroy`.

Follow the [example run instructions](../examples/README.md#run), enable examples and build only `hx_os_byte_queue_ring`. This program needs no arguments or input files.

**Expected result:** The first line is `ByteQueue: 5 bytes: hello`; the program then demonstrates a fixed ring. Process the returned byte count. The buffer adds neither a string terminator nor thread synchronization.

Short snippets below explain individual calls; use the complete example when running them. You can also use its source as main in the [minimal project](USAGE.md#c-project).

Use `hx_os_byte_queue` to accumulate a byte stream and consume complete pieces. It reuses the existing deque storage and CONTAINERS switch. It does not provide message framing, locking, blocking waits or background threads. For concurrent producers/consumers, the caller supplies synchronization or uses the higher-level IPC API.

## 1. Append, inspect and consume a block

This complete C99 program stores four bytes, inspects them without consuming them, then reads them back:

```c
#include <hx_container.h>
#include <stdio.h>

int main(void)
{
	hx_os_byte_queue *queue = NULL;
	unsigned char packet[4] = {1, 0, 2, 3};
	unsigned char result[4];
	hx_os_status status = hx_os_byte_queue_create(NULL, 4096, &queue, NULL);
	if (status != HX_OS_OK)
		return 1;

	status = hx_os_byte_queue_write(queue, packet, sizeof(packet), NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_peek(queue, 0, result, 4, NULL);
	if (status == HX_OS_OK)
		status = hx_os_byte_queue_read(queue, result, 4, NULL);
	if (status == HX_OS_OK)
		printf("Remaining: %zu bytes\n", hx_os_byte_queue_size(queue));

	hx_os_byte_queue_destroy(&queue, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
```

Expected output: `Remaining: 0 bytes`. NUL is ordinary data. Passing NULL as the read destination discards the requested bytes. The last argument is an optional error record.

## 2. Capacity and failures

The creation limit bounds queued payload bytes; zero selects 64 KiB. The buffer grows on demand. Growth temporarily holds old and new storage, bounded by twice the payload limit plus allocation metadata. `hx_os_sequence_memory()` reports actual library allocation costs. `hx_os_byte_queue_reserve(queue, capacity, error)` reserves payload storage in advance. `capacity()` reports allocated payload bytes; `limit()` reports the maximum allowed payload; `space()` is `limit()-size()`, which may exceed currently allocated free storage. `clear()` discards queued bytes but retains allocation. No existing layouts or exact read/write behavior changed.

Writes are complete or return LIMIT/NO_MEMORY without changing contents. Reads and peeks are exact: if the requested range is not available, they return NEED_INPUT and preserve the queue and output. Zero-byte operations succeed on a valid range. This queue has no EOF or producer-close state.

The default factory creates plain byte storage with no element callbacks. Bulk operations reject other element types, lists/vectors and callback-owned elements. Buffers may not alias queue-owned storage. Successful nonempty writes and reads invalidate existing container iterators. Reads and peeks perform at most two block copies; writes do the same after any required growth. These are implementation properties, not a throughput benchmark.

## 3. C++11

Include `<hx/ByteQueue.hpp>`. This function returns an owned three-byte result, preserving the embedded NUL:

```cpp
hx::Result<hx::Buffer> queued_bytes() noexcept
{
	auto created = hx::ByteQueue::create(4096);
	if (!created)
		return hx::Failure(created.error());

	auto &queue = created.value();
	auto written = queue.write("a\0b", 3);
	if (!written)
		return hx::Failure(written.error());

	return queue.read(size_t(3));
}
```

`read(pointer, bytes)` uses caller storage; `read(bytes, context)` returns an owned `Buffer`. `peek(pointer, bytes, offset)` leaves the queue unchanged; `discard(bytes)` consumes without copying. Allocation failure when obtaining an owned result does not consume the queue. The facade owns the native queue and destroys it without waiting. Keep concurrent access externally serialized.

## 4. Read or write the available portion

`hx_os_byte_queue_read_some(queue, output, requested, &completed, error)` consumes up to the requested number of available bytes. `write_some()` accepts up to the remaining payload limit. Both return `HX_OS_OK` with the actual byte count, even when fewer bytes were copied. Check `completed` when the whole request matters; no hidden retry is performed.

A nonzero read from an empty queue returns `HX_OS_NEED_INPUT`; a nonzero write to a full queue returns `HX_OS_LIMIT`. Errors set `completed` to zero and preserve contents. Allocation failure does not accept a partial write. A zero-byte request succeeds with zero progress. The `completed` pointer is required and cannot alias the data or queue storage. Passing NULL read output discards the available prefix.

```c
char block[1024];
size_t received = 0;
hx_os_status status = hx_os_byte_queue_read_some(
	queue, block, sizeof(block), &received, NULL);

if (status == HX_OS_OK)
	printf("Read %zu bytes\n", received);
```

Unlike exact `read`, this code can retrieve a five-byte queue into a 1024-byte destination immediately. Bytes are not NUL-terminated automatically.

C++ adds `capacity()`, `limit()`, `space()`, `empty()`, `reserve()`, `clear()`, `readSome()` and `writeSome()`. The `Some` methods return `Result<size_t>` with the copied byte count. Ordinary `read()`/`write()` still require the whole range.

For a caller-supplied fixed array and direct two-segment access, use [the fixed ring](RING.md). Its borrow/commit interface is separate because growable storage can move. See the complete [ByteQueue and ring example](../examples/byte_queue_ring.c).
