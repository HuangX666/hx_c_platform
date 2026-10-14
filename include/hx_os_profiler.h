#ifndef HX_C_PLATFORM_PROFILER_H
#define HX_C_PLATFORM_PROFILER_H

#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_profiler hx_os_profiler;
typedef struct hx_os_profiler_snapshot hx_os_profiler_snapshot;

/**
 * \brief Fixed-layout borrowed entry in an immutable snapshot.
 *
 * \details Durations are ns; averages/deviations are approximate doubles
 * (population deviation). Aborted/open scopes do not contribute duration
 * samples. Empty statistics are zero. Last means most recently completed, not
 * most recently started. Names are copied NUL-terminated UTF-8; name_bytes
 * excludes NUL. IDs identify paths in the originating profiler only; parent=0
 * identifies a root. Flags retain their dump meaning: 1=counter saturation,
 * 2=clock error. Saturated/clock-error statistics should not be treated as
 * exact measurements. Do not change fields or retain a name/entry after
 * releasing its snapshot.
 */
typedef struct hx_os_profiler_entry {
	const char *name;
	size_t name_bytes;
	uint64_t completed, aborted;
	uint64_t inclusive_ns, exclusive_ns;
	uint64_t minimum_ns, maximum_ns;
	uint64_t minimum_exclusive_ns, maximum_exclusive_ns;
	uint64_t last_inclusive_ns, last_exclusive_ns;
	double mean_inclusive_ns, deviation_inclusive_ns;
	double mean_exclusive_ns, deviation_exclusive_ns;
	uint32_t identifier, parent, depth, flags;
} hx_os_profiler_entry;

enum {
	HX_OS_PROFILER_HISTOGRAM_INCLUSIVE = 1u,
	HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE = 2u
};

typedef struct hx_os_profiler_quantile {
	uint64_t lower_ns, upper_ns;
	uint64_t samples;
} hx_os_profiler_quantile;

/**
 * \brief Captures all records consistently, copying names and statistics.
 *
 * \details Snapshot storage (including allocation overhead) is bounded by
 * max_output_bytes. Allocates outside the profiler lock; growth races retry at
 * most eight times, then BUSY. Monotonic capture time identifies the
 * observation instant. Success owns *out; failure sets it NULL. Default uses
 * the immutable default allocator; with_context uses the specified context
 * (NULL has the same rule). Retained snapshots survive profiler close and are
 * safe for concurrent reads. The snapshot's context and the loaded library
 * outlive its last reference.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_snapshot_create(
	hx_os_profiler *profiler, hx_os_profiler_snapshot **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_snapshot_create_with_context(
	hx_os_context *context, hx_os_profiler *profiler,
	hx_os_profiler_snapshot **out, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_profiler_snapshot_retain(
	hx_os_profiler_snapshot *snapshot);

HX_OS_API void HX_OS_CALL hx_os_profiler_snapshot_release(
	hx_os_profiler_snapshot *snapshot);

HX_OS_API size_t HX_OS_CALL hx_os_profiler_snapshot_count(
	const hx_os_profiler_snapshot *snapshot);

HX_OS_API uint64_t HX_OS_CALL hx_os_profiler_snapshot_time_ns(
	const hx_os_profiler_snapshot *snapshot);

/**
 * \brief Contiguous entries in ID order; a parent precedes its children.
 *
 * \details NULL snapshot has NULL data/count zero. at returns NULL for an
 * invalid index. Returned pointers borrow the snapshot through its last
 * release.
 */
HX_OS_API const hx_os_profiler_entry *HX_OS_CALL hx_os_profiler_snapshot_data(
	const hx_os_profiler_snapshot *snapshot);

HX_OS_API const hx_os_profiler_entry *HX_OS_CALL hx_os_profiler_snapshot_at(
	const hx_os_profiler_snapshot *snapshot, size_t index);

HX_OS_API uint32_t HX_OS_CALL hx_os_profiler_snapshot_features(
	const hx_os_profiler_snapshot *snapshot);

/**
 * \brief Inclusive P0..P100 convenience, keyed by entry.identifier (not array
 * index).
 *
 * \details Advanced quantile accepts exclusive=0/1 and numerator/denominator in
 * [0,1]. Requires the selected histogram, otherwise NOT_SUPPORTED; no completed
 * samples is NOT_FOUND, saturated statistics RANGE. Failure preserves out. Uses
 * nearest rank, returning the selected bucket's interval intersected with
 * observed min/max. P0/P100 return the exact observed extrema. Each enabled
 * channel has 252 fixed logarithmic bins (~2 KiB/record): 0..3 ns are exact,
 * then four bins per power of two, including UINT64_MAX. No sample list grows.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_snapshot_percentile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	unsigned percent, hx_os_profiler_quantile *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_snapshot_quantile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	int exclusive, uint32_t numerator, uint32_t denominator,
	hx_os_profiler_quantile *out, hx_os_error *error);

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

/**
 * \brief Explicit interval tree, no native stack capture or worker thread.
 *
 * \details Counters aggregate identical (parent path, UTF-8 name) across
 * threads. Inclusive/exclusive wall durations use the library's monotonic
 * clock. Concurrent intervals can sum to more than elapsed process wall time.
 * Storage grows by distinct path; each participating thread caches a bounded
 * frame array in TLS. Library coroutines use independent frame arrays;
 * max_threads bounds all participating contexts (native and coroutine).
 * Defaults above; maxima 65535 records, 1024 contexts, depth 64, names 4096
 * bytes, output 64 MiB, 65535 active API calls.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_create(
	hx_os_context *context, hx_os_profiler **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_create_with_options(
	hx_os_context *context, const hx_os_profiler_options *options,
	hx_os_profiler **out, hx_os_error *error);

/**
 * \brief Select optional distributions at creation; immutable for this
 * instance.
 *
 * \details Features are the OR of HISTOGRAM_* above; 0 keeps the original
 * default. With both channels and NULL options, the default output/snapshot
 * budget is 8 MiB; explicit options always keep their supplied budget. No
 * worker starts.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_create_with_features(
	hx_os_context *context, const hx_os_profiler_options *options,
	uint32_t features, hx_os_profiler **out, hx_os_error *error);

/**
 * \brief Thread-bound balanced scopes; names are copied strict UTF-8, nonempty.
 *
 * \details start_root requires no current scope (otherwise BUSY); reach
 * requires a current parent (otherwise INVALID). Only successful entries need
 * leave. Failure preserves the current stack. Repeated paths allocate no
 * records. Do not move an open scope to another thread/fiber or unwind through
 * the C call. Library coroutine scopes remain with their coroutine across a
 * yield. A coroutine can use at most 64 profiler instances at once. Its exit
 * aborts unfinished frames and releases its cached arrays. Other Windows FLS
 * usage follows fibers; max_threads bounds participating contexts.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_start_root(
	hx_os_profiler *profiler, const char *name, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_reach(
	hx_os_profiler *profiler, const char *name, hx_os_error *error);

/**
 * \brief Completed children subtract from exclusive time.
 *
 * \details A failed clock read keeps the frame for retry; clock errors mark the
 * record's flags in a later dump. Statistics saturate at UINT64_MAX and set
 * overflow flag value 1.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_leave(
	hx_os_profiler *profiler, hx_os_error *error);

/**
 * \brief Abort open frames and detach this thread's TLS cache.
 *
 * \details Completed records remain. Normal thread exit does the same
 * automatically. Call on idle long-lived threads before plugin unload; another
 * thread cannot clear TLS.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_restart_thread(
	hx_os_profiler *profiler, hx_os_error *error);

/**
 * \brief Stop new entries, allow leave/restart/dump.
 *
 * \details Existing scopes are not killed.
 */
HX_OS_API void HX_OS_CALL hx_os_profiler_stop(hx_os_profiler *profiler);

/**
 * \brief Auto-detaches caller's idle cache; an open caller scope gives DEADLOCK
 * without stopping.
 *
 * \details Waits for accepted API calls and all foreign TLS caches.
 * NOW/INFINITE allowed; timeout retains *profiler. If native TLS retirement is
 * still finishing, returns BUSY with the same handle for retry. Successful
 * close includes key destruction. Exclude new API entrants and parallel final
 * closes; all host calls must return before module unload. Detected reentry
 * during profiler allocation/free returns DEADLOCK; native TLS bookkeeping that
 * is still retiring can instead report BUSY.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_close_until(
	hx_os_profiler **profiler, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_destroy(
	hx_os_profiler **profiler, hx_os_error *error);

/**
 * \brief Consistent bounded snapshot of completed/aborted statistics.
 *
 * \details Open intervals contribute only on leave/abort. Output is UTF-8 TSV
 * with JSON-quoted names:
 * id,parent,depth,name,completed,aborted,inclusive_ns,exclusive_ns,min_ns,
 * max_ns,flags. Flags: 1=counter saturation, 2=clock error. Growth races retry
 * at most eight times then BUSY. required includes NUL; OUTPUT_FULL/other
 * failures preserve buffer/string. String uses its context. Snapshot allocation
 * and formatting run outside the profiler lock.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_dump(hx_os_profiler *profiler,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_dump_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error);

/**
 * \brief Dump and atomically publish, using FILE_UTILS; otherwise
 * NOT_SUPPORTED.
 *
 * \details Synchronous I/O follows the existing atomic-save durability
 * contract.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_profiler_save(
	hx_os_profiler *profiler, const char *path, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
