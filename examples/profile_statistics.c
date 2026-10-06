#include <hx_os.h>
#include <stdio.h>

static hx_os_status collect_samples(hx_os_profiler *profiler)
{
	unsigned i;

	for (i = 0; i < 12; ++i) {
		hx_os_status status = hx_os_profiler_start_root(
			profiler, "read_sensor", NULL);
		if (status != HX_OS_OK)
			return status;

		/* Replace this simulated work with the operation being
		 * measured. */
		status = hx_os_sleep_until(
			hx_os_deadline_after_ms(1 + i % 3), NULL);
		if (status != HX_OS_OK)
			return status;
		status = hx_os_profiler_leave(profiler, NULL);
		if (status != HX_OS_OK)
			return status;
	}

	return HX_OS_OK;
}

static hx_os_status show_statistics(const hx_os_profiler_snapshot *snapshot)
{
	size_t i;
	const unsigned percentiles[] = {50, 95, 99};
	const hx_os_profiler_entry *entry =
		hx_os_profiler_snapshot_at(snapshot, 0);

	if (!entry)
		return HX_OS_NOT_FOUND;

	printf("%s: %llu samples, mean %.3f ms, deviation %.3f ms\n",
		entry->name, (unsigned long long)entry->completed,
		entry->mean_inclusive_ns / 1000000.0,
		entry->deviation_inclusive_ns / 1000000.0);

	for (i = 0; i < sizeof(percentiles) / sizeof(percentiles[0]); ++i) {
		hx_os_profiler_quantile value;
		hx_os_status status = hx_os_profiler_snapshot_percentile(
			snapshot, entry->identifier, percentiles[i], &value,
			NULL);
		if (status != HX_OS_OK)
			return status;

		printf("P%u: [%.3f, %.3f] ms (approximate interval)\n",
			percentiles[i], (double)value.lower_ns / 1000000.0,
			(double)value.upper_ns / 1000000.0);
	}

	return HX_OS_OK;
}

int main(void)
{
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_snapshot *snapshot = NULL;
	hx_os_status status = hx_os_profiler_create_with_features(NULL, NULL,
		HX_OS_PROFILER_HISTOGRAM_INCLUSIVE, &profiler, NULL);

	if (status == HX_OS_OK)
		status = collect_samples(profiler);
	if (status == HX_OS_OK)
		status = hx_os_profiler_snapshot_create(
			profiler, &snapshot, NULL);

	if (profiler) {
		hx_os_status closed;

		(void)hx_os_profiler_restart_thread(profiler, NULL);
		closed = hx_os_profiler_destroy(&profiler, NULL);
		if (status == HX_OS_OK)
			status = closed;
	}

	/* The snapshot remains valid after the profiler has closed. */
	if (status == HX_OS_OK)
		status = show_statistics(snapshot);
	hx_os_profiler_snapshot_release(snapshot);

	if (status != HX_OS_OK)
		fprintf(stderr, "Profiling failed: %s\n",
			hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
