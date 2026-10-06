#include "utility_test.h"
#include "../src/profiler_private.h"

static void record_leaf(hx_os_profiler *profiler)
{
	CHECK(hx_os_profiler_start_root(profiler, "leaf", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
}

static hx_os_profiler_snapshot *capture(hx_os_profiler *profiler)
{
	hx_os_profiler_snapshot *snapshot = NULL;

	CHECK(hx_os_profiler_snapshot_create(profiler, &snapshot, NULL) ==
		HX_OS_OK);

	return snapshot;
}

static void test_lifetime(void)
{
	test_memory source_memory = {0}, snapshot_memory = {0};
	hx_os_context *context = test_context(&source_memory);
	hx_os_context *other = test_context(&snapshot_memory);
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_snapshot *snapshot, *custom = NULL, *empty;
	const hx_os_profiler_entry *entries;
	hx_os_profiler_quantile quantile = {123, 456, 789};
	size_t calls;

	CHECK(hx_os_profiler_create(context, &profiler, NULL) == HX_OS_OK);
	empty = capture(profiler);
	CHECK(!hx_os_profiler_snapshot_count(empty));
	CHECK(!hx_os_profiler_snapshot_at(empty, 0));
	hx_os_profiler_snapshot_release(empty);

	record_leaf(profiler);
	calls = source_memory.calls;
	record_leaf(profiler);
	CHECK(source_memory.calls == calls);
	CHECK(hx_os_profiler_start_root(profiler, "parent", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_reach(profiler, "child", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_start_root(profiler, "unfinished", NULL) ==
		HX_OS_OK);

	snapshot = capture(profiler);
	CHECK(hx_os_profiler_snapshot_at(snapshot, 3)->aborted == 0);
	CHECK(hx_os_profiler_snapshot_at(snapshot, 3)->completed == 0);
	CHECK(hx_os_profiler_snapshot_at(snapshot, 3)->minimum_ns == 0);
	CHECK(hx_os_profiler_restart_thread(profiler, NULL) == HX_OS_OK);
	hx_os_profiler_snapshot_release(snapshot);
	snapshot = capture(profiler);
	CHECK(!hx_os_profiler_snapshot_features(snapshot));
	CHECK(hx_os_profiler_snapshot_percentile(
		      snapshot, 1, 95, &quantile, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(quantile.lower_ns == 123 && quantile.upper_ns == 456 &&
		quantile.samples == 789);

	snapshot_memory.fail = snapshot_memory.calls + 1;
	CHECK(hx_os_profiler_snapshot_create_with_context(
		      other, profiler, &custom, NULL) == HX_OS_NO_MEMORY &&
		!custom);
	snapshot_memory.fail = 0;
	CHECK(hx_os_profiler_snapshot_create_with_context(
		      other, profiler, &custom, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&other, NULL) == HX_OS_BUSY);
	hx_os_profiler_snapshot_release(custom);
	CHECK(hx_os_context_destroy(&other, NULL) == HX_OS_OK);
	CHECK(!snapshot_memory.live);

	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!source_memory.live);
	hx_os_profiler_snapshot_retain(snapshot);
	hx_os_profiler_snapshot_release(snapshot);
	CHECK(hx_os_profiler_snapshot_count(snapshot) == 4);
	entries = hx_os_profiler_snapshot_data(snapshot);
	CHECK(!strcmp(entries[0].name, "leaf") && entries[0].completed == 2);
	CHECK(entries[1].identifier == 2 && entries[1].parent == 0);
	CHECK(entries[2].parent == 2 && entries[2].depth == 2);
	CHECK(entries[1].exclusive_ns + entries[2].inclusive_ns ==
		entries[1].inclusive_ns);
	CHECK(entries[3].aborted == 1 && entries[3].completed == 0);
	CHECK(!hx_os_profiler_snapshot_at(snapshot, 4));
	hx_os_profiler_snapshot_release(snapshot);
}

static int compare_u64(const void *left, const void *right)
{
	uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;

	return (a > b) - (a < b);
}

static void check_close(double actual, double expected)
{
	double difference = actual - expected;

	if (difference < 0)
		difference = -difference;
	CHECK(difference <= (expected + 1) * 1e-8);
}

static void test_distribution(uint32_t features)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_snapshot *snapshot = NULL;
	hx_os_profiler_quantile quantile;
	const hx_os_profiler_entry *entry;
	uint64_t samples[25], before, after;
	double mean = 0, variance = 0;
	size_t i, calls = 0;
	unsigned percent;
	int exclusive = features == HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE;

	CHECK(hx_os_profiler_create_with_features(
		      context, NULL, features, &profiler, NULL) == HX_OS_OK);
	for (i = 0; i < 25; ++i) {
		CHECK(hx_os_profiler_start_root(profiler, "sample", NULL) ==
			HX_OS_OK);
		if (!i)
			calls = memory.calls;
		CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(i % 4), NULL) ==
			HX_OS_OK);
		CHECK(hx_os_profiler_leave(profiler, NULL) == HX_OS_OK);
		CHECK(memory.calls == calls);

		hx_os_profiler_snapshot_release(snapshot);
		before = hx_os_monotonic_ns();
		snapshot = capture(profiler);
		after = hx_os_monotonic_ns();
		CHECK(hx_os_profiler_snapshot_time_ns(snapshot) >= before);
		CHECK(hx_os_profiler_snapshot_time_ns(snapshot) <= after);
		entry = hx_os_profiler_snapshot_at(snapshot, 0);
		CHECK(entry->completed == i + 1 && !entry->flags);
		samples[i] = entry->last_inclusive_ns;
		mean += (double)samples[i] / 25;
	}

	for (i = 0; i < 25; ++i) {
		double difference = (double)samples[i] - mean;

		variance += difference * difference / 25;
	}
	check_close(entry->mean_inclusive_ns, mean);
	check_close(
		entry->deviation_inclusive_ns * entry->deviation_inclusive_ns,
		variance);
	check_close(entry->mean_exclusive_ns, mean);
	check_close(
		entry->deviation_exclusive_ns * entry->deviation_exclusive_ns,
		variance);
	CHECK(entry->last_exclusive_ns == samples[24]);
	qsort(samples, 25, sizeof(*samples), compare_u64);
	CHECK(entry->minimum_ns == samples[0] &&
		entry->maximum_ns == samples[24]);
	CHECK(entry->minimum_exclusive_ns == samples[0]);
	CHECK(entry->maximum_exclusive_ns == samples[24]);

	for (percent = 0; percent <= 100; ++percent) {
		size_t rank = percent ? (25 * percent + 99) / 100 - 1 : 0;

		CHECK(hx_os_profiler_snapshot_quantile(snapshot, 1, exclusive,
			      percent, 100, &quantile, NULL) == HX_OS_OK);
		CHECK(quantile.samples == 25 &&
			quantile.lower_ns <= samples[rank]);
		CHECK(quantile.upper_ns >= samples[rank]);
		CHECK(quantile.lower_ns >= samples[0] &&
			quantile.upper_ns <= samples[24]);
		if (!percent || percent == 100)
			CHECK(quantile.lower_ns == quantile.upper_ns);
	}
	if (!exclusive)
		CHECK(hx_os_profiler_snapshot_percentile(
			      snapshot, 1, 95, &quantile, NULL) == HX_OS_OK);
	if (features != 3)
		CHECK(hx_os_profiler_snapshot_quantile(snapshot, 1, !exclusive,
			      1, 2, &quantile, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_profiler_snapshot_quantile(snapshot, 1, exclusive,
		      UINT32_MAX - 1, UINT32_MAX, &quantile, NULL) == HX_OS_OK);
	CHECK(quantile.lower_ns <= samples[24] &&
		quantile.upper_ns >= samples[24]);
	quantile.lower_ns = 987;
	CHECK(hx_os_profiler_snapshot_quantile(snapshot, 1, exclusive, 1, 0,
		      &quantile, NULL) == HX_OS_INVALID);
	CHECK(hx_os_profiler_snapshot_percentile(
		      snapshot, 1, 101, &quantile, NULL) == HX_OS_RANGE);
	CHECK(hx_os_profiler_snapshot_percentile(
		      snapshot, 0, 50, &quantile, NULL) == HX_OS_RANGE);
	CHECK(quantile.lower_ns == 987);
	hx_os_profiler_snapshot_release(snapshot);

	CHECK(hx_os_profiler_start_root(profiler, "empty", NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_restart_thread(profiler, NULL) == HX_OS_OK);
	snapshot = capture(profiler);
	CHECK(hx_os_profiler_snapshot_quantile(snapshot, 2, exclusive, 1, 2,
		      &quantile, NULL) == HX_OS_NOT_FOUND);
	hx_os_profiler_snapshot_release(snapshot);
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static void *record_thread(void *user)
{
	size_t i;

	for (i = 0; i < 500; ++i)
		record_leaf(user);

	return NULL;
}

static void test_concurrent(void)
{
	hx_os_profiler *profiler = NULL;
	hx_os_thread *threads[3] = {NULL};
	size_t i;
	uint64_t completed = 0;

	CHECK(hx_os_profiler_create_with_features(
		      NULL, NULL, 3, &profiler, NULL) == HX_OS_OK);
	for (i = 0; i < 3; ++i)
		CHECK(hx_os_thread_create(NULL, record_thread, profiler,
			      threads + i, NULL) == HX_OS_OK);
	for (i = 0; i < 100; ++i) {
		hx_os_profiler_snapshot *snapshot = capture(profiler);
		const hx_os_profiler_entry *entry =
			hx_os_profiler_snapshot_at(snapshot, 0);

		if (entry) {
			CHECK(entry->completed >= completed);
			completed = entry->completed;
			CHECK(entry->inclusive_ns == entry->exclusive_ns);
			if (completed) {
				hx_os_profiler_quantile quantile;

				CHECK(hx_os_profiler_snapshot_percentile(
					      snapshot, 1, 50, &quantile,
					      NULL) == HX_OS_OK);
				CHECK(quantile.samples == completed);
			}
		}
		hx_os_profiler_snapshot_release(snapshot);
	}
	for (i = 0; i < 3; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(threads + i, NULL) == HX_OS_OK);
	}
	{
		hx_os_profiler_snapshot *snapshot = capture(profiler);

		CHECK(hx_os_profiler_snapshot_at(snapshot, 0)->completed ==
			1500);
		hx_os_profiler_snapshot_release(snapshot);
	}
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
}

static void test_bucket_boundaries(void)
{
	static const uint64_t samples[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 255,
		256, 1023, 1024, UINT64_MAX / 2, UINT64_MAX / 2 + 1,
		UINT64_MAX};
	hx_os_profiler *profiler = NULL;
	hx_os_profile_record *record;
	hx_os_profiler_snapshot *snapshot;
	hx_os_profiler_quantile quantile;
	uint64_t *bins;
	size_t i, count = sizeof(samples) / sizeof(samples[0]);

	CHECK(hx_os_profile_bin(0) == 0 && hx_os_profile_bin(3) == 3);
	CHECK(hx_os_profile_bin(4) == 4 && hx_os_profile_bin(7) == 7);
	CHECK(hx_os_profile_bin(8) == 8 && hx_os_profile_bin(15) == 11);
	CHECK(hx_os_profile_bin(UINT64_MAX) == 251);
	CHECK(hx_os_profiler_create_with_features(
		      NULL, NULL, 1, &profiler, NULL) == HX_OS_OK);
	record_leaf(profiler);

	/* Inject quiescent samples that cannot be timed in a practical test,
	 * including zero and the last uint64 bucket. No public data is mutated.
	 */
	record = profiler->first;
	bins = (uint64_t *)((unsigned char *)record +
		hx_os_profile_bins_offset(record->name_bytes));
	memset(bins, 0, HX_OS_PROFILE_BUCKETS * sizeof(*bins));
	for (i = 0; i < count; ++i)
		++bins[hx_os_profile_bin(samples[i])];
	record->completed = count;
	record->minimum = 0;
	record->maximum = UINT64_MAX;
	snapshot = capture(profiler);

	for (i = 0; i < count; ++i) {
		CHECK(hx_os_profiler_snapshot_quantile(snapshot, 1, 0,
			      (uint32_t)i + 1, (uint32_t)count, &quantile,
			      NULL) == HX_OS_OK);
		CHECK(quantile.lower_ns <= samples[i]);
		CHECK(quantile.upper_ns >= samples[i]);
	}
	hx_os_profiler_snapshot_release(snapshot);
	record->flags |= 1u;
	snapshot = capture(profiler);
	CHECK(hx_os_profiler_snapshot_percentile(
		      snapshot, 1, 95, &quantile, NULL) == HX_OS_RANGE);
	hx_os_profiler_snapshot_release(snapshot);
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
}

int main(void)
{
	hx_os_profiler *profiler = NULL;
	hx_os_profiler_snapshot *snapshot = NULL;
	hx_os_profiler_options options = HX_OS_PROFILER_OPTIONS_INIT;
	hx_os_profiler_quantile quantile;

	CHECK(!hx_os_profiler_snapshot_count(NULL));
	CHECK(!hx_os_profiler_snapshot_at(NULL, 0));
	CHECK(!hx_os_profiler_snapshot_data(NULL));
	hx_os_profiler_snapshot_release(NULL);
	hx_os_profiler_snapshot_retain(NULL);
	if (!(hx_os_capabilities() & HX_OS_CAP_PROFILER)) {
		CHECK(hx_os_profiler_create_with_features(NULL, NULL, 3,
			      &profiler, NULL) == HX_OS_NOT_SUPPORTED &&
			!profiler);
		CHECK(hx_os_profiler_snapshot_create(NULL, &snapshot, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!snapshot);
		CHECK(hx_os_profiler_snapshot_percentile(NULL, 1, 95, &quantile,
			      NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	CHECK(hx_os_profiler_create_with_features(
		      NULL, NULL, 4, &profiler, NULL) == HX_OS_INVALID &&
		!profiler);
	options.max_output_bytes = 1;
	CHECK(hx_os_profiler_create_with_options(
		      NULL, &options, &profiler, NULL) == HX_OS_OK);
	CHECK(hx_os_profiler_snapshot_create(profiler, &snapshot, NULL) ==
			HX_OS_LIMIT &&
		!snapshot);
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
	test_lifetime();
	options.max_output_bytes = 512;
	CHECK(hx_os_profiler_create_with_features(
		      NULL, &options, 1, &profiler, NULL) == HX_OS_OK);
	record_leaf(profiler);
	CHECK(hx_os_profiler_snapshot_create(profiler, &snapshot, NULL) ==
			HX_OS_LIMIT &&
		!snapshot);
	{
		char buffer[512];
		size_t required;

		CHECK(hx_os_profiler_dump(profiler, buffer, sizeof(buffer),
			      &required, NULL) == HX_OS_OK);
		CHECK(strstr(buffer, "# hx_profiler v1\n"));
	}
	CHECK(hx_os_profiler_destroy(&profiler, NULL) == HX_OS_OK);
	test_distribution(HX_OS_PROFILER_HISTOGRAM_INCLUSIVE);
	test_distribution(HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE);
	test_distribution(3);
	test_concurrent();
	test_bucket_boundaries();

	return 0;
}
