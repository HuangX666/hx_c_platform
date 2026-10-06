# Bulk byte queues

[简体中文](BYTE_QUEUE.zh-CN.md) · [README](../README.md)

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

The creation limit bounds queued payload bytes; zero selects 64 KiB. The buffer grows on demand. Growth temporarily holds old and new storage, bounded by twice the payload limit plus allocation metadata. `hx_os_sequence_memory()` reports actual library allocation costs. Reserving through the existing sequence API can avoid later growth when that is useful.

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
