# C++11 thread-pool facade

[简体中文](CPP_POOL.zh-CN.md) · [README](../README.md) · [Result contracts](CPP_RESULTS.md)

Use `hx/ThreadPool.hpp` when you want to offload independent calculations or background jobs to a fixed number of worker threads and retrieve their results under an explicit deadline. It provides a header-only C++11 facade over the bounded C99 thread pool: link `hx_c_platform::cpp`, handle outcomes via `hx::Result<T>`, and compile with or without C++ exceptions (`-fno-exceptions` / `/EHs-c-`). The underlying scheduler and public C ABI remain pure C99. Coroutines live in a [separate module](COROUTINES.md) and are not needed for standard thread-pool jobs.

### Quick Start Example

Open [thread_pool.cpp](../examples/thread_pool.cpp) for a complete runnable program: it creates a two-worker pool with `withThreads(owner, 2)`, submits a job with input `21`, retrieves the computed answer via `getUntil()`, and shuts the pool down cleanly with `shutdownUntil()`.

Follow the [example run instructions](../examples/README.md#run) to enable examples and build `hx_os_cpp_thread_pool` (no command-line arguments or input files are required):

```sh
cmake --build build --config Release --target hx_os_cpp_thread_pool
```

Expected output:

```text
Task result: 42
```

To integrate the thread pool into your own application, you can copy [thread_pool.cpp](../examples/thread_pool.cpp) as your starting `main.cpp` using the [minimal project](USAGE.md#cpp-project) setup.

## Ownership and shutdown

`hx::ThreadPool` is a lightweight non-owning view over a host-owned `hx_os_thread_pool *` pointer:
- **Host-owned lifetime**: The native `owner` variable must outlive every `ThreadPool` view and must not be modified concurrently while pool operations are running. Copying a `ThreadPool` view still refers to the same `owner`.
- **No hidden blocking in destructors**: Destroying a `ThreadPool` view does not close the pool, block indefinitely, or detach worker threads. The host application is responsible for explicitly closing and destroying the native `owner`.
- **Reference-counted task handles**: `Task<T>` and `CancellationSource` automatically manage their own native references. A `Task<T>` may outlive a destroyed thread pool, though unconsumed results keep their allocation `context` active until released.

The [complete example](../examples/thread_pool.cpp) checks creation, submission, result retrieval, and shutdown step by step. Its two-second deadline is an application choice: in a long-running service, keep the native `owner` in host state, stop new submissions, and retry shutdown with a fresh monotonic deadline if needed. `shutdownUntil()` combines close and destroy, clearing `owner` to `NULL` **only when both steps succeed**. Callers must exclude new operations and concurrent destruction before performing final teardown.

Once `closeUntil()` begins, the pool permanently rejects new submissions and supports two modes:
- `HX_OS_POOL_DRAIN`: Executes all accepted tasks already in the queue before stopping workers.
- `HX_OS_POOL_CANCEL_PENDING`: Skips queued tasks that have not started yet (still running their cleanup callbacks) without forcibly interrupting tasks already running on workers.

Both modes wait for task cleanup and native worker thread exit. Calling close from a worker thread belonging to the same pool returns `HX_OS_DEADLOCK`. Concurrent close calls or in-flight task preparation can return `HX_OS_BUSY`. The library never spawns hidden background reaper threads or forcibly terminates worker threads.

> [!IMPORTANT]
> **Why must `owner` be retained when `shutdownUntil()` returns `HX_OS_TIMEOUT` or `HX_OS_BUSY`?**
> As long as a worker thread is still running, it may be accessing pool state or captured task data. Freeing those resources on a timeout would cause a use-after-free crash in the background thread. Therefore:
> - Worker thread exit and native thread-handle cleanup are separate steps. If `destroy()` or `shutdownUntil()` returns `HX_OS_TIMEOUT`, `HX_OS_BUSY`, or a native cleanup error, retain the non-NULL `owner` pointer and any borrowed data, then retry `shutdownUntil()` later (already released worker handles are skipped, and completed tasks or cleanup callbacks are never run twice). The C functions `hx_os_thread_pool_destroy()` and `hx_os_thread_pool_shutdown()` follow the exact same rule.
> - Pool creation can also leave a non-NULL closing `owner` if partial worker startup fails and rollback cleanup cannot finish before the deadline. Always inspect `owner` even when `create()` or `withThreads()` returns a failed `Result`: do not submit work, and use `hx::ThreadPool(owner).shutdownUntil(deadline)` to finish cleanup. See [shared lifetime rules](CONTRACTS.md#shared-rules).

## Wait for work without closing the pool

When you want to wait for current jobs to finish while **keeping the pool open** for subsequent batches, choose between two waiting modes:
- **Batch snapshot wait (`waitUntil(deadline)` / `waitFor(duration)`)**: Captures the boundary of already accepted tasks under the pool mutex and waits only for that batch; tasks submitted after the boundary is captured do not extend the wait.
- **Pool-idle wait (`waitIdleUntil(deadline)` / `waitIdleFor(duration)`)**: Waits until no accepted tasks remain in the pool; continuous submissions during the wait can delay completion.

Both modes include delayed tasks and task cleanup callbacks, leave the pool open, and return `HX_OS_TIMEOUT` if the monotonic deadline expires (a zero duration polls immediately):

```cpp
auto first = pool.submit([]() noexcept { return 21 * 2; });
if (!first)
	return hx::Failure(first.error());

auto completed = pool.waitFor(std::chrono::milliseconds(2000));
if (!completed)
	return completed;

// Waiting leaves the pool available for the next batch.
auto second = pool.submit([]() noexcept { return 7 * 3; });
```

This snippet runs inside a function returning `hx::Result<void>` with an active `pool`. Always check `second` before using it, and call each task's `getUntil()` to inspect its outcome: a successful pool wait confirms that tasks and their cleanup callbacks have finished (including tasks that failed or were cancelled), not that every task succeeded.

Multiple external threads may wait concurrently. Calling either wait function from a worker thread (or task cleanup callback) of the same pool returns `HX_OS_DEADLOCK`. Tasks still being prepared outside the pool mutex are not included until formally accepted. Calling `waitUntil()` again after a timeout captures a fresh snapshot boundary; an idle wait succeeding is a point-in-time observation and does not block subsequent submissions. If the internal 64-bit submission sequence ever saturates, new submissions return `HX_OS_LIMIT` rather than wrapping. The C equivalents are `hx_os_thread_pool_wait_until()` and `hx_os_thread_pool_wait_idle_until()`.

## Submission and results

`submit(callable, args...)` returns `Result<Task<T>>`. It accepts plain functions, member pointers, lambdas, move-only captures and arguments, and `std::ref`. The stored callable is invoked as an lvalue while stored arguments are moved into the call.

> [!NOTE]
> **No-exception callable contract**: Function objects and lambdas must mark their invocation operator `noexcept`, and constructing or destroying captures and arguments must not throw. Because C++11 plain function pointers and member pointers cannot encode `noexcept` in their type system, their implementations must uphold the same non-throwing guarantee by contract.

| Callable return type | `submit()` return type | `get()` / `getUntil()` return type and semantics |
| --- | --- | --- |
| `T` | `Result<Task<T>>` | `Result<T>` |
| `Result<T>` | `Result<Task<T>>` | `Result<T>`, preserving native `hx_os_error` details and partial values |
| `void` or `Result<void>` | `Result<Task<void>>` | `Result<void>` |
| `T&` | `Result<Task<T&>>` | `Result<T&>`, borrowing external storage that outlives consumption |

A callable returning a raw `hx_os_status` integer produces `Task<hx_os_status>`, treating that integer as the task's value. Return `Result<void>` instead when the status should represent task success or failure.

`Task<T>` provides the following operations:

| Method | Contract |
| --- | --- |
| `valid()` | Returns `true` if the handle holds a native task reference |
| `ready()` | Nonblocking query for task and capture-cleanup completion |
| `waitUntil(deadline)` | Returns `Result<void>`; waits until an absolute monotonic deadline |
| `waitFor(milliseconds)` | Returns `Result<void>`; zero or negative duration polls without blocking |
| `wait()` | Returns `Result<void>`; explicit unbounded wait for completion |
| `getUntil(deadline)` | Returns `Result<T>`; preserves the `Task` handle on wait timeout/failure, and consumes it once the task has finished |
| `get()` | Returns `Result<T>`; explicit unbounded wait that consumes the completed outcome |
| `cancel()` | Requests cooperative cancellation |
| `cancellationSource()` | Returns an independently reference-counted `CancellationSource` for a controller thread |

Note that `wait*()` succeeding only reports that execution has finished; inspect `getUntil()` or `get()` to see whether the task succeeded, failed, or was cancelled. Waiting on an unfinished task from a worker in the same pool returns `HX_OS_DEADLOCK`. Once a task finishes (whether by succeeding, failing, or being cancelled while queued), `getUntil()` or `get()` consumes the `Task` handle. Destroying a `Task` handle merely releases the observation reference without cancelling the underlying job.

`Result<T>` stores its value inline without extra heap allocation. Always check `ok()` (or `hasValue()` on interfaces with documented partial progress) before calling `value()`, or use `get()` to obtain a nullable pointer. Returned references (`Result<T&>`) must outlive consumption and must never point to captures or arguments that are destroyed during worker cleanup.

## Cancellation

Use `submitCancelable()` to pass an `hx::CancellationToken` into your job and extract an independent `hx::CancellationSource` for the controlling thread:

~~~cpp
#include <hx/ThreadPool.hpp>

hx::Result<hx::CancellationSource>
start_work(hx::ThreadPool &pool) noexcept
{
	auto submitted = pool.submitCancelable(
		[](hx::CancellationToken token) noexcept -> hx::Result<void> {
			while (!token.stopRequested()) {
				// Perform one bounded unit of work.
				hx_os_cpu_relax();
			}
			return token.check();
		});

	if (!submitted)
		return hx::Failure(submitted.error());

	return submitted.value().cancellationSource();
}
~~~

After a controller calls `source.requestStop()`, `token.stopRequested()` returns `true` and `token.check()` returns `HX_OS_CANCELLED`.

- **Independent lifetime**: Both `CancellationSource` and `CancellationToken` retain native task references and may safely outlive the thread pool.
- **Thread-safety boundary**: Do not invoke methods on, move, consume, or destroy the same `Task` instance concurrently from multiple threads. Instead, copy a `CancellationSource` via `task.cancellationSource()` before moving the `Task` to a waiting thread; separate `CancellationSource` instances can safely call `requestStop()` concurrently with task execution and waiting.
- **Cooperative semantics**: Cancellation is cooperative and cannot forcibly interrupt arbitrary blocking user or third-party calls.

## Admission, allocation and cleanup

Task submission uses prepared admission (`hx_os_thread_pool_submit_prepared()`):
1. **Capacity reserved first**: Queue capacity is reserved before constructing the C++ job. A full or closed queue rejects submission immediately without copying the callable or allocating C++ job storage.
2. **Preparation outside the pool lock**: C++ job allocation and capture construction run outside the pool mutex. An allocation failure returns `HX_OS_NO_MEMORY` and releases any partial resources.
3. **Close race cleanup**: If pool shutdown races with a job that has already finished preparation, submission returns `HX_OS_CLOSED` and the submitting thread cleans up the prepared job once (note that moved-from arguments have already been consumed and cannot be restored). Once a job is accepted into the queue, its captures are cleaned up on a worker thread exactly once—even if the job is later skipped by `HX_OS_POOL_CANCEL_PENDING`.

The callable, inline result slot, and reference state share a single context allocation aligned to the job's actual alignment (`alignas`), while native task and event structures are allocated separately. The default inline job size limit is `1 MiB` and can be adjusted via `ThreadPool(owner, maxTaskBytes)`. This bound limits the inline job structure itself, not external heap graphs owned by user pointers; holding onto many `Task` or result handles after the queue drains will keep their backing `context` memory allocated.

> [!WARNING]
> **Destructor and callback execution rules**
> Capture and result destructors may execute on a **worker thread**, on a **rejecting submission thread**, or on the **thread releasing the final task reference**. Destructors must return normally without unbounded blocking and must never unwind exceptions or `longjmp` across C stack frames.

## Alignment, rebuilding and trimming

- **Over-aligned types**: Inline result and capture slots use C++11 `alignas` and allocate through the bound `hx_os_context` using the job's true alignment, supporting `16`- through `128`-byte aligned results, move-only values, and references.
- **C++ recompilation boundary**: Public C structs, opaque handles, and exported functions maintain a stable C99 ABI. Because the C++ facade consists of header templates, rebuild all C++ consumers and plugins together when updating headers, and do not pass C++ `Task` objects across modules built with mismatched header revisions.
- **Module trimming and plugin lifetime**: When `HX_OS_ENABLE_THREAD_POOL` is disabled, pool factories return `HX_OS_NOT_SUPPORTED` and clear the capability bit. The `hx_c_platform::cpp` CMake target requires only C++11 and does not force exception flags onto unrelated consumer targets (see [the Result guide](CPP_RESULTS.md) for no-exception CMake settings). Keep custom allocators, task code, destructors, borrowed data, and dynamic plugins loaded until every task, result, and cancellation reference has been released.
