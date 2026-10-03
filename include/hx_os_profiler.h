#ifndef HX_C_PLATFORM_PROFILER_H
#define HX_C_PLATFORM_PROFILER_H
#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_profiler hx_os_profiler;
typedef struct hx_os_profiler_options {
	uint32_t size;
	uint32_t version;
	size_t max_records;
	size_t max_threads;
	size_t max_depth;
	size_t max_name_bytes;
	size_t max_output_bytes;
	size_t max_active_operations;
} hx_os_profiler_options;
#define HX_OS_PROFILER_OPTIONS_INIT                                            \
	{                                                                      \
		sizeof(hx_os_profiler_options), 1, 1024, 64, 32, 127,          \
			4 * 1024 * 1024, 64                                    \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_options_init(
	hx_os_profiler_options *options, size_t capacity);
/* Explicit interval tree, no native stack capture or worker thread.
 * Counters aggregate identical (parent path, UTF-8 name) across threads.
 * Inclusive/exclusive wall durations use the library's monotonic clock.
 * Concurrent intervals can sum to more than elapsed process wall time.
 * Storage grows by distinct path; each participating thread caches a bounded
 * frame array in TLS. Library coroutines use independent frame arrays;
 * max_threads bounds all participating contexts (native and coroutine).
 * Defaults above; maxima 65535 records, 1024 contexts,
 * depth 64, names 4096 bytes, output 64 MiB, 65535 active API calls.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_create(
	hx_os_context *context, hx_os_profiler **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_create_with_options(
	hx_os_context *context, const hx_os_profiler_options *options,
	hx_os_profiler **out, hx_os_error *error);
/* Thread-bound balanced scopes; names are copied strict UTF-8, nonempty.
 * start_root requires no current scope (otherwise BUSY); reach requires a
 * current parent (otherwise INVALID). Only successful entries need leave.
 * Failure preserves the current stack. Repeated paths allocate no records.
 * Do not move an open scope to another thread/fiber or unwind through the C
 * call. Library coroutine scopes remain with their coroutine across a yield.
 * A coroutine can use at most 64 profiler instances at once. Its exit aborts
 * unfinished frames and releases its cached arrays. Other Windows FLS usage
 * follows fibers; max_threads bounds participating contexts.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_start_root(
	hx_os_profiler *profiler, const char *name, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_reach(
	hx_os_profiler *profiler, const char *name, hx_os_error *error);
/* Completed children subtract from exclusive time. A failed clock read keeps
 * the frame for retry; clock errors mark the record's flags in a later dump.
 * Statistics saturate at UINT64_MAX and set overflow flag value 1.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_leave(
	hx_os_profiler *profiler, hx_os_error *error);
/* Abort open frames and detach this thread's TLS cache. Completed records
 * remain. Normal thread exit does the same automatically. Call on idle
 * long-lived threads before plugin unload; another thread cannot clear TLS.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_restart_thread(
	hx_os_profiler *profiler, hx_os_error *error);
/* Stop new entries, allow leave/restart/dump. Existing scopes are not killed.
 */
HX_OS_API void HX_OS_CALL hx_os_profiler_stop(hx_os_profiler *profiler);
/* Auto-detaches caller's idle cache; an open caller scope gives DEADLOCK
 * without stopping. Waits for accepted API calls and all foreign TLS caches.
 * NOW/INFINITE allowed; timeout retains *profiler. If native TLS retirement
 * is still finishing, returns BUSY with the same handle for retry.
 * Successful close includes key destruction. Exclude new API entrants and
 * parallel final closes; all host calls must return before module unload.
 * Detected reentry during profiler allocation/free returns DEADLOCK; native
 * TLS bookkeeping that is still retiring can instead report BUSY.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_close_until(
	hx_os_profiler **profiler, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_destroy(
	hx_os_profiler **profiler, hx_os_error *error);
/* Consistent bounded snapshot of completed/aborted statistics. Open intervals
 * contribute only on leave/abort. Output is UTF-8 TSV with JSON-quoted names:
 * id,parent,depth,name,completed,aborted,inclusive_ns,exclusive_ns,min_ns,
 * max_ns,flags. Flags: 1=counter saturation, 2=clock error.
 * Growth races retry at most eight times then BUSY. required includes NUL;
 * OUTPUT_FULL/other failures preserve buffer/string. String uses its context.
 * Snapshot allocation and formatting run outside the profiler lock.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_dump(hx_os_profiler *profiler,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_dump_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error);
/* Dump and atomically publish, using FILE_UTILS; otherwise NOT_SUPPORTED.
 * Synchronous I/O follows the existing atomic-save durability contract.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_save(
	hx_os_profiler *profiler, const char *path, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
