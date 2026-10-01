# Future hx_c_log migration

[简体中文](MIGRATION.zh-CN.md) · [README](../README.md)

This is a plan only. The neighboring hx_c_log source was read, not changed. Its private platform header includes hx_log.h and accepts sink/crash options, calls core allocation/error helpers and delegates TLS cleanup to the logger. Moving those files unchanged would preserve the coupling.

Reusable ideas include the opaque platform seam, monotonic deadlines, matching atomic storage/operations, UTF-16 path handling, partial-write progress, native thread-exit cleanup and explicit platform capability switches. The new library implements independent errors, contexts and ownership. The OBS-derived warning CMake module was reused with its original MIT notice.

## Mapping through a thin adapter

| Existing private operation | Foundation operation / adapter responsibility |
|---|---|
| hx_atomic_load/acquire/store/release/add/cas | hx_os_atomic_* with explicit orders |
| hx_atomic_ptr_* / hx_cpu_relax | Pointer atomics / CPU hint |
| hx_mutex_* | Opaque mutex; preserve the old recursion requirement |
| hx_event_create/notify/wait | Auto-reset retained event and deadline conversion |
| hx_thread_start/join/is_current | Thread wrapper, native join, separate destroy |
| hx_mono_ms / hx_now_ms / hx_sleep_ms | Monotonic ns / wall ms / sleep deadline |
| hx_file_open / open_read / read_some | File flags and separate status/byte count |
| hx_file_write_progress | write_all and accumulated size_t progress |
| hx_file_flush(durable) | Flush consumer buffer, then sync only when requested |
| hx_file_size / move / remove / stat | Typed results and unified/native errors |
| hx_make_directories / directory_scan | Directory APIs; retain scan policy above |
| hx_native_path / hx_wide_utf8 | UTF-8 path seam / explicit text conversion |
| hx_tls_get/set | Consumer-owned key and consumer-owned payload destructor |
| hx_alloc / hx_free | Consumer-local context allocator adapter |
| hx_network_* | Sockets/loop below existing framing, retry and sink policy |
| hx_platform_capabilities | Map only actually enabled foundation capabilities |

Audit differences before changing each adapter. Old event wait rejects an already expired deadline before consuming a signal; the new event NOW can consume an available signal. Preserve the logger's old observable behavior. Join and object release are separate. Split sizes larger than SIZE_MAX on a 32-bit consumer rather than silently truncating uint64_t arguments.

Never mix absolute deadlines from different epochs/clock semantics. Change clock construction and waiting together, or translate the remaining duration at the seam. Do not merely multiply an old millisecond timestamp by a million. No native file buffer is added here; hx_c_log retains its own buffering, ordered output and rotation/persistence policy.

Date caches, locale-stable numeric formatting, console/system sinks, regex behavior, fork hooks, crash-handler installation, LocalDumps registry settings, history-ring export and termination policy stay in the consumer or a future explicit extension. Native stack capture and logger history are distinct. This version does not silently claim replacements for these operations.

## Stages and acceptance gates

1. Capture the current public header/ABI, functional results and locked performance artifacts. Freeze workload/compiler/runtime settings and hashes.
2. Add an optional foundation dependency and a private adapter target. Keep hx_log.h and its public ABI unchanged.
3. Migrate status/clock/path helpers, then files, synchronization, native thread lifetime and TLS. Keep each stage independently reversible and measurable.
4. Migrate network primitives only after preserving sink framing, reconnection, backpressure and close behavior. Do not substitute the general thread pool for the logger's ordered asynchronous output queue.
5. Verify formatting snapshots, unified ordering, callback reentry, allocator injection, ownership, repeated init/cleanup and static/shared installation.
6. Retain the logger's default 2-second shared close budget. A timeout keeps its resources/user data alive and allows retry. The platform library receives that deadline and does not impose it on other consumers.
7. Run the exact locked performance contract independently on Windows and WSL2. Only then make the new adapter the default and remove retired private code.

The observed hx_c_log performance contract uses medians of three complete processing runs with durable final flush: the new eight-producer throughput must reach at least five times the locked original single-producer baseline, and the new single-producer throughput must not regress against that baseline. Original baseline values are 201,186.88 records/s on Windows and 381,704.17 on WSL2; the eight-producer thresholds are 1,005,934.40 and 1,908,520.85. Treat the original evidence files and their hashes as authoritative when performing migration; this document is not a new performance result.

Report latency percentiles, completed/dropped/error counts, drain time, allocations, new locks/indirect calls/syscalls and absolute throughput. A cleaner directory or wrapper API is not performance evidence. New memory-pool use, malloc_trim and other allocator experiments require separate changes and benchmarks; do not combine them with seam migration.

## Packaging

Consumers may use add_subdirectory or the installed hx_c_platform::hx_c_platform target. A standalone installed logger package must find_dependency(hx_c_platform) when its exported static targets reference the foundation. Static dependencies include Threads, Windows sockets and any selected codec/atomic library as applicable. Shared packaging must deliver the matching DLL/so/dylib and honor each included dependency's license.

A host and plugin should share one compatible library instance when exchanging owning objects. Private static copies require private objects and matching allocators. No plugin unload is allowed while callbacks, native I/O, thread-local destructors or allocator user data still reference that plugin.
