# Thread Coordination

[简体中文](COORDINATION.zh-CN.md)

Use the thread coordination facilities when a controller thread needs to broadcast a cooperative stop signal to worker threads, wait for a dynamic batch of tasks to finish, or protect shared data with a reader-writer lock.

### Quick Start Example

Open [coordination.c](../examples/coordination.c) for a complete runnable program: it registers work in a `WaitGroup` before launching two worker threads, increments a shared counter under a write lock, requests cancellation from the main thread, waits for the `WaitGroup` to reach zero, joins both threads, reads the final count under a read lock, and destroys all coordination objects.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_coordination` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_coordination
```

Expected output: `Processed ... records, then stopped cooperatively.` (the exact record count varies with OS thread scheduling). You can also use [coordination.c](../examples/coordination.c) as your starting `main.c` in the [minimal project](USAGE.md#c-project) setup.

## 1. Three independent tools

Enable `HX_OS_ENABLE_THREAD_EX` to use these three primitives. They operate directly on native OS threads without requiring a thread pool, coroutine runtime, or hidden background worker:

| Tool | Purpose |
| --- | --- |
| **Cancellation token (`hx_os_cancel_token`)** | Publishes a cooperative stop request, wakes supported cancellable waits, and invokes registered cancellation callbacks |
| **Wait group (`hx_os_wait_group`)** | Counts unfinished tasks, allowing active parent tasks to add child tasks before completing |
| **Read/write lock (`hx_os_rwlock`)** | Allows concurrent readers while serializing exclusive writers |

> [!IMPORTANT]
> **Cancellation is a request, and WaitGroup completion is not thread exit**
> - Requesting cancellation sets the stop flag and wakes supported waits, but it never forcibly interrupts a thread blocked inside an uncancellable third-party call.
> - A `WaitGroup` reaching zero only means every worker has called `hx_os_wait_group_done()`; the native threads may still be unwinding their stack frames or running TLS destructors. Always call `hx_os_thread_join()` on worker threads before freeing their shared state or destroying coordination objects.

The complete [coordination example](../examples/coordination.c) demonstrates the full sequence from counting work and locking shared state to requesting cancellation, waiting on the group, and joining both worker threads.

## 2. Count work before publishing it

When using `hx_os_wait_group`, always call `hx_os_wait_group_add()` on the submitting thread **before** launching the worker, and roll back the count with `hx_os_wait_group_done()` if thread creation fails:

```c
hx_os_status status = hx_os_wait_group_add(group, 1, &error);
if (status == HX_OS_OK) {
	status = hx_os_thread_create(NULL, worker, state, &thread, &error);
	if (status != HX_OS_OK)
		(void)hx_os_wait_group_done(group, 1, NULL);
}

// At the end of every worker path:
(void)hx_os_wait_group_done(group, 1, NULL);
```

- **Adding child tasks dynamically**: While the active count remains positive, a running task may add child tasks with `hx_os_wait_group_add()` before calling its own `hx_os_wait_group_done()`.
- **Waiting for a round to finish**: `hx_os_wait_group_wait_until(group, deadline, NULL, ...)` waits for the current round's count to reach zero (including child tasks added during that active round). On `HX_OS_TIMEOUT`, the count and object remain intact for retry. Wait for all callers of the previous round to return before starting a new round. Counter overflow or underflow returns `HX_OS_RANGE`, and attempting to destroy a group with active tasks or waiters returns `HX_OS_BUSY`.
- **Cancelling the wait call vs. cancelling workers**: Passing a cancellation token to `hx_os_wait_group_wait_until()` cancels **only the waiting call itself**, not the counted tasks. During shutdown, first request cancellation on your worker token, and then call `hx_os_wait_group_wait_until()` **without** the already-cancelled token so you wait for the workers to actually finish.

## 3. Cancellation and subscriptions

An `hx_os_cancel_token` is one-way (sticky): once `hx_os_cancel_request()` is called, the token remains cancelled permanently (create a new token for a new cancellation scope). By default, a token preallocates `64` callback registration slots (`max_callbacks` can configure a different bound, and cancellable waits temporarily borrow a slot while blocked):

- **Polling and cancellable waits**:
  - `hx_os_cancel_requested(token)`: Nonblocking query returning whether cancellation has been requested.
  - `hx_os_cancel_wait_until(token, deadline, error)`: Blocks until cancellation is requested (`HX_OS_OK`) or the deadline expires (`HX_OS_TIMEOUT`).
  - `hx_os_sleep_cancellable_until(deadline, token, error)`: Sleeps until the deadline expires (`HX_OS_OK`) or cancellation is requested (`HX_OS_CANCELLED`; an `HX_OS_DEADLINE_INFINITE` sleep requires a non-NULL token).
- **Subscribing callbacks (`subscribe` / `unsubscribe_until`)**:
  - `hx_os_cancel_subscribe()` returns a registration ID. The first thread that calls `hx_os_cancel_request()` invokes registered callbacks **synchronously outside the token lock**—keep callbacks short (such as waking a handle or setting a flag) and never block or join threads inside a callback.
  - Subscribing to an already-cancelled token returns `HX_OS_CANCELLED` immediately without invoking the callback or taking ownership of `user` data.
  - Always call `hx_os_cancel_unsubscribe_until(token, id, deadline, error)` before freeing data referenced by a callback. It prevents the callback from starting and waits for any in-flight execution to return; on `HX_OS_TIMEOUT`, retain the ID and user data and retry later (calling unsubscribe for a callback from inside that callback itself returns `HX_OS_DEADLOCK`).

## 4. Read/write locking

Acquire locks via `hx_os_rwlock_read_until()` or `hx_os_rwlock_write_until()`, optionally supplying a monotonic deadline (`HX_OS_DEADLINE_NOW` for nonblocking try-lock) and cancellation token:
- **Unlock on the same thread and mode**: The acquiring native thread must release the lock in the matching read or write mode. Queued writers prevent new readers from bypassing them.
- **Nonrecursive and no in-place upgrade**: Locks are nonrecursive and cannot upgrade directly from a read lock to a write lock—release the read lock first, acquire the write lock, and re-verify your invariant.
- **Native thread primitive**: `hx_os_rwlock` blocks the calling OS thread; inside coroutines, use the coroutine-aware `RwLock` in `<hx/CoroutineSync.hpp>` so waiting yields the worker thread.

## 5. C++11 facades keep host ownership visible

Include `<hx/Coordination.hpp>` for the exception-free `hx::CancelToken`, `hx::WaitGroup`, and `hx::ReadWriteLock` facades. Like `hx::Thread`, each facade is constructed around a host-owned native pointer initialized to `NULL`:

```cpp
hx_os_wait_group *groupOwner = NULL;
auto created = hx::WaitGroup::create(groupOwner);
if (!created)
	return 1;

auto &group = created.value();
auto added = group.add();
if (!added)
	return 1;

// Publish work here; every accepted task must eventually call group.done().
```

- **Explicit close and retry**: Facade destructors never block or free the underlying resource. Call `close()` explicitly when work is done (`WaitGroup::closeUntil(deadline)` waits for the count to reach zero and then destroys the native group). On `HX_OS_BUSY`, `HX_OS_TIMEOUT`, or `HX_OS_CANCELLED`, the host `owner` pointer stays intact for retry.
- **RAII lock guards (`ReadWriteGuard`)**: `ReadWriteLock::readUntil()` and `writeUntil()` return `Result<ReadWriteGuard>`. Guards are move-only, must remain on the acquiring thread, and unlock automatically on scope exit (call `guard.unlock()` explicitly if you want to inspect unlock errors; calling `lock.close()` while a guard still holds the lock returns `HX_OS_BUSY`).
- **Managing subscription IDs**: `CancelToken::subscribe(function, user)` returns `Result<uint64_t>`. Retain that ID and confirm `unsubscribeUntil(id, deadline)` succeeds before freeing any state referenced by the callback.
