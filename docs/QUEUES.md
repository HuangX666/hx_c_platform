# Thread message queues and concurrent rings

[简体中文](QUEUES.zh-CN.md) · [Fixed byte rings](RING.md) · [README](../README.md)

Use the in-process queue module when producer threads generate fixed-size records (such as sensor readings, log events, or task messages) and hand them off to consumer threads for storage or upload. Every send and receive transfers a **complete record**, so application threads never have to reassemble partial structs.

### Quick Start Example

Open [message_queue.c](../examples/message_queue.c) for a complete runnable program: it creates a two-record message queue, launches a producer thread that sends six sensor samples in a batch, receives batches on the main thread, drains remaining messages after close, and finally joins the producer and destroys the queue.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_message_queue` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_message_queue
```

Expected output: six readings for device `17`, from `Device 17, sample 1: 20.5` through `Device 17, sample 6: 23.0`. You can also copy [message_queue.c](../examples/message_queue.c) into your own project using the [minimal project](USAGE.md#c-project) setup and replace `device_sample` with your own record type.

## 1. Choose an interface

Select the container that matches your threading model and waiting requirements:

| Requirement | Interface | Behavior |
| --- | --- | --- |
| Worker threads pass complete structs and sleep when the queue is full or empty | `hx_os_message_queue` | Bounded, mutex/condition-backed MPMC queue with deadlines, cancellation, and close/drain semantics |
| An existing worker loop decides how to handle full or empty states without blocking | `hx_os_ring_queue` | Bounded fixed-element concurrent ring with nonblocking `try_push` and `try_pop` |
| A single thread (or externally locked caller) parses bytes in caller-owned memory | `hx_os_ring` | Fixed byte ring buffer with explicit two-segment read/write borrows |
| A single thread (or externally locked caller) accumulates a growable byte stream | `hx_os_byte_queue` | Bounded growable byte buffer supporting exact and partial byte operations |

- **CMake switches**: Thread message queues (`hx_os_message_queue`) and concurrent ring queues (`hx_os_ring_queue`) are controlled by `HX_OS_ENABLE_QUEUES` (queryable at runtime via `hx_os_queues_available()`), while the byte containers use `HX_OS_ENABLE_CONTAINERS`. None of the queue types spawn background threads or coroutine runtimes.
- **Preallocated storage**: Both concurrent queue types allocate their slot array once at creation and perform **zero heap allocations** during send, receive, push, or pop. Capacity is measured in **message count** (not bytes), with the per-message byte size fixed at creation.

> [!WARNING]
> **Shallow byte copies and pointer ownership**
> Queues copy records by value (`memcpy`). If a struct contains pointer fields, the queue does not deep-copy pointed-to memory or free pointed-to resources when a non-empty queue is destroyed. Prefer trivially copyable value records, or define an explicit ownership handoff in application code. C++ types with non-trivial constructors or destructors must not be passed as raw byte messages.

## 2. Producer thread to processing thread

In the complete [C device example](../examples/message_queue.c), the queue holds at most `2` `device_sample` records while the producer submits `6` records in a single `hx_os_message_queue_send_many_until()` call and the consumer reads into a `3`-element array:
- Even though the queue capacity (`2`) is smaller than both the producer batch (`6`) and the consumer array (`3`), the producer automatically sleeps until the consumer frees slots, while the consumer returns as soon as at least one record is available rather than waiting to fill all three slots.
- When finished, the producer calls `hx_os_message_queue_close()`. The consumer drains remaining queued records until `HX_OS_EOF`, joins the producer thread, and destroys the queue.

Sending a single record looks like this:

```c
typedef struct device_sample {
	uint32_t device_id;
	uint32_t sequence;
	double value;
} device_sample;

// queue was created for sizeof(device_sample); main owns its lifetime.
device_sample sample = {17, 1, 20.5};
hx_os_status status = hx_os_message_queue_send_until(
	queue, &sample, hx_os_deadline_after_ms(1000), NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Send failed: %s\n", hx_os_status_name(status));
```

The consumer supplies its own `device_sample` struct and calls `hx_os_message_queue_receive_until()`. Because `hx_os_message_queue_send_until()` copies the record into the queue before returning `HX_OS_OK`, the producer can reuse its stack variable `sample` immediately.

## 3. Wait, close and ownership rules

`hx_os_message_queue` reports the following status codes across deadlines and lifecycle transitions:

| Situation | Status and effect |
| --- | --- |
| `HX_OS_DEADLINE_NOW` when full or empty | Returns `HX_OS_WOULD_BLOCK` immediately; never transfers a partial message |
| Finite monotonic deadline expires | Returns `HX_OS_TIMEOUT`; single-message calls transfer nothing, while batch sends record their accepted prefix in `completed` |
| `HX_OS_DEADLINE_INFINITE` | Waits until space or a message becomes available, or until woken by `close()` / cancellation |
| Send after `close()` | Returns `HX_OS_CLOSED` |
| Receive after `close()` while queued messages remain | Returns `HX_OS_OK` with the next accepted message |
| Receive after `close()` once the queue is empty | Returns `HX_OS_EOF` |
| Repeated `close()` calls | Succeeds (`HX_OS_OK`) and wakes all waiting producers and consumers |

> [!IMPORTANT]
> **Required shutdown sequence**
> 1. **`close()`** (safe to call concurrently with active sends and receives; wakes all blocked waiters)
> 2. **Drain remaining messages** (consume until `receive` returns `HX_OS_EOF`)
> 3. **Join all producer and consumer threads** (if a bounded thread join returns `HX_OS_TIMEOUT`, keep the queue and worker state alive and retry the join before freeing resources)
> 4. **`destroy()`** (callers must be externally excluded before destruction; if an active waiter is detected, `destroy()` returns `HX_OS_BUSY`)

Never hold application locks required by the opposite side while waiting on a queue. `hx_os_message_queue_size()` returns a synchronized point-in-time snapshot intended for metrics; always branch on the status returned by `send` or `receive` rather than checking `size()` beforehand.

## 4. SPSC, MPSC and MPMC ring queues

When your worker loop manages its own backoff or polling without blocking on condition variables, create an `hx_os_ring_queue` via `hx_os_ring_queue_create(context, item_bytes, capacity, mode, backend, &queue, error)`:

| Mode | Allowed callers | `HX_OS_QUEUE_LOCKED` backend | `HX_OS_QUEUE_ATOMIC` backend |
| --- | --- | --- | --- |
| `HX_OS_QUEUE_SPSC` | One producer, one consumer | Mutex-backed nonblocking try operations | Lock-free SPSC ring algorithm |
| `HX_OS_QUEUE_MPSC` | Multiple producers, one consumer | Mutex-backed nonblocking try operations | Atomic slot reservation (mutex-free, not strictly lock-free) |
| `HX_OS_QUEUE_MPMC` | Multiple producers and consumers | Mutex-backed nonblocking try operations | Atomic slot reservation (mutex-free, not strictly lock-free) |

- **Capacity and platform atomic requirements**:
  - `HX_OS_QUEUE_LOCKED` accepts any nonzero capacity.
  - `HX_OS_QUEUE_ATOMIC` requires a **power-of-two capacity of at least `2`** (returning `HX_OS_INVALID` otherwise) and lock-free 64-bit platform atomics (returning `HX_OS_NOT_SUPPORTED` if unavailable, never silently falling back to a mutex). Call `hx_os_ring_queue_is_lock_free()` to query the progress guarantee.

```c
hx_os_ring_queue *queue = NULL;
hx_os_status status = hx_os_ring_queue_create(NULL, sizeof(device_sample),
	256, HX_OS_QUEUE_SPSC, HX_OS_QUEUE_ATOMIC, &queue, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Create failed: %s\n", hx_os_status_name(status));
```

- **Handling `HX_OS_WOULD_BLOCK` and progress guarantees**: `try_push()` and `try_pop()` return `HX_OS_WOULD_BLOCK` when the ring is full or empty, when the locked backend cannot immediately acquire its mutex, or when the next atomic slot is still reserved by an in-progress operation. In the `MPSC` and `MPMC` atomic backends, threads reserve a slot via CAS before copying bytes; if a thread is preempted by the OS mid-copy, other threads reaching that slot will observe `HX_OS_WOULD_BLOCK` until the reservation publishes. Always pair `HX_OS_WOULD_BLOCK` with backoff, yielding, or backpressure rather than an unbounded busy-spin.
- **Lifecycle**: `hx_os_ring_queue` has no blocking wait, `close()`, or `HX_OS_EOF`. Stop and join all producer and consumer threads externally before destroying the ring queue.

## 5. Storage and copy boundaries

Concurrent queues (`hx_os_message_queue` and `hx_os_ring_queue`) copy fixed-size records into preallocated internal slots and do not expose zero-copy slot pointers. For single-threaded byte buffering, use:
- [Fixed byte rings (`hx_os_ring`)](RING.md): Caller-owned byte arrays with two-segment read/write span borrows (requires external synchronization if shared across threads).
- [Growable byte queues (`hx_os_byte_queue`)](BYTE_QUEUE.md): Bounded dynamically resized byte streams.

Estimate queue memory starting from `item_bytes * capacity` plus queue header and per-slot metadata. Increasing capacity absorbs short bursts, but sustained producer overload requires backpressure or additional consumer throughput.

## 6. C++11 wrappers

Include `<hx/Queue.hpp>` for the exception-free `hx::MessageQueue<T>` and `hx::RingQueue<T>` wrappers (both require trivially copyable `T`):
- **`hx::MessageQueue<T>`**: Created via `MessageQueue<T>::create(capacity)`, providing `send()`, `receive()`, `trySend()`, `tryReceive()`, `close()`, and `size()`. `send()` and `receive()` default to `HX_OS_DEADLINE_INFINITE` and accept an optional monotonic deadline. `receive(T&)` populates caller storage, while `receive()` returns `Result<T>` (requiring a `noexcept` default constructor on `T`).
- **`hx::RingQueue<T>`**: Created via `RingQueue<T>::create(capacity, mode, backend)` (defaulting to locked MPMC), providing `tryPush()`, `tryPop()`, `capacity()`, and `isLockFree()`.

Both wrappers own the underlying queue handle but do not join worker threads in their destructors. Keep the wrapper at a stable address while threads use it, and join all callers before moving or destroying the wrapper.

## 7. Batch send and receive

Batch operations amortize synchronization overhead across multiple records while preserving whole-record boundaries. Pass a contiguous array of `T` and a non-NULL `size_t *completed` pointer (initialized to `0` at entry):

| C batch function | Waiting and partial-completion contract |
| --- | --- |
| `hx_os_message_queue_send_many_until` | Attempts to send all `count` records within the deadline. On `HX_OS_TIMEOUT`, `HX_OS_CLOSED`, `HX_OS_WOULD_BLOCK`, or `HX_OS_CANCELLED`, `*completed` holds the number of prefix records accepted before stopping. |
| `hx_os_message_queue_receive_many_until` | Waits until **at least one record** is available, then pops up to `maximum` currently available records without waiting to fill the entire output array. After close, drains remaining records before returning `HX_OS_EOF`. |
| `hx_os_ring_queue_try_push_many` | Pushes as many records as possible immediately. Returns `HX_OS_OK` if at least `1` record is pushed, or `HX_OS_WOULD_BLOCK` if `0` records are pushed. |
| `hx_os_ring_queue_try_pop_many` | Pops as many records as possible immediately. Returns `HX_OS_OK` if at least `1` record is popped, or `HX_OS_WOULD_BLOCK` if `0` records are popped. |

```c
size_t sent = 0;
hx_os_status status = hx_os_message_queue_send_many_until(
	queue, samples, sample_count, hx_os_deadline_after_ms(1000),
	&sent, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Accepted %zu records, stopped with %s\n",
		sent, hx_os_status_name(status));
```

> [!TIP]
> **Advancing the pointer after a partial batch send**
> Individual records are atomic, but a multi-record batch is not an all-or-nothing transaction. When `send_many_until` stops early with `*completed > 0`, resume from `samples + sent` with `sample_count - sent` items—resubmitting from index `0` would duplicate the already accepted prefix.

In C++, `MessageQueue<T>::sendMany()`, `receiveMany()`, `trySendMany()`, and `tryReceiveMany()`, plus `RingQueue<T>::tryPushMany()` and `tryPopMany()`, return `Result<size_t>` where `value()` retains the completed count even when `ok()` is `false`.

## 8. Cancel one waiting operation

To cancel an individual blocking send or receive without closing the entire message queue, call the `*_cancellable_until` variants (such as `hx_os_message_queue_send_cancellable_until` and `hx_os_message_queue_receive_cancellable_until`) with a shared `hx_os_cancel_token *`:
- A non-NULL token borrows one preallocated subscription slot for the duration of the call and wakes the condition variable directly on cancellation without polling.
- Returning `HX_OS_CANCELLED` preserves any accepted prefix in `*completed` for batch sends, leaves the queue open, and does not affect other calls using different tokens.
- In C++, `send`, `receive`, `sendMany`, and `receiveMany` accept an optional `hx_os_cancel_token *` after the deadline parameter (defaulting to `NULL`). See [thread coordination](COORDINATION.md) for cancellation token creation and lifecycle rules.
