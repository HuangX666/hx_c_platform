# C++11 thread-pool facade

[简体中文](CPP_POOL.zh-CN.md) · [README](../README.md)

`hx/ThreadPool.hpp` provides a header-only C++11 interface over the bounded C
pool. C++ consumers link `hx_c_platform::cpp`; pure C consumers continue to
link `hx_c_platform::hx_c_platform`. There is no second worker scheduler and
no C++ object in the C ABI. Exceptions are required in the C++ consumer;
the library itself remains C99. Coroutines are deferred and are not provided
by this module.

~~~cmake
find_package(hx_c_platform 0.1 CONFIG REQUIRED)
target_link_libraries(my_cpp_app PRIVATE hx_c_platform::cpp)
~~~

The imported interface requests C++11 and carries the C library's link
requirements. The header also compiles without a precompiled header or
umbrella include. C++ types are source interfaces, not a stable binary ABI
between different C++ standard libraries or compilers.

## Ownership and shutdown

`ThreadPool` is deliberately a view over a host-owned `hx_os_thread_pool *`
variable. That variable must outlive every view, and must not be changed
concurrently with pool calls. Copies of a view refer to the same owner.
Destroying a view performs no shutdown, wait or thread detach; the original
owner remains available. The host must eventually close and destroy it.

This explicit ownership allows shutdown to return TIMEOUT or BUSY while
retaining a reachable pool and all work. It avoids hiding an infinite wait
inside a C++ destructor. `Task<T>` and cancellation handles do automatically
release their own references. Tasks can outlive a successfully destroyed
pool; retained task/result allocations can keep its context busy.

~~~cpp
#include <hx/ThreadPool.hpp>

hx_os_thread_pool *owner = NULL; // Keep in the host's long-lived state.
hx_os_thread_pool_options options;
hx_os_thread_pool_options_init(&options);
options.threads = 4;
options.queue_capacity = 128;

auto pool = hx::ThreadPool::create(owner, NULL, &options);
auto answer = pool.submit([](int x) { return x * 2; }, 21);
int value = answer.get(); // 42; task exceptions propagate here.

hx_os_status status = pool.closeUntil(hx_os_deadline_after_ms(2000));
if (status == HX_OS_OK) {
	// Exclude all pool callers before destroying the owner.
	status = pool.destroy();
}
// Handle status. TIMEOUT/BUSY keeps owner intact for a later retry.
~~~

The [standalone example](../examples/thread_pool.cpp) includes error handling
and is executed by installed-consumer tests. Its two-second deadline is an
example policy, not a library-wide default. A service should put `owner` in
its shutdown state, stop new application submissions, request task-specific
cancellation as appropriate, and retry close with a fresh monotonic deadline.
Do not discard that state on TIMEOUT/BUSY. Destruction requires external
exclusion even after successful close; it clears the owner on success.

`closeUntil` permanently rejects new submissions once closing begins.
DRAIN runs accepted queued tasks. CANCEL_PENDING cancels queued work but does
not forcibly interrupt a task already running. Both wait for worker cleanup
and native thread exit. A close call made by a worker in the same pool returns
DEADLOCK. Concurrent close callers can receive BUSY; a preparation still
executing on a submitting thread can also keep close/destroy busy.
No consumer thread is killed and no background reaper is started.

## Submission and results

`submit(callable, args...)` accepts lambdas, function/member pointers,
move-only functors/arguments, `std::ref`, and value/reference/void results.
Stored callables are invoked as lvalues; stored arguments are moved into the
call. A reference wrapper intentionally keeps a borrowed reference.
The returned move-only `Task<T>` supports:

| Method | Contract |
|---|---|
| valid() | Whether this object holds a task reference |
| ready() | Nonblocking completion query; completion includes payload cleanup |
| waitUntil(deadline) | Absolute library monotonic deadline, returns wait status |
| waitFor(milliseconds) | Relative duration; zero/negative means poll |
| wait() | Explicit infinite wait, throws StatusError for wait failure |
| get() | Infinite wait, consumes outcome, returns value or rethrows exception |
| cancel() | Cooperative stop request; no forced interruption |
| cancellationSource() | Independently retained source for another thread |

`HX_OS_DEADLINE_NOW` polls and `HX_OS_DEADLINE_INFINITE` explicitly waits
without a deadline. A successful wait reports completion, not successful
execution: `get()` retrieves the result/exception. A timeout does not consume
the task. An unfinished same-pool wait returns DEADLOCK instead of helping
execute arbitrary queued tasks. `get()` preserves the handle on a wait
failure, and consumes it after a completed result, exception or cancellation.

User exceptions are captured in the worker and rethrown by get; none cross
the C callback boundary. Library errors use `hx::StatusError`, with status
and copied `hx_os_error`. Cancellation of queued work yields CANCELLED.
Dropping a Task releases the observation handle; it does not cancel work.
There is no std::future conversion, shared_future or continuation API yet.

Bound values are copied/moved, not arbitrary pointed-to memory. Referenced
objects must outlive their use by the worker. A returned `T&` must refer to
external storage that survives result consumption, not to arguments/captures
destroyed by worker cleanup. Result and bound-value destructors must be
nonthrowing. Destructors can run on a worker, on the submitting thread during
rollback, or on the thread releasing the last result reference.

## Admission, allocation and reentry

The C `hx_os_thread_pool_submit_prepared` entry reserves a queue admission
before constructing the C++ payload. A full or already closed queue therefore
rejects without copying the callable or allocating the C++ job. Ordinary
C `submit` retains its original rejection/cleanup semantics.

Preparation and cleanup callbacks run outside the pool mutex. If preparation
fails, it releases partial resources; a throwing C++ copy/move is rethrown in
the submitting thread. If close races a successful preparation, the C entry
returns CLOSED and calls cleanup once on the submitting thread. Moved-from
arguments cannot be restored after this race. Successfully accepted payloads
are cleaned exactly once on workers, including skipped cancelled tasks.
The pool keeps an admission claim until rejection cleanup has completed.

Callable storage, result storage and state bookkeeping share one aligned
allocation from the pool context; native task/event allocations are separate.
A view defaults to a 1 MiB inline task-size limit. Use
`hx::ThreadPool view(owner, maxTaskBytes)` to choose another limit.
This bounds the job's `sizeof`, including result storage, not heap graphs
allocated by arbitrary user types or exception objects. Result/exception
retention outside the queue is controlled by how many Task handles the host
keeps. The library does not replace the process C++ allocator.

Callbacks and user destructors should not perform an unbounded wait in
preparation/cleanup. Library close deadlines do not cancel arbitrary user
code or provide hard real-time guarantees. Same-pool waits from workers are
rejected; the library cannot detect every cycle involving external resources.

## Cooperative cancellation across threads

~~~cpp
auto task = pool.submitCancelable([](hx::CancellationToken token) {
	while (!token.stopRequested()) {
		// Perform one bounded unit of work, then check again.
	}
	token.throwIfCancelled();
});
auto stop = task.cancellationSource();
// Move task to its waiting thread; pass a copy of stop to the controller.
stop.requestStop();
~~~

CancellationSource/Token retain native task references and can outlive the
pool. Copy the source before transferring Task ownership to another thread.
Do not call cancel on a Task object concurrently with get/move/destruction of
that same object; use the separate source. Calls through stable independent
sources can safely request stop while the task is running or being waited on.
Assignment/destruction of a source must also exclude its concurrent users.
Cancellation is a request: tasks must check their token or use a native
operation that supports their chosen interruption strategy.

## Storage layout and rebuilding

Internal result state selects member or base storage at compile time. Empty
void storage can use empty-base optimization; suitable result storage can
reuse base tail padding. A short nonempty base before the 8-byte aligned
reference count can instead grow 32-bit objects, so those results remain
members. No packed access, smaller atomic or C++20 feature is required.

The result slot starts empty without throwing. Execution either constructs
a ready result or stores an exception; those outcomes are mutually exclusive.
Both storage forms retain the same owning cleanup and publication contract.
Result alignment, transfer and destruction are tested through the public API.

These internal C++ template layouts can change between source versions.
Rebuild all consumers/plugins using the facade together after an update;
never pass a task instantiated with an old header to code compiled with a
new layout. The public C structures and opaque-handle ABI are unchanged.
The measured differences are in the [validation record](VALIDATION.md).

## Linking and optional features

`HX_OS_ENABLE_THREAD_POOL=OFF` keeps the C ABI but reports NOT_SUPPORTED on
creation; the C++ factory throws StatusError for this status. The capability
query lets callers branch before creation. No C++ compiler is needed when
building only the C library with tests/examples disabled.

Keep allocator callbacks, task functions, C++ destructors, exception types
and their modules loaded until all work, task results and cancellation
references are released. A task reference does not pin a plugin in the OS
loader. Static copies in different DLLs have separate library state; allocate
and release through the corresponding instance and compatible C++ runtime.
General thread TLS and its destructor lifetime rules remain covered by the
[core contracts](CONTRACTS.md).
