#ifndef HX_C_PLATFORM_PROFILER_PRIVATE_H
#define HX_C_PLATFORM_PROFILER_PRIVATE_H
#include "internal.h"
#include "hx_os_profiler.h"
typedef struct hx_os_profile_record {
	struct hx_os_profile_record *next;
	struct hx_os_profile_record *bucket_next;
	struct hx_os_profile_record *parent;
	uint64_t completed, aborted, inclusive, exclusive, minimum, maximum;
	size_t name_bytes;
	uint32_t identifier, depth, flags;
} hx_os_profile_record;
typedef struct hx_os_profile_frame {
	hx_os_profile_record *record;
	uint64_t started;
	uint64_t children;
} hx_os_profile_frame;
typedef struct hx_os_profile_thread {
	hx_os_profiler *profiler;
	size_t depth;
} hx_os_profile_thread;
struct hx_os_profiler {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_tls_key *key;
	hx_os_profile_record *first, *last;
	hx_os_profile_record *buckets[64];
	hx_os_profiler_options options;
	size_t records, pending_records, threads, operations;
	int closing, finalizing;
};
typedef struct hx_os_profile_guard {
	struct hx_os_profile_guard *previous;
	hx_os_profiler *profiler;
	int disposing;
} hx_os_profile_guard;
hx_os_status hx_os_i_profiler_begin(hx_os_profiler *profiler,
	hx_os_profile_guard *guard, int allow_closing, hx_os_error *error);
void hx_os_i_profiler_end(hx_os_profile_guard *guard);
#endif
