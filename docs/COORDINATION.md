# Thread Coordination

[简体中文](COORDINATION.zh-CN.md)

## 1. Three independent tools

Enable `HX_OS_ENABLE_THREAD_EX`. These facilities use native threads and require no thread pool, coroutine runtime or background worker.

| Tool | Purpose |
|---|---|
| Cancellation token | Publish a cooperative stop request, wake supported waits, or request cancellation of registered work. |
| Wait group | Count unfinished tasks, including children added while a parent is still outstanding. |
| Read/write lock | Allow concurrent readers while protecting exclusive writes. |

Cancellation is a request, not proof that work finished. An uncancellable third-party blocking function will not be forcibly interrupted. Wait-group completion also does not join native threads or run their TLS cleanup; join threads separately before releasing their state.

The complete [coordination example](../examples/coordination.c) runs two workers, updates a shared record count under a write lock, requests cancellation, waits for the group, joins both threads and reads the final count under a read lock. Build `hx_os_coordination` in the existing examples-enabled build directory. The count varies with scheduling; successful completion, not a fixed number, is the expected result.

## 2. Count work before publishing it

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

The first block runs in the submitting thread; the final `done` belongs inside the worker, including its error/cancellation paths. A failed launch rolls back its count. A task may add children while the count remains positive, before calling its own `done`.

`hx_os_wait_group_wait_until(group, deadline, NULL, ...)` waits for the current round to reach zero. This is not a snapshot: children added to that active round are included. Timeout leaves the count and ownership intact. A new round may start only after all waits from the previous round have returned. Underflow/overflow returns `RANGE`; destroying an active group returns `BUSY`.

Passing a cancellation token to the wait cancels the waiting caller, not the counted tasks. During shutdown, request cancellation of the workers first and then wait without the already-cancelled token to observe their actual completion.

## 3. Cancellation and subscriptions

A token is sticky. Once requested, it stays requested; create another token for another scope. Default creation preallocates 64 registration slots. `max_callbacks` can set a different finite limit. Supported cancellable waits use temporary registration slots too.

- `hx_os_cancel_requested` checks the flag.
- `hx_os_cancel_wait_until` returns `OK` when cancellation is requested and `TIMEOUT` when its wait expires.
- `hx_os_sleep_cancellable_until` returns `CANCELLED` on cancellation and `OK` when the sleep deadline expires. An infinite cancellable sleep requires a token.
- `subscribe` returns a registration ID. The first requesting thread invokes callbacks synchronously outside the token lock. Callbacks must be short wake/request operations, not joins or blocking business work.
- Subscribing after cancellation returns `CANCELLED` without calling the callback or taking ownership of its user data.
- `unsubscribe_until` prevents new starts and waits for an in-flight callback. Timeout retains the ID and user-data lifetime obligation; retry with the same ID. A callback waiting for itself returns `DEADLOCK`.

Task, I/O request and coroutine bindings use their existing cancellation contracts. A successful binding retains its target until successful unsubscribe; completion alone does not remove the registration. Bindings do not create a detached thread to run a blocking callback. Another concurrent `request` may return before the first request's callback dispatch finishes.

## 4. Read/write locking

Acquire using `hx_os_rwlock_read_until` or `hx_os_rwlock_write_until`, optionally with a token. Unlock on the same native thread and in the same mode. Writers already queued block new readers, but strict FIFO ordering and freedom from starvation are not promised.

Locks are nonrecursive. Holding a read lock and asking to upgrade to a write lock is unsupported; release and reacquire, then recheck the shared condition. Read ownership is a caller contract, not an allocated per-thread ownership table. These native waits do not suspend a coroutine independently of its worker thread; coroutine code should use coroutine synchronization facilities.

Waits use monotonic deadlines. Mutex reacquisition and cancellation-registration retirement may exceed a requested deadline. `NOW` attempts without waiting for the condition, but none of these interfaces promises a hard real-time bound.

## 5. C++11 facades keep host ownership visible

Include `<hx/Coordination.hpp>`. Create `CancelToken`, `WaitGroup` and `ReadWriteLock` with a reference to a host-owned pointer initialized to NULL, following `hx::Thread`.

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

Keep the owner pointer, facade and worker data in the host's lifetime state. Facade destruction does not wait or free the resource; call `close()` explicitly. `WaitGroup::closeUntil(deadline)` first waits, then destroys when safe. `BUSY`, `TIMEOUT` or `CANCELLED` leaves the owner available for retry. Close requires exclusion of new callers and submissions.

`ReadWriteLock::readUntil` and `writeUntil` return `Result<ReadWriteGuard>`. A guard unlocks at scope exit, is move-only, and must stay on the acquiring thread. Keep the lock owner alive until all guards are released. Explicit `guard.unlock()` lets you inspect errors; `lock.close()` returns `BUSY` while a guard still holds the lock.

`CancelToken::subscribe(function, user)` returns `Result<uint64_t>`; retain that ID and call `unsubscribeUntil(id, deadline)` before releasing callback data. The facade does not hide an allocated lambda owner or silently drop a timed-out subscription. All methods use `Result`, with no exceptions.
