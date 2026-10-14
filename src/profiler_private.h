#ifndef HX_C_PLATFORM_PROFILER_PRIVATE_H
#define HX_C_PLATFORM_PROFILER_PRIVATE_H

#include "internal.h"
#include "hx_os_profiler.h"

void *hx_os_i_profiler_exchange_execution(void *value);

typedef struct hx_os_profile_record {
	struct hx_os_profile_record *next;
	struct hx_os_profile_record *bucket_next;
	struct hx_os_profile_record *parent;
	uint64_t completed, aborted, inclusive, exclusive, minimum, maximum;
	uint64_t minimum_exclusive, maximum_exclusive, last_inclusive,
		last_exclusive;
	double mean_inclusive, m2_inclusive, mean_exclusive, m2_exclusive;
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
	size_t records, pending_records, threads, operations, name_bytes;
	uint32_t features;
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

#define HX_OS_PROFILE_BUCKETS 252

/**
 * \brief Names immediately follow the record; optional bins follow at uint64
 * alignment.
 *
 * \details Feature flags never change after creation.
 */
static inline size_t hx_os_profile_bins_offset(size_t name_bytes)
{
	return (sizeof(hx_os_profile_record) + name_bytes + 1 + 7) & ~(size_t)7;
}

static inline unsigned hx_os_profile_bin(uint64_t value)
{
	uint64_t shifted = value;
	unsigned power = 0;

	if (value < 4)
		return (unsigned)value;
	if (shifted >> 32) {
		power += 32;
		shifted >>= 32;
	}
	if (shifted >> 16) {
		power += 16;
		shifted >>= 16;
	}
	if (shifted >> 8) {
		power += 8;
		shifted >>= 8;
	}
	if (shifted >> 4) {
		power += 4;
		shifted >>= 4;
	}
	if (shifted >> 2) {
		power += 2;
		shifted >>= 2;
	}
	if (shifted >> 1)
		++power;

	return 4 + (power - 2) * 4 + (unsigned)((value >> (power - 2)) & 3);
}
#endif
