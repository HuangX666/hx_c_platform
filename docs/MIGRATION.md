# `hx_c_log` Platform Seam Migration Guide

[简体中文](MIGRATION.zh-CN.md) · [Back to README](../README.md)

This document outlines the architectural mapping and staged integration plan for migrating the private platform abstraction layer of the sibling `hx_c_log` project onto `hx_c_platform`.

---

## 1. Background and Decoupling Goals

In earlier versions of `hx_c_log`, the private platform header included `hx_log.h` directly—coupling low-level file, thread, and socket code to logger sink options, crash-handler configuration, logger allocator helpers, and logger-specific thread-exit TLS cleanup.

`hx_c_platform` extracts these general-purpose OS primitives into a standalone, logger-independent foundation library:
- Provides opaque handles, monotonic nanosecond deadlines (`hx_os_deadline`), explicit memory-order atomics, automatic UTF-8 to UTF-16 path conversion on Windows, partial-write progress reporting, and native thread-exit TLS destructors.
- Separates error reporting (`hx_os_status` and `hx_os_error`) and custom memory allocation (`hx_os_context`) from any specific consumer library.

---

## 2. Thin Adapter Mapping Table

When bridging `hx_c_log`'s internal platform seam to `hx_c_platform`, map each private operation as follows:

| Existing `hx_c_log` Private Operation | `hx_c_platform` API & Adapter Responsibility |
| --- | --- |
| `hx_atomic_load/acquire/store/release/add/cas` | `hx_os_atomic_*` operations with explicit memory orders |
| `hx_atomic_ptr_*` / `hx_cpu_relax` | Pointer atomics and `hx_os_cpu_relax()` |
| `hx_mutex_*` | Opaque `hx_os_mutex` (enable recursive mode where required by existing callers) |
| `hx_event_create/notify/wait` | Auto-reset `hx_os_event` with deadline conversion |
| `hx_thread_start/join/is_current` | `hx_os_thread` wrapper with separate `join` and `destroy` calls |
| `hx_mono_ms` / `hx_now_ms` / `hx_sleep_ms` | Monotonic nanoseconds, wall-clock milliseconds, and `hx_os_sleep_until()` |
| `hx_file_open` / `open_read` / `read_some` | `hx_os_file` open flags with separated `hx_os_status` and byte count |
| `hx_file_write_progress` | `hx_os_file_write_all()` reporting accumulated `size_t` byte progress |
| `hx_file_flush(durable)` | Flush the logger's user-space buffer first, then call `hx_os_file_sync()` when durability is requested |
| `hx_file_size` / `move` / `remove` / `stat` | Typed file info queries and unified `hx_os_error` diagnostics |
| `hx_make_directories` / `directory_scan` | `hx_os_file` directory APIs while keeping rotation/retention policy in `hx_c_log` |
| `hx_native_path` / `hx_wide_utf8` | UTF-8 path APIs and explicit `hx_os_text` encoding conversion |
| `hx_tls_get/set` | Logger-owned `hx_os_tls_key` with a logger-supplied destructor callback |
| `hx_alloc` / `hx_free` | Per-logger `hx_os_context` wrapping the caller's custom allocator |
| `hx_network_*` | Core sockets and event loop underneath the logger's framing, retry, and sink backpressure logic |
| `hx_platform_capabilities` | Map only the capability bits enabled in the linked `hx_c_platform` binary |

---

## 3. Key Semantic Differences to Watch During Migration

Check the following behavioral boundaries when adapting each primitive:

1. **Event Timeout vs. Ready-Signal Consumption**:
   The legacy event `wait` rejected an already expired deadline before checking whether the event was signaled; `hx_c_platform` events consume an already available signal even when called with `HX_OS_DEADLINE_NOW` or an expired deadline. Preserve the logger's expected external behavior in the seam adapter.
2. **Separate Thread `join` and `destroy`**:
   After `hx_os_thread_join_until()` succeeds, call `hx_os_thread_destroy()` to free the thread handle.
3. **32-Bit Size Bounds Checking**:
   On 32-bit targets, validate or chunk any `uint64_t` byte length exceeding `SIZE_MAX` before passing it to a `size_t` API—never truncate silently.
4. **Do Not Mix Clock Epochs**:
   Migrate deadline creation and waiting together to `hx_os_deadline`, or convert remaining relative durations at the adapter boundary—do not simply multiply legacy millisecond timestamps by `1,000,000`.
5. **Keep Logger-Specific Mechanics in `hx_c_log`**:
   Date caching, locale-independent fast numeric formatting, console/syslog/EventLog sinks, the ordered single-consumer async log queue, crash-handler installation, and backtrace ring export remain in `hx_c_log` (do not replace the logger's ordered async output queue with a general multi-worker thread pool). Per-EXE Windows WER LocalDumps registry setup is available as an explicit opt-in helper via [WER LocalDumps](LOCAL_DUMPS.md).

---

## 4. Staged Rollout and Verification Checklist

1. **Freeze the Baseline**: Record the current `hx_c_log` public ABI, functional test suite results, and baseline throughput/latency benchmarks on Windows and WSL2.
2. **Add the Adapter Target**: Link `hx_c_platform` behind `hx_c_log`'s private platform seam while keeping `hx_log.h` and its public ABI unchanged.
3. **Migrate Primitives Incrementally**: Migrate status/clock/path helpers first, followed by file I/O, synchronization primitives, thread lifetimes, and TLS, and finally network sockets—keeping each step independently testable and reversible.
4. **Preserve Shutdown Deadlines**: Pass `hx_c_log`'s default 2-second shared shutdown budget into `hx_c_platform` as an `hx_os_deadline`, retaining active handles and user data if a shutdown times out (`HX_OS_TIMEOUT`).
5. **Verify Packaging and Performance**: Confirm that single-producer and 8-producer durable-flush throughput and latency percentiles meet baseline targets, and verify both static (`find_dependency(hx_c_platform)`) and shared-library consumer installations.
