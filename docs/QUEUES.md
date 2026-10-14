# Thread message queues and concurrent rings

[简体中文](QUEUES.zh-CN.md) · [Fixed byte rings](RING.md) · [README](../README.md)

## 1. Choose an interface

| Requirement | Interface | Behavior |
| --- | --- | --- |
| Workers pass complete messages and sleep when no progress is possible | `hx_os_message_queue` | Bounded, mutex-based MPMC; deadlines and close/drain |
| An existing worker loop decides what to do when full or empty | `hx_os_ring_queue` | Bounded element ring; `try_push` and `try_pop` |
| One thread parses bytes in caller-owned memory | `hx_os_ring` | Fixed byte storage with explicit two-segment borrows |
| One thread accumulates a growing byte stream | `hx_os_byte_queue` | Bounded growth, exact and partial copied byte operations |

Message and concurrent ring queues use `HX_OS_ENABLE_QUEUES`; the byte containers use `HX_OS_ENABLE_CONTAINERS`. `hx_os_queues_available()` reports whether the thread queue module was built. No queue creates a worker, coroutine runtime or background task.

Both thread queue types allocate storage at creation and perform no heap allocation per send/receive or push/pop. Capacity is an item count, not a byte count. The item size is fixed at creation. Messages are copied as complete byte values; objects with pointer fields do not gain deep-copy or automatic resource ownership. Use plain records or define a separate ownership handoff in application code. C++ objects requiring constructors or destructors cannot be copied as raw messages.

## 2. Producer thread to processing thread

The complete [C device example](../examples/message_queue.c) creates a queue holding two `device_sample` records, starts one producer thread, and processes six measurements on the main thread. The producer submits all six records with one `send_many_until()` call; the consumer uses a three-record output array and receives whichever complete records are currently available. The two-record queue is smaller than either batch: sending waits for the consumer to make space, while receiving does not wait to fill its output array. The producer closes the queue when done. The consumer drains accepted messages until `HX_OS_EOF`, joins the producer and releases resources.

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

The receiving thread supplies another `device_sample` and calls `hx_os_message_queue_receive_until()`. It receives a whole record or a failure; no partial-record assembly is needed. A successful send copies the record before returning, so the producer may reuse its local record immediately. This is in-process communication, not a JSON or network protocol.

With examples enabled, build `hx_os_message_queue` and run the executable in the configured `bin` directory. Output begins with `Device 17, sample 1: 20.5` and ends with `Device 17, sample 6: 23.0`. The example reports operation failures. If its bounded join cannot complete, static worker state and resource handles remain valid; it does not free objects used by the worker. In a long-running application, retain that state and retry the join from the shutdown owner before cleanup.

## 3. Wait, close and ownership rules

| Situation | Status and effect |
| --- | --- |
| `HX_OS_DEADLINE_NOW` and no progress available | `HX_OS_WOULD_BLOCK`; never consumes part of a message |
| Finite monotonic deadline expires | `HX_OS_TIMEOUT`; a single-message call transfers nothing, while a batch send retains its completed prefix |
| Infinite deadline | `HX_OS_DEADLINE_INFINITE` is supported; close can wake waiters |
| Send after close | `HX_OS_CLOSED` |
| Receive after close, messages remain | Returns the next accepted complete message |
| Receive after close, queue empty | `HX_OS_EOF` |
| Repeat close | Succeeds; all waiters are notified |

Do not hold application locks needed by the other side while waiting. Mutex acquisition and condition-variable reacquisition can outlast the deadline; this is not a hard real-time guarantee. Waiter scheduling is not guaranteed to be FIFO. `size()` returns a synchronized snapshot that may change immediately afterward; make decisions from send/receive results, not a preceding size check.

Shutdown order is **close → drain accepted messages → join all users → destroy**. `close()` can run concurrently with send and receive. Destruction requires external exclusion of every caller, including threads about to enter an operation; a detected active wait returns `HX_OS_BUSY`, but this check cannot replace ownership coordination. Destroying a nonempty queue discards remaining copied bytes and does not free resources referenced by pointer fields. A join timeout leaves the queue and worker state owned by the application.

## 4. SPSC, MPSC and MPMC ring queues

`hx_os_ring_queue_create(context, item_bytes, capacity, mode, backend, &queue, error)` selects the producer/consumer contract and implementation explicitly:

| Mode | Allowed callers | `HX_OS_QUEUE_LOCKED` | `HX_OS_QUEUE_ATOMIC` |
| --- | --- | --- | --- |
| `HX_OS_QUEUE_SPSC` | One producer, one consumer | Mutex-backed try operations | Lock-free SPSC algorithm |
| `HX_OS_QUEUE_MPSC` | Multiple producers, one consumer | Mutex-backed try operations | Atomic slot reservation; no mutex, not strictly lock-free |
| `HX_OS_QUEUE_MPMC` | Multiple producers and consumers | Mutex-backed try operations | Atomic slot reservation; no mutex, not strictly lock-free |

The locked backend accepts any nonzero capacity. The atomic backend requires a power-of-two capacity of at least two and lock-free 64-bit platform atomics. Unsupported atomic capability returns `HX_OS_NOT_SUPPORTED`; invalid capacity returns `HX_OS_INVALID`. There is no silent switch to mutexes. Query `hx_os_ring_queue_is_lock_free()` when the progress guarantee matters.

```c
hx_os_ring_queue *queue = NULL;
hx_os_status status = hx_os_ring_queue_create(NULL, sizeof(device_sample),
	256, HX_OS_QUEUE_SPSC, HX_OS_QUEUE_ATOMIC, &queue, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Create failed: %s\n", hx_os_status_name(status));
```

`try_push()` and `try_pop()` return `HX_OS_WOULD_BLOCK` when full/empty, when the locked backend cannot acquire its mutex immediately, or when the next atomic slot is still reserved. The caller chooses to retry later, drop with explicit reporting, or apply backpressure. Do not busy-spin indefinitely just because these are `try` operations.

MPSC/MPMC reserve slots before publishing their contents. If a participant is paused during a reservation, another operation can be delayed by that slot even though there is no mutex. CAS contention can also repeat internal attempts. These algorithms do not promise strict lock-free progress or bounded per-call execution. SPSC avoids competing slot reservations and reports the stronger lock-free guarantee when its required atomics are supported. Lock-free does not mean faster for every workload.

The ring queue has no blocking wait, close, EOF, exact concurrent size snapshot or pointer cleanup. Coordinate producers and consumers externally, stop/join them, then destroy. When waiting and shutdown wakeups are required, prefer the message queue.

## 5. Storage and copy boundaries

Concurrent queues copy fixed-size values and own their preallocated slots. They do not expose writable slots or zero-copy borrowing. [Fixed byte rings](RING.md) provide caller-owned arrays and ordered read/write spans; their whole borrow must be externally synchronized. [ByteQueue](BYTE_QUEUE.md) offers growable byte buffering. Do not share their unsynchronized buffers between producer and consumer merely because the storage is circular.

Useful sizing starts with `item_bytes × capacity`, plus queue/slot/alignment metadata. Larger queues absorb bursts but cannot permanently compensate for a slower consumer. Measure throughput, latency and memory under the actual producer/consumer topology before selecting a backend.

## 6. C++11 wrappers

Include `<hx/Queue.hpp>` for `hx::MessageQueue<T>` and `hx::RingQueue<T>`. Both require trivially copyable `T` and return `hx::Result` without exceptions. `MessageQueue<T>::create(capacity)` provides `send()`, `receive()`, `trySend()`, `tryReceive()`, `close()` and `size()`. Pass a finite deadline to bound the waiting phase; the default `send`/`receive` deadline is infinite. `receive(T&)` writes caller storage; the value-returning overload also requires a nonthrowing default constructor.

`RingQueue<T>::create(capacity, mode, backend)` provides `tryPush()`, `tryPop()`, `capacity()` and `isLockFree()`. The default is locked MPMC. Both wrappers own their native queue; neither joins threads or makes a busy destruction safe. Keep the wrapper at a stable address while callers use it, close the message queue when appropriate, and join every caller before scope exit or a move.

## 7. Batch send and receive

Batches reduce application call overhead while retaining whole-message boundaries. Pass a contiguous array of the queue's fixed-size message type and a required `size_t *completed`. The count is initialized to zero and remains meaningful on success, partial completion and failure. The array and completion counter must not overlap queue storage or each other. A zero-count request with NULL array succeeds on a valid enabled queue, including a closed message queue.

| C entry | Waiting and partial completion |
| --- | --- |
| `hx_os_message_queue_send_many_until` | Attempts all `count` records under one deadline. `TIMEOUT`, `CLOSED` or `WOULD_BLOCK` can return with a successfully accepted prefix in `completed`. |
| `hx_os_message_queue_receive_many_until` | Waits for at least one record, then returns up to `maximum` currently available records. Does not wait to fill the array. After close, drains accepted records before `EOF`. |
| `hx_os_ring_queue_try_push_many` | Accepts as many records as it can immediately. Any positive progress returns `OK`; no progress returns `WOULD_BLOCK`. |
| `hx_os_ring_queue_try_pop_many` | Removes as many records as it can immediately. Any positive progress returns `OK`; no progress returns `WOULD_BLOCK`. |

Each record remains complete, but the whole batch is not a transaction. Other producers/consumers may interleave. Retrying a partially completed send must start at `messages + completed`, with only the remaining count; retrying the entire batch would duplicate accepted records. A batched send larger than queue capacity requires an active consumer to make progress. Memory allocation and deep-copy behavior are unchanged.

```c
size_t sent = 0;
hx_os_status status = hx_os_message_queue_send_many_until(
	queue, samples, sample_count, hx_os_deadline_after_ms(1000),
	&sent, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Accepted %zu records, stopped with %s\n",
		sent, hx_os_status_name(status));
```

C++ adds `MessageQueue<T>::sendMany()`, `receiveMany()`, `trySendMany()` and `tryReceiveMany()`, plus `RingQueue<T>::tryPushMany()` and `tryPopMany()`. They return `Result<size_t>` that retains a count even when `ok()` is false. Read the count via `value()` and inspect the status via `status()`; do not discard progress because the status is a timeout. `sendMany`/`receiveMany` default to an infinite deadline; their `try` forms use NOW.

## 8. Cancel one waiting operation

The single-message and batch `*_cancellable_until` entries accept a shared `hx_os_cancel_token`. A non-NULL token borrows one preallocated subscription slot until the call and any in-flight wake finish. Cancellation wakes the condition variable; there is no repeated polling. `CANCELLED` retains a batch sender's accepted prefix and does not close the queue or cancel other calls using another token. Completion racing cancellation may still succeed. Keep the token alive through the call, and continue to close/join queue users before destruction.

In C++, `send`, `receive`, `sendMany` and `receiveMany` accept an optional token after the deadline. Passing NULL preserves ordinary behavior. See [thread coordination](COORDINATION.md) for creating, requesting and retiring cancellation tokens.
