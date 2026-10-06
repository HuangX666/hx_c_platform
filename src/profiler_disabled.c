#include "profiler_private.h"

hx_os_status hx_os_profiler_create_with_features(hx_os_context *context,
	const hx_os_profiler_options *options, uint32_t features,
	hx_os_profiler **out, hx_os_error *error)
{
	(void)features;

	return hx_os_profiler_create_with_options(context, options, out, error);
}

hx_os_status hx_os_profiler_snapshot_create(hx_os_profiler *profiler,
	hx_os_profiler_snapshot **out, hx_os_error *error)
{
	return hx_os_profiler_snapshot_create_with_context(
		NULL, profiler, out, error);
}

hx_os_status hx_os_profiler_snapshot_create_with_context(hx_os_context *context,
	hx_os_profiler *profiler, hx_os_profiler_snapshot **out,
	hx_os_error *error)
{
	(void)context;
	(void)profiler;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_profiler_snapshot_retain(hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;
}

void hx_os_profiler_snapshot_release(hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;
}

size_t hx_os_profiler_snapshot_count(const hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;

	return 0;
}

uint64_t hx_os_profiler_snapshot_time_ns(
	const hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;

	return 0;
}

const hx_os_profiler_entry *hx_os_profiler_snapshot_data(
	const hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;

	return NULL;
}

const hx_os_profiler_entry *hx_os_profiler_snapshot_at(
	const hx_os_profiler_snapshot *snapshot, size_t index)
{
	(void)snapshot;
	(void)index;

	return NULL;
}

uint32_t hx_os_profiler_snapshot_features(
	const hx_os_profiler_snapshot *snapshot)
{
	(void)snapshot;

	return 0;
}

hx_os_status hx_os_profiler_snapshot_quantile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	int exclusive, uint32_t numerator, uint32_t denominator,
	hx_os_profiler_quantile *out, hx_os_error *error)
{
	(void)snapshot;
	(void)record_id;
	(void)exclusive;
	(void)numerator;
	(void)denominator;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_snapshot_percentile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	unsigned percent, hx_os_profiler_quantile *out, hx_os_error *error)
{
	return hx_os_profiler_snapshot_quantile(
		snapshot, record_id, 0, (uint32_t)percent, 100, out, error);
}

hx_os_status hx_os_profiler_create(
	hx_os_context *context, hx_os_profiler **out, hx_os_error *error)
{
	(void)context;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_create_with_options(hx_os_context *context,
	const hx_os_profiler_options *options, hx_os_profiler **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_start_root(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	(void)profiler;
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_reach(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	(void)profiler;
	(void)name;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_leave(hx_os_profiler *profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_restart_thread(
	hx_os_profiler *profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

void hx_os_profiler_stop(hx_os_profiler *profiler)
{
	(void)profiler;
}

hx_os_status hx_os_profiler_close_until(
	hx_os_profiler **profiler, hx_os_deadline deadline, hx_os_error *error)
{
	(void)profiler;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_destroy(
	hx_os_profiler **profiler, hx_os_error *error)
{
	(void)profiler;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_dump(hx_os_profiler *profiler, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error)
{
	(void)profiler;
	(void)buffer;
	(void)capacity;
	(void)required;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_dump_string(
	hx_os_profiler *profiler, hx_string *out, hx_os_error *error)
{
	(void)profiler;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_os_profiler_save(
	hx_os_profiler *profiler, const char *path, hx_os_error *error)
{
	(void)profiler;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
