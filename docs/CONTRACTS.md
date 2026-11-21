# Behavior, Ownership, and Lifetime Contracts

[简体中文](CONTRACTS.zh-CN.md) · [Back to README](../README.md) · [Getting-Started Guide](USAGE.md)

Use this reference whenever you need to know **what remains valid after an error or timeout, which handles must be retained for retry, and when memory can be safely freed**. If you are new to `hx_c_platform`, start with the [Getting-Started Tutorials](USAGE.md) first.

Every API in the library follows one core rule: **inspect the returned `hx_os_status` first, account for any partial byte count or progress, and only free resources once background workers or asynchronous I/O have completely stopped using them.**

---

## 1. Status Codes, Ownership, and Allocation Contexts (`hx_os_context`)

### 1.1 Status Code Categories (`hx_os_status` Is Not a Boolean)

- **Success and Non-Error Flow States (`>= 0`)**:
  - `HX_OS_OK` (`0`): The operation completed as requested.
  - `HX_OS_EOF` (`1`): Reached the end of a file, stream, or directory listing.
  - `HX_OS_NEED_INPUT` (`2`): A streaming decoder or parser requires more input bytes to complete a unit.
  - `HX_OS_OUTPUT_FULL` (`3`): The destination buffer is too small; grow the buffer and retry.
- **Errors, Timeouts, and Blocking States (`< 0`)**:
  - Negative codes distinguish invalid arguments (`HX_OS_INVALID`), out-of-memory (`HX_OS_NO_MEMORY`), timeouts (`HX_OS_TIMEOUT`), cancellation (`HX_OS_CANCELLED`), busy resources (`HX_OS_BUSY`), non-blocking readiness (`HX_OS_WOULD_BLOCK`), and capacity limits (`HX_OS_LIMIT`).
  - Always check the transferred byte count out-parameter after partial I/O operations.
- **Standalone Error Records (`hx_os_error`)**:
  - The library keeps no mutable thread-global "last error" variable and invokes no global logging callbacks. Callers can pass an optional `hx_os_error *` to capture the normalized `hx_os_status`, the native error domain (e.g., `HX_OS_ERROR_WIN32`, `HX_OS_ERROR_ERRNO`, `HX_OS_ERROR_OPENSSL`), the raw OS code, and any secondary cause code, then format a human-readable UTF-8 message with `hx_os_error_message()`.

### 1.2 Handle Ownership and the Non-NULL Cleanup Exception

Unless a header explicitly states otherwise:
- **Input pointers** are borrowed only for the duration of the call.
- **Output objects** have a single owner and must be released through the matching destroy/release function of the same library instance (never pass library handles to `free()` or across isolated static-library copies in different plugins). Callers must exclude new concurrent users before destroying an object.
- **Disabled modules and `NULL` cleanup**: Constructors for build-disabled modules return `HX_OS_NOT_SUPPORTED`. Cleanup functions that document `NULL` support treat `NULL` as a safe no-op.

> [!WARNING]
> **Six Constructors That May Retain a Non-`NULL` `owner` on Failure**:
> Ordinary constructors always set their output pointer to `NULL` on failure. However, the six multi-resource objects below manage worker threads or kernel handles where an **intermediate creation failure combined with an incomplete rollback** cannot safely free the partially created state immediately.
>
> In that case, the constructor returns a failure status while leaving a **non-`NULL` `owner` pointer** for cleanup. Do **not** submit work to that `owner`; keep it and its `hx_os_context` alive and retry cleanup using the matching entry point below:
>
> | Object with Retained `owner` | Cleanup Function to Retry |
> | --- | --- |
> | **Thread Pool** | `hx_os_thread_pool_shutdown()` (C++: `hx::ThreadPool(owner).shutdownUntil(deadline)`) |
> | **Coroutine Runtime** | `hx_os_coro_runtime_close_until()`, followed by `hx_os_coro_runtime_destroy()` once closed |
> | **Directory Watch** | `hx_os_watch_close_until()` (a failed watch creation never takes ownership of caller callbacks/cleanup) |
> | **Coroutine HTTP Service** | `hx_os_coro_http_close_until()`, before destroying the underlying coroutine runtime |
> | **Process Output Capture** | `hx_os_process_capture_close_until()`, called on the thread that created the capture |
> | **Named Shared Memory** | `hx_os_shared_memory_close()` (`data` remains `NULL` and `size` is `0` when open/create fails) |

### 1.3 Allocator Contexts (`hx_os_context`) and `Variant`

- **Default Context (`NULL`)**: Passing `NULL` uses the default `malloc` / `free` allocator. Status helpers, capability queries, clocks, Unicode helpers, and atomics require no context setup.
- **Custom Allocator Contexts**: `hx_os_context_create()` copies the `hx_os_allocator` callback table and borrows `user` state for the lifetime of the context. Callbacks must be thread-safe, provide fundamental alignment, and support cross-thread deallocation; the library wrapper provides higher power-of-two alignments and records the originating allocator per block.
- **Context Destruction**: `hx_os_context_destroy()` returns `HX_OS_BUSY` while any allocation or context lease remains active (note that even an empty `hx_string` initialized with a non-`NULL` context holds a lease until `hx_string_destroy()`). Stop new allocations and destroy all owned objects before destroying the context.
- **[Variant Values](VARIANT.md)**: Follow the same ownership model. Calling `clear` releases the active value while keeping the context lease; calling `destroy` releases the context lease as well. Custom type descriptors and callbacks must remain valid until all associated `Variant` instances are destroyed.

---

## 2. Clocks, Deadlines, and Thread Synchronization

### 2.1 Monotonic Deadlines

- **Absolute Monotonic Deadlines**: All timed wait APIs accept an absolute monotonic timestamp in nanoseconds (`hx_os_deadline`), typically created via `hx_os_deadline_after_ms(milliseconds)`.
  - `HX_OS_DEADLINE_NOW` (`0`): Poll once without blocking.
  - `HX_OS_DEADLINE_INFINITE` (`UINT64_MAX`): Wait indefinitely (note that `hx_os_sleep_until(HX_OS_DEADLINE_INFINITE)` is invalid and returns `HX_OS_INVALID`).
- **Clock Semantics**: Monotonic time excludes system suspend time (using `QueryUnbiasedInterruptTime` on Windows); wall-clock time is Unix epoch milliseconds and may jump across clock adjustments. Nanosecond units provide a uniform representation and do not imply hard real-time nanosecond wakeup precision.
- **Fail-Closed Clock Reads**: Status-returning clock reads (`hx_os_monotonic_read()` / `hx_os_wall_time_read()`) report native clock failures; convenience reads return `UINT64_MAX` or `INT64_MIN` on failure, causing `hx_os_deadline_after_ms()` to yield `HX_OS_DEADLINE_NOW` so finite waits fail closed instead of blocking on a broken clock.

### 2.2 Synchronization Primitives and Thread Lifetimes

| Facility | Core Contract |
| --- | --- |
| **Mutex** | Non-recursive by default (recursive mode is opt-in); wakeup order is not guaranteed to be fair |
| **Condition Variable** | Must be paired with a non-recursive mutex locked once; signals emitted with no current waiters are not queued |
| **Condition Wait** | Spurious wakeups are permitted—always test your predicate in a `while` loop; returns with the mutex re-locked |
| **Auto-Reset Event** | Retains at most one signal and resets automatically after waking a single waiter |
| **Manual-Reset Event** | Remains signaled for all current and future waiters until explicitly reset |
| **Thread Join** | Supports a single concurrent joiner; a timed-out join (`HX_OS_TIMEOUT`) can be retried; join success includes TLS destructor completion |
| **Thread Destroy** | Returns `HX_OS_BUSY` until `join` succeeds; never detaches or forcefully terminates threads |
| **TLS Key** | Runs its destructor on normal thread exit; destroying a key while any thread holds a non-`NULL` value returns `HX_OS_BUSY` |
| **TLS Replace vs. Clear** | Overwriting a TLS slot via `set` does **not** run the destructor on the old value; `clear` or thread exit runs the destructor |

> [!NOTE]
> - Ensure all waiting threads have returned before destroying a mutex, condition variable, or event. A timed-out wait never corrupts the synchronization primitive.
> - On Windows, Fiber-Local Storage (FLS) drives TLS cleanup on thread exit; if the host application explicitly switches Win32 fibers, values behave as fiber-local.
> - **Atomics**: Expose aligned 64-bit integer and pointer storage. Initialize atomic objects before publishing them to other threads, never place them in packed or under-aligned structs, and never access their struct fields directly during concurrent use.

---

## 3. Files, Paths, and Text Encoding

### 3.1 File I/O and Persistence

- **Unbuffered Handles**: `hx_os_file` wraps native OS file descriptors/handles directly without adding a user-space `stdio` buffer (OS kernel page caching still applies).
- **Read and Write Semantics**:
  - `read_some`: Returns `HX_OS_OK` with a positive byte count when data is read, or `HX_OS_EOF` with `0` bytes at end-of-file. A zero-length read request returns `HX_OS_OK` immediately and does not test for `EOF`.
  - `write_some` / `write_all`: `write_some` may write only a prefix; `write_all` loops until all bytes are written or an error occurs, reporting the total bytes written so far (on retry, pass only the unwritten suffix).
- **Sync and Close**: `sync` requests OS persistence via `fsync` or `FlushFileBuffers`. `close` consumes and clears the caller's handle pointer even if the underlying close reports an I/O error, preventing double-close on a potentially reused OS descriptor.

### 3.2 UTF-8 Paths, Directory Iteration, and Strings

- **UTF-8 Paths**: All path arguments are NUL-terminated UTF-8 strings (converted internally to UTF-16 with long-path support on Windows). The library performs no implicit Unicode normalization, case folding, or cross-filesystem copy fallback on `rename` (`HX_OS_CROSS_DEVICE`).
- **Directory Enumeration**: Copies NUL-terminated entry names (`required` includes the trailing NUL). On `HX_OS_OUTPUT_FULL`, the current entry is retained so you can retry with a larger buffer; `HX_OS_EOF` marks the end of the directory.
- **`hx_string` and `hx_string_view`**: `hx_string` always contains valid UTF-8 and must not be shallow-copied. `hx_string_view` borrows memory from its owner and is invalidated when the owner is modified or destroyed.

### 3.3 Streaming Text Encoding Conversion (`hx_os_text`)

- Input and output sizes are **always measured in bytes** (even for UTF-16 and UTF-32), and no BOM is implicitly added, stripped, or guessed.
- `HX_OS_TEXT_STRICT` stops on invalid sequences; `HX_OS_TEXT_REPLACE` substitutes `U+FFFD` (or `'?'` when unrepresentable) and counts replacements.
- On `HX_OS_OUTPUT_FULL`, grow the output buffer and resubmit the unconsumed input slice. On `HX_OS_NEED_INPUT`, retain the incomplete trailing bytes and prepend them to the next input chunk.

---

## 4. Memory Pools and Thread Pools

- **Memory Pools**: Grow on demand when `maximum_bytes = 0`. When a finite `maximum_bytes` budget is set, it bounds all pool-managed upstream blocks, headers, and metadata, while `cached_bytes` separately caps idle cached blocks. Destroying a pool while allocations remain outstanding returns `HX_OS_BUSY`. Arenas require caller serialization, and `reset` invalidates all prior arena allocations at once. A zero-length pool reallocation clears the pointer, including when it starts empty. Reported reserved bytes and their peak count successful upstream allocations. In-flight reservations still participate in the budget.
- **Thread Pools**: Use a fixed worker count and bounded queue capacity (defaults: 2 workers, 256 queued tasks). A rejected submission leaves caller data untouched; an accepted task guarantees its `cleanup` callback runs exactly once on a worker thread (even if the task is cancelled while queued).
- **Deadlock Detection**: Waiting on a task or shutting down a pool from one of that pool's own worker threads returns `HX_OS_DEADLOCK` immediately.

---

## 5. Networking and Event Loops (`hx_os_loop`)

### 5.1 Socket Options and Close Semantics

- **Non-Blocking and Non-Inheritable**: Sockets are created non-blocking and non-inheritable (`WSA_FLAG_NO_HANDLE_INHERIT` on Windows; `SOCK_CLOEXEC | SOCK_NONBLOCK` and `accept4` on Linux/Android).
- **Inspect Both Status and `owner` After Closing a Socket**:
  - **Windows**: When a non-blocking socket has a non-zero `SO_LINGER` timeout, `closesocket` can return `HX_OS_WOULD_BLOCK`. The library preserves the socket handle, network reference, and loop attachment so you can retry closing later.
  - **POSIX (Linux / macOS / Android)**: `close()` is invoked once; even if an error is reported, the descriptor is consumed and `owner` is cleared to `NULL`.

### 5.2 Event Loop Thread Ownership and Shutdown Order

- **Single Driving Thread**: The thread that creates an `hx_os_loop` owns loop driving (`run_until` / `poll_until`), I/O submission, socket attachment, and loop destruction. Cross-thread `POST` submissions, request cancellation, reference counting, and request waits (`wait`) are thread-safe.
- **Completion Callbacks**: Accepted requests invoke their callback exactly once on the loop-driving thread without holding internal locks and never synchronously inside `submit`. Callbacks must return normally (no C++ exceptions, `longjmp`, or library unloading inside callbacks).
- **Shutdown Order**: `hx_os_loop_close_until()` stops new request admission, requests cancellation of in-flight work, and drives callbacks until idle (retaining all live resources if `HX_OS_TIMEOUT` occurs). **Wait for all requests to finish, close all attached sockets, and then destroy the closed loop with `hx_os_loop_destroy()`.**

---

## 6. Dynamic Linking and Safe Plugin Unloading Checklist

Always release objects through the same `hx_c_platform` binary instance that created them. Before unloading a plugin DLL / `.so` that uses `hx_c_platform`:

1. **Stop admitting new work** to thread pools, event loops, watches, and signal handlers.
2. **Wait for all background tasks, timers, and async I/O callbacks to complete** (if any wait returns `HX_OS_TIMEOUT`, plugin code or memory is still in use—**do not unload the plugin**).
3. **Join all worker threads** created by the plugin (including TLS destructor execution) and clear/destroy all plugin TLS keys.
4. **Release all owned objects and destroy the plugin's `hx_os_context`** (verifying `HX_OS_OK` rather than `HX_OS_BUSY`).
5. **`fork()` and Async Signals**: On POSIX systems, a `fork()` child process must not reuse synchronization primitives, thread pools, or event loops inherited from the parent. No API in this library is async-signal-safe.
