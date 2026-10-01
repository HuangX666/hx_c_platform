# C++11 thread-pool facade

[简体中文](CPP_POOL.zh-CN.md) · [README](../README.md) · [Result contracts](CPP_RESULTS.md)

`hx/ThreadPool.hpp` provides a header-only C++11 interface over the bounded C pool. Link `hx_c_platform::cpp`. The scheduler and C ABI remain C99. The facade uses explicit results and supports builds with exceptions disabled. Coroutines remain deferred.

## Ownership and shutdown

`ThreadPool` is a view over a host-owned `hx_os_thread_pool *` variable. That variable must outlive every view and must not change concurrently with pool calls. Copies of a view refer to the same owner. Destroying a view performs no shutdown, infinite wait or detach. The host must eventually close and destroy the native owner.

`Task<T>` and cancellation sources automatically release their own references. A task may outlive successful pool destruction; retained results may keep the allocation context busy. A view never hides the owner required for retry after TIMEOUT or BUSY.

The [complete example](../examples/thread_pool.cpp) checks creation, submission, result and shutdown separately. Its two-second deadline is consumer policy. A service keeps the native owner in long-lived shutdown state, stops new submissions and retries with a fresh monotonic deadline. `shutdownUntil()` combines close and destroy, clearing the owner only on success. Exclude new callers and simultaneous final destruction.

`closeUntil()` permanently rejects new submissions. DRAIN runs accepted queued tasks; CANCEL_PENDING skips queued work without forcibly interrupting work already running. Both wait for cleanup and native thread exit. Closing from a worker in the same pool returns DEADLOCK. Concurrent close or in-progress preparation can return BUSY. No reaper thread or forced termination is introduced.

## Submission and results

`submit(callable, args...)` returns `Result<Task<T>>`. It accepts functions, member pointers, lambdas, move-only captures/arguments and `std::ref`. Stored functions run as lvalues; stored arguments move into the invocation. Function objects must declare their invocation `noexcept`. Capture/argument construction and destruction must be non-failing. C++11 function/member pointers cannot encode that guarantee in the type, so their implementations must honor the same contract.

| Callable result | Submitted result | Retrieved result |
| --- | --- | --- |
| T | Result<Task<T>> | Result<T> |
| Result<T> | Result<Task<T>> | Result<T>, preserving native errors and partial values |
| void or Result<void> | Result<Task<void>> | Result<void> |
| T& | Result<Task<T&>> | Result<T&>, borrowing external storage |

A task returning an ordinary status integer produces `Task<hx_os_status>`: that integer is its value. Return `Result<void>` when it should represent success or failure.

| Method | Contract |
| --- | --- |
| valid() | Holds a native task reference |
| ready() | Nonblocking completion query, including capture cleanup |
| waitUntil(deadline) | Result<void>; absolute library monotonic deadline |
| waitFor(milliseconds) | Result<void>; zero/negative duration polls |
| wait() | Result<void>; explicit infinite waiting |
| getUntil(deadline) | Result<T>; retains task on wait failure, consumes completed outcome |
| get() | Result<T>; explicit infinite waiting |
| cancel() | Cooperative stop request |
| cancellationSource() | Independently retained source for a controller thread |

Successful waiting reports completion, not successful work. Inspect `getUntil()` or `get()` for the outcome. Unfinished same-pool waits return DEADLOCK. Timeout preserves the task. Completed failure and queued cancellation consume the handle just like success. Dropping a Task releases observation without cancelling work.

`Result<T>` stores its value inline and performs no allocation. Read `value()` only after checking `ok()` or, for documented partial progress, `hasValue()`. `get()` returns a nullable pointer. A returned reference must outlive consumption and must not refer to captures/arguments destroyed during worker cleanup.

## Cancellation

This function accepts a host-owned pool view and returns a separate controller handle. Keep that handle on the controller thread while the task executes independently.

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

`CancellationToken::check()` returns CANCELLED after a stop request. Sources/tokens retain native references and may outlive the pool. Copy a source before moving the Task to a waiting thread. Do not call methods on the same Task concurrently with its move, consumption or destruction. Independent stable sources may request stop concurrently with execution/waiting. Cancellation cannot interrupt arbitrary blocking user code.

## Admission, allocation and cleanup

Prepared submission reserves queue capacity before constructing a C++ job. Full/closed queues reject without copying the callable or allocating its C++ job. Preparation runs outside the pool mutex. Allocation failure returns NO_MEMORY and releases partial resources. Copy/move of captures must be non-failing; allocate fallible user state before submission and check it explicitly.

If close races successful preparation, submission returns CLOSED and cleanup runs once on the submitting thread. Moved-from inputs cannot be restored after this race. Accepted captures are retired exactly once on workers, including cancelled queued tasks. An admission claim remains held until rejected preparation cleanup completes.

Callable, result and reference state share one aligned context allocation; native task/event allocations are separate. Default inline job limit is 1 MiB. `ThreadPool(owner, maxTaskBytes)` selects another bound. This limits the job object size, not arbitrary user-owned heap graphs. Retaining Task/result handles can retain memory after the queue drains. The library does not replace the global C++ allocator.

Capture/result destructors may run on a worker, on a rejecting submission thread, or on the thread releasing the final reference. They must return normally and avoid unbounded work. Library deadlines cannot preempt them. User callbacks must not unwind or jump across C frames.

## Alignment, rebuilding and trimming

Inline result/capture slots use C++11 `alignas`. The job is allocated with its actual alignment through the context. Tests cover result alignment from 16 through 128 bytes, move-only values, references, rollback and destruction. Layout reduction alone does not establish a performance improvement.

This migration changes C++ source signatures and template layouts. Rebuild all consumers/plugins using these headers together. Do not exchange old and new C++ task objects across module boundaries. Public C structures, opaque handles and exports remain unchanged.

With THREAD_POOL disabled, factories return NOT_SUPPORTED and the capability bit is clear. The C library alone needs no C++ compiler with tests/examples disabled. The imported C++ target requests C++11 without forcing exception flags on unrelated consumer targets. Explicit flags are in [the Result guide](CPP_RESULTS.md).

Keep allocator code, task functions, destructors, referenced data and their modules alive until every task, result and cancellation reference is released. Task references do not pin plugins in the OS loader. Separate static copies maintain separate state; release through the instance that created the resource.
