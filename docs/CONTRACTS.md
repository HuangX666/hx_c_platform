# Behavior and lifetime contracts

[简体中文](CONTRACTS.zh-CN.md) · [README](../README.md)

**Use this page to answer “what remains valid after an error or timeout?”** Start with the [file, configuration or pool tutorial](USAGE.md); this page collects their shared rules.

After a successful text read, you own the string and the file is already closed. After a pool wait times out, work may still be running, so task data must remain alive. Check status, account for any documented partial progress, then decide whether to release resources or retain them for retry.

## Shared rules

[Variant values](VARIANT.md) follow these ownership/error rules without requiring a global setup. They are independent of the immutable configuration tree and mutable settings. Clear releases the active value while keeping a context lease; destroy releases that lease. Custom descriptors/code/user state are borrowed until all associated objects retire; there is no global registration or automatic plugin pinning.

A status is not a Boolean. OK means the stated operation completed; EOF, NEED_INPUT and OUTPUT_FULL are distinct non-error flow states. Negative values report failure, timeout, cancellation, temporary blocking or capacity limits. Inspect the byte count after partial I/O. An optional caller-owned error record holds the normalized status, native domain/code and secondary cause code. There is no mutable thread-global "last library error" and no logging callback. Native messages are formatted as UTF-8 into caller storage.

Unless explicitly stated otherwise, pointer inputs are borrowed for the call, output objects have one owner, and destruction requires external exclusion of new users. A failed constructor publishes no object. Never release an object with free(), another library instance or another pool. NULL cleanup is accepted where documented; a disabled constructor returns NOT_SUPPORTED.

Worker-backed constructors have one explicit exception: pool, coroutine runtime, directory watch and coroutine HTTP service creation may fail with a non-NULL owner when rollback also fails. The error still describes creation. This owner is already closing and must not accept business work. Keep it and its allocation context alive, then retry the corresponding cleanup below. Ordinary creation failure still returns NULL; no hidden reaper or forced thread termination is used.

| Retained owner | Existing cleanup entry |
| --- | --- |
| Thread pool | `hx_os_thread_pool_shutdown()` |
| Coroutine runtime | `hx_os_coro_runtime_close_until()`, then `hx_os_coro_runtime_destroy()` after successful close |
| Directory watch | `hx_os_watch_close_until()` |
| Coroutine HTTP service | `hx_os_coro_http_close_until()`, before releasing its runtime |

Check ownership even when creation reports failure. A failed watch creation never transfers callback/cleanup ownership, including when a native owner remains. C++ factories return a failed `Result` while preserving the caller's owner variable for the same cleanup; do not call `value()` on that failed result.

Pure status/capability/time/Unicode/atomic functions need no initialization. A NULL context uses an immutable malloc/free policy. Explicit contexts copy allocator callbacks and borrow user state. They cannot change allocator. Create one per consumer when allocator lifetimes differ. Strings retain even an empty context; allocated objects also keep it busy. Context destruction returns BUSY until all allocations and leases retire. It does not race safely with a new allocation: exclude admission before destruction.

Allocator callbacks require fundamental alignment, thread safety and cross-thread freeing. Sizes are size_t; reject checked conversions from wider consumer sizes. The wrapper supplies higher power-of-two alignment and stores the originating allocator. Callback code/user data remain loaded until every allocation is freed and context destruction succeeds. Native APIs and iconv may allocate internally using their own allocators; the context controls this library's allocations, not all OS/backend memory.

## Time and synchronization

All waiting APIs accept absolute monotonic deadlines in nanoseconds, computed with deadline_after_ms(). NOW polls and INFINITE waits without a time limit. sleep_until(INFINITE) is invalid. Large durations saturate to the largest finite deadline. Monotonic time excludes suspend; wall time is Unix epoch milliseconds and can jump. Nanosecond units do not promise nanosecond resolution. Windows uses QueryUnbiasedInterruptTime for this suspend contract. Status-returning clock reads preserve native failures. Convenience reads return UINT64_MAX for monotonic failure and INT64_MIN for wall-clock failure; deadline creation then yields NOW. Finite waits fail closed instead of using an invalid clock value. A 32-bit SDK's time_t remains relevant to dates beyond 2038; select and verify a time64-capable SDK where that is required.

An already available event or completed task may succeed at an expired deadline. Scheduling, a slow callback, mutex reacquisition and native retirement can extend observed return latency. None of these are hard real-time deadlines.

| Facility | Common guarantee |
|---|---|
| Mutex | Nonrecursive by default; explicit recursive option; no fairness |
| Condition | Nonrecursive mutex locked once; notifications not retained |
| Condition wait | Predicate loop required; returns with mutex locked |
| Auto event | Retains at most one signal; one waiter consumes it |
| Manual event | Remains signaled for all waiters until explicit reset |
| Thread join | One joiner; retryable timeout; success includes TLS cleanup |
| Thread destruction | BUSY before successful join; never detaches/kills |
| TLS key | Destructor on normal exit; live values prevent key destruction |
| TLS replace | Does not destroy old value; clear explicitly destroys it |

Condition waits may wake spuriously. All waiters must leave before a mutex, condition or event is destroyed. A timed-out wait does not invalidate its object. Thread user data remains borrowed until native termination, including TLS destructors. A key destructor must not reset that same key. On Windows, FLS supplies exit cleanup and is fiber-local if the host explicitly uses fibers.

Atomics expose aligned 64-bit integer and pointer storage. Initialize before publication; do not directly touch the members concurrently or embed them in packed/under-aligned storage. Load/store/CAS order rules are documented in the header. Storage must be writable even for native RMW-based loads. The implementations may use stronger ordering. GCC/Clang use __atomic; MSVC uses matching Interlocked operations, with a CAS loop on x86 for 64-bit updates. A capability query distinguishes lock-free support. Volatile alone is never used as synchronization. CPU hints provide no ordering guarantee.

## Files, paths and text

Regular files use unbuffered library handles; native/kernel caching remains. read_some reports OK with positive bytes, EOF with zero bytes, or a failure. A zero-length request returns OK and does not discover EOF. write_some can write a prefix. write_all retries progress, reports total bytes on later failure, and does not spin forever on a successful zero-byte write. Callers retry only the remaining suffix when that is appropriate.

sync requests native persistence using fsync/FlushFileBuffers. There is no stdio buffer in this object to flush. Hardware power-loss guarantees and directory-entry persistence are outside this common promise. File calls may block and have no artificial cancellation/deadline guarantee. Serialize handle position operations. Append applies at the native write level; a multi-write record is not an atomic transaction. close consumes the pointer even on error; do not retry a raw close against a possibly reused native descriptor.

Paths are NUL-terminated UTF-8. No normalization, sandbox, locale conversion, case-folding or symlink policy is inferred. POSIX arbitrary non-UTF-8 names and Windows unpaired-surrogate names cannot be represented. Windows converts to UTF-16 and handles long absolute drive/UNC paths. Symlink-following is an explicit stat argument; special reparse points retain native behavior. Rename is same-filesystem, without copy/delete fallback. Unsupported atomic no-replace modes fail. Directory deletion requires emptiness. Recursive directory creation can leave successfully created parents on later failure.

Directory next copies a NUL-terminated name; required includes the NUL. OUTPUT_FULL retains the current entry for retry. Invalid-encoding entries are consumed and reported, allowing explicit continuation. EOF ends enumeration.

Strings always own valid UTF-8. Mutation requires serialization. A view borrows; do not hold it across owner destruction/mutation. Copy retains the destination context; move transfers the source context and leaves an empty default-context source. Never shallow-copy the struct. Indexes are bytes at codepoint boundaries. No grapheme editing, Unicode case folding, normalization or locale sorting.

Encoding lengths are bytes even for UTF-16/32. No BOM or NUL is inserted, removed or guessed. No lossy conversion occurs under STRICT. REPLACE reports replacement counts, using U+FFFD or '?' when needed. Input/output cannot overlap. On OUTPUT_FULL, retry unconsumed input; on NEED_INPUT, retain and resubmit the incomplete suffix. final_input distinguishes truncated final input. Reset after final success or a strict error. error_offset is the stream byte offset; UINT64_MAX means no conversion error. A converter is not concurrently shared. The legacy adapter supports the declared stateless encoding set, not arbitrary iconv names or stateful ISO-2022 encodings.

## Pools and asynchronous work

Memory pools grow by default. maximum_bytes=0 permits growth; a finite maximum counts pool-controlled upstream blocks, allocation wrappers and metadata. It excludes allocator-private bookkeeping and native kernel state. Pending allocation/free callbacks continue to consume their reserved budget. cached_bytes limits idle whole-block caching separately. There is no hidden upstream bypass. Free uses the original pool; reallocation preserves the original on failure. An in-slot resize may remain in place. Outstanding blocks make destroy BUSY. Object pools bound the number of live objects; arenas require external serialization and reset invalidates all of their allocations.

Thread pools have fixed workers and a bounded waiting queue (defaults: 2 and 256). Queued plus in-flight submissions consume queue capacity. Running jobs are additionally bounded by worker count. Rejection transfers no user data. Accepted jobs call cleanup once on a worker, including queued cancellation. A running task must cooperate with its cancellation flag. Waiting for pending work on the same worker pool returns DEADLOCK. Wait success includes cleanup; work status is separate. Task handles are reference-counted and may outlive a closed pool. Close chooses drain or cancel-pending, shares the caller's deadline across joins, retains resources on timeout, and may be retried.

No library background thread is created by core functions or event-loop creation. Thread pools create only the requested bounded workers. Async DNS lazily creates the loop's configured bounded resolver pool. There is no process termination, forced thread termination or unmanaged detach.

## Networking and event loops

Network instances balance native startup/cleanup and retain sockets/loops. Basic sockets are nonblocking and noninheritable. Direct socket use is serialized by the caller. TCP receive distinguishes EOF from an empty requested buffer. UDP zero-length datagrams are data. Truncated datagrams report LIMIT plus the copied byte count; the remainder is discarded. IPv6 scope_id is explicit; parse/format handle the address literal, while port/scope remain separate fields. DNS takes ASCII/Punycode hostnames, with at most 256 distinct addresses.

Windows creates sockets with WSA_FLAG_NO_HANDLE_INHERIT. Linux/Android set close-on-exec during socket creation and use accept4 with SOCK_CLOEXEC | SOCK_NONBLOCK; an unavailable accept4 reports an error instead of silently using a racy fallback. POSIX close-on-exec prevents retention across exec, not the temporary descriptor copy made by fork. On macOS, creation/acceptance still uses a separate fcntl step: the host must coordinate concurrent socket creation and child-process startup when inheritance isolation is required.

Direct connect_until timeout leaves establishment in progress; close to abandon. send_until reports a sent prefix. Data already delivered is never rolled back. Readiness is a hint and subsequent nonblocking I/O may still return WOULD_BLOCK.

A client can disconnect before the server accepts it. Known recoverable accept errors return WOULD_BLOCK with their native details; loop ACCEPT operations keep waiting for another connection, subject to the original deadline and cancellation. They do not fail the whole IPC/TLS listener merely because one incoming connection was aborted. Invalid listeners and resource exhaustion remain errors; the library does not retry every failure indefinitely.

Always inspect the owner pointer after socket close. On Windows, a native close failure preserves the socket, its network reference and any loop attachment for retry. In particular, nonzero SO_LINGER on a nonblocking socket can make close return WOULD_BLOCK; do not discard the owner or destroy its network/loop. POSIX close is issued once: an error may still consume the descriptor and clear the owner, so do not retry the old native descriptor.

The creating thread owns loop driving, I/O submission, attached sockets and destruction, and must stay alive until loop destruction. POST submission, request cancellation/reference operations and waiting are thread-safe. One read-side and one write-side request may coexist per socket; CONNECT occupies both. Do not mix direct I/O with pending loop I/O. A socket stays attached to one loop until close; closing a busy socket fails.

Operations copy their descriptions, addresses and DNS names. Sending copies bytes by default. SEND_BORROW and receive buffers remain valid until completion; user is borrowed through callback return. Accepted operations call the callback once on the driving thread, outside internal locks, never inline in submit. Rejection calls no callback. Defaults are 1,024 requests and 4 MiB pending buffer/name bytes, plus bounded per-request metadata; DNS uses 2 workers and 16 queue entries. Request handles retained after completion still occupy caller-controlled memory but are not pending-work budget.

Callbacks can submit new work or close idle sockets. Recursive driving/closing and owner-thread waits for unfinished requests return DEADLOCK. Callback code must return normally: no C++ exceptions, longjmp or library-unload during calls. Completion data is borrowed until request release; accepted sockets/address lists must be taken to transfer ownership. Untaken results are freed with the request. Request wait success includes callback return; waiting does not drive.

run_until drains to idle; poll_until performs a batch and may wait for work. A run/wait deadline does not cancel requests. An operation deadline requests cancellation and may retire later. IOCP cancellation waits for the native completion packet before freeing OVERLAPPED/buffers. POSIX readiness removal does not undo a connection attempt. Close a socket after failed/cancelled CONNECT. DNS may remain inside getaddrinfo until the native resolver finishes; deadline/close can time out without freeing its job or adding replacement threads.

Close stops new admission, requests cancellation and drives retirement. It can invoke callbacks and retains everything live on timeout. Close attached sockets after their requests retire, then destroy the closed loop. Completed request handles can outlive the loop. There is no fixed two-second library shutdown policy.

On Windows, failure to post an IOCP wake is reported with its native error instead of permanently suppressing future wakes. Native waits check for this failure at intervals of at most 250 ms; normal posts still wake immediately, and internal checks do not create public timeouts. This is not a hard response-time guarantee. A poll error does not release accepted requests or their buffers: continue driving them or call close. If closing the internal DNS worker pool fails, the loop stays owned and unfinished; retry `hx_os_loop_close_until()` before destruction.

## Linking and unloading

Static and shared consumers must release objects through the creating library instance. Do not exchange owning handles between private static copies in different plugins, or mix incompatible CRT/allocator tables. CMake target usage keeps build/consumer macros and transitive dependencies consistent.

Before unloading a plugin/library: stop admission, retire work and callbacks, join workers including native TLS cleanup, clear/destroy live TLS keys, release objects and destroy contexts. A timeout forbids unloading the code/user state still referenced. Forked children must not reuse existing library synchronization, pools or loops; exec or create a separately defined post-fork environment. No function is promised async-signal-safe.
