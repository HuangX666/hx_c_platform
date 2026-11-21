# Bulk byte queues

[简体中文](BYTE_QUEUE.zh-CN.md) · [README](../README.md)

When serial ports, sockets, or pipes deliver data in arbitrary chunks and you need to accumulate those bytes in order before consuming complete headers or payloads, use the growable byte queue `hx_os_byte_queue` (or `hx::ByteQueue` in `<hx/ByteQueue.hpp>` for C++11). Built on top of the container module's circular deque storage, it grows on demand up to a configured byte ceiling and supports exact writes, non-destructive peeking, exact reads, and partial reads/writes (`read_some` / `write_some`).

See [byte_queue_ring.c](../examples/byte_queue_ring.c) for a complete runnable program: `example_queue` creates a bounded queue, appends `"hello"` with `hx_os_byte_queue_write`, consumes the available bytes with `hx_os_byte_queue_read_some`, and destroys the queue.

Follow the [example run instructions](../examples/README.md#run) to configure with `-DHX_OS_BUILD_EXAMPLES=ON` and build `hx_os_byte_queue_ring` (no command-line arguments or input files are needed). The first line of output comes from `ByteQueue` (followed by the fixed-ring demo):

```text
ByteQueue: 5 bytes: hello
Ring: efxxxxxx
```

You can also use the example source as `main.c` in the [minimal project](USAGE.md#c-project).

> [!NOTE]
> `hx_os_byte_queue` is controlled by the `HX_OS_ENABLE_CONTAINERS` CMake option. It buffers raw byte streams in memory and does not add message framing, internal mutexes, blocking waits, or worker threads. For concurrent producers and consumers, synchronize access externally or use the higher-level concurrent queues/IPC APIs.

## 1. Append, inspect and consume a block

The complete C99 program below writes four bytes (including an embedded `0x00`), inspects them with `hx_os_byte_queue_peek` without removing them, and then consumes them with `hx_os_byte_queue_read`:

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

Expected output: `Remaining: 0 bytes`.
- Embedded `NUL` (`0x00`) bytes are treated as ordinary data, and reads do not append a trailing `NUL` terminator.
- Passing `NULL` as the destination buffer to `hx_os_byte_queue_read` discards the requested number of bytes from the front of the queue without copying.
- The final parameter of every call is an optional `hx_os_error *error` (pass `NULL` when detailed error records are not needed).

## 2. Capacity and failures

- **Payload ceiling and on-demand growth**: The limit passed to `hx_os_byte_queue_create(context, max_bytes, &queue, error)` bounds the maximum queued payload bytes (`0` selects the default `64 KiB` ceiling). Storage grows on demand; during growth, the old and new buffers coexist briefly (bounded by twice the payload limit plus container metadata; query `hx_os_sequence_memory()` for exact library allocation bytes).
- **Exact read/write contracts**:
  - `hx_os_byte_queue_write` is all-or-nothing: it either appends the entire input span (`HX_OS_OK`) or leaves the queue unchanged and returns `HX_OS_LIMIT` (when the write would exceed the payload limit) or `HX_OS_NO_MEMORY` (on allocator failure).
  - `hx_os_byte_queue_read` and `hx_os_byte_queue_peek` require the full requested byte range to be present: if fewer bytes are available, they return `HX_OS_NEED_INPUT` and leave both the queue and destination buffer untouched.
  - Zero-byte reads, peeks, and writes succeed on valid arguments. The queue has no `EOF` or producer-closed state.
- **Storage constraints**: The bulk byte functions accept only plain-byte queues created by `hx_os_byte_queue_create` (rejecting other element sizes, `list`/`vector` handles, and callback-owned elements). Input and output buffers must not overlap queue-owned storage. Because the underlying buffer is circular, each read or peek performs at most two contiguous `memcpy` block copies, and each write does the same after any required growth. Any successful non-empty write or read invalidates existing container iterators on that queue.

## 3. C++11

Include `<hx/ByteQueue.hpp>` to use the RAII wrapper `hx::ByteQueue`. The function below writes three bytes containing an embedded `NUL` and reads them back into an owned `hx::Buffer`:

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

- `queue.read(pointer, bytes)` copies into caller-supplied storage, whereas `queue.read(bytes, context)` allocates and returns an owning `hx::Buffer` (if allocating the `Buffer` fails, the queued bytes remain unconsumed).
- `queue.peek(pointer, bytes, offset)` inspects bytes at `offset` without consuming them, and `queue.discard(bytes)` drops bytes from the front without copying.

## 4. Read or write the available portion

Use the capacity-management and partial I/O functions when you want to pre-allocate buffer space or consume whatever bytes have arrived so far:
- **Capacity reservation and queries**:
  - `hx_os_byte_queue_reserve(queue, capacity, error)` pre-allocates storage for `capacity` payload bytes.
  - `hx_os_byte_queue_capacity(queue)` returns the currently allocated payload capacity.
  - `hx_os_byte_queue_limit(queue)` returns the maximum allowed payload configured at creation.
  - `hx_os_byte_queue_space(queue)` returns `limit() - size()`, which is the remaining payload quota (not all of which is necessarily pre-allocated yet).
  - `hx_os_byte_queue_clear(queue)` discards all queued bytes while retaining the allocated buffer.
- **Partial reads and writes (`read_some` / `write_some`)**:
  - `hx_os_byte_queue_read_some(queue, output, requested, &completed, error)` consumes up to `min(requested, size())` bytes.
  - `hx_os_byte_queue_write_some(queue, input, requested, &completed, error)` appends up to `min(requested, space())` bytes.
  - Both functions return `HX_OS_OK` and store the actual byte count in `*completed` whenever at least one byte is transferred (or when `requested == 0`). Passing `NULL` for `output` in `read_some` discards the available prefix and reports the discarded byte count in `*completed`.
  - A non-zero `read_some` on an empty queue returns `HX_OS_NEED_INPUT`, and a non-zero `write_some` on a full queue returns `HX_OS_LIMIT` (if buffer growth fails to allocate, `write_some` returns `HX_OS_NO_MEMORY` without accepting a partial write). On any error, `*completed` is set to `0` and the queue contents remain unchanged.

```c
char block[1024];
size_t received = 0;
hx_os_status status = hx_os_byte_queue_read_some(
	queue, block, sizeof(block), &received, NULL);

if (status == HX_OS_OK)
	printf("Read %zu bytes\n", received);
```

Unlike exact `hx_os_byte_queue_read`, if the queue currently holds only 5 bytes, the `read_some` call above copies those 5 bytes into `block` immediately and sets `received` to `5`.

In C++11, `hx::ByteQueue` provides matching `capacity()`, `limit()`, `space()`, `empty()`, `reserve()`, `clear()`, `readSome()`, and `writeSome()` methods (`readSome` and `writeSome` return `hx::Result<size_t>` holding the transferred byte count).

> [!TIP]
> When you want to use a caller-supplied fixed array (such as a stack or static buffer) with zero heap allocation and borrow its two contiguous segments directly for zero-copy parsing or filling, use [the fixed ring](RING.md) (demonstrated alongside `ByteQueue` in the [ByteQueue and ring example](../examples/byte_queue_ring.c)).
