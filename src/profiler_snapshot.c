#include "profiler_private.h"
#include <math.h>

struct hx_os_profiler_snapshot {
	hx_os_atomic references;
	uint64_t time_ns;
	size_t count, bins_offset;
	uint32_t features;
};

static size_t hx_os_profile_histogram_bytes(uint32_t features)
{
	unsigned channels = !!(features & HX_OS_PROFILER_HISTOGRAM_INCLUSIVE) +
		!!(features & HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE);

	return channels * HX_OS_PROFILE_BUCKETS * sizeof(uint64_t);
}

static size_t hx_os_profile_snapshot_bytes(
	const hx_os_profiler *profiler, size_t *bins_offset)
{
	/* Creation bounds records/names, so even the largest 32-bit layout fits
	 * size_t. The configured snapshot budget imposes the tighter limit.
	 */
	size_t bytes = sizeof(hx_os_profiler_snapshot) +
		profiler->records * sizeof(hx_os_profiler_entry) +
		profiler->name_bytes;

	*bins_offset = (bytes + 7) & ~(size_t)7;

	return *bins_offset +
		profiler->records *
		hx_os_profile_histogram_bytes(profiler->features);
}

static void hx_os_profile_entry_copy(
	hx_os_profiler_entry *entry, const hx_os_profile_record *record)
{
	entry->name = (const char *)(record + 1);
	entry->name_bytes = record->name_bytes;
	entry->completed = record->completed;
	entry->aborted = record->aborted;
	entry->inclusive_ns = record->inclusive;
	entry->exclusive_ns = record->exclusive;
	entry->minimum_ns = record->completed ? record->minimum : 0;
	entry->maximum_ns = record->maximum;
	entry->minimum_exclusive_ns =
		record->completed ? record->minimum_exclusive : 0;
	entry->maximum_exclusive_ns = record->maximum_exclusive;
	entry->last_inclusive_ns = record->last_inclusive;
	entry->last_exclusive_ns = record->last_exclusive;
	entry->mean_inclusive_ns = record->mean_inclusive;
	entry->mean_exclusive_ns = record->mean_exclusive;
	/* Carry M2 in the unpublished entry; sqrt runs outside the lock. */
	entry->deviation_inclusive_ns = record->m2_inclusive;
	entry->deviation_exclusive_ns = record->m2_exclusive;
	entry->identifier = record->identifier;
	entry->parent = record->parent ? record->parent->identifier : 0;
	entry->depth = record->depth;
	entry->flags = record->flags;
}

static double hx_os_profile_deviation(double m2, uint64_t count)
{
	return count && m2 > 0 ? sqrt(m2 / (double)count) : 0;
}

static void hx_os_profile_snapshot_finish(hx_os_profiler_snapshot *snapshot)
{
	hx_os_profiler_entry *entries = (hx_os_profiler_entry *)(snapshot + 1);
	char *names = (char *)(entries + snapshot->count);
	size_t i;

	for (i = 0; i < snapshot->count; ++i) {
		hx_os_profiler_entry *entry = entries + i;

		/* Record names are immutable and the operation guard pins them.
		 */
		memcpy(names, entry->name, entry->name_bytes + 1);
		entry->name = names;
		names += entry->name_bytes + 1;
		entry->deviation_inclusive_ns = hx_os_profile_deviation(
			entry->deviation_inclusive_ns, entry->completed);
		entry->deviation_exclusive_ns = hx_os_profile_deviation(
			entry->deviation_exclusive_ns, entry->completed);
	}
}

static hx_os_status hx_os_profile_capture(hx_os_context *context,
	hx_os_profiler *profiler, hx_os_profiler_snapshot **out,
	hx_os_error *error)
{
	hx_os_profiler_snapshot *snapshot = NULL;
	size_t capacity = 0;
	unsigned attempt;
	hx_os_status status = HX_OS_BUSY;

	for (attempt = 0; attempt < 8; ++attempt) {
		size_t bytes, cost, bins_offset;

		(void)hx_os_mutex_lock(profiler->mutex, NULL);
		bytes = hx_os_profile_snapshot_bytes(profiler, &bins_offset);
		cost = hx_os_i_allocation_cost(bytes, 0);
		if (!cost || cost > profiler->options.max_output_bytes) {
			(void)hx_os_mutex_unlock(profiler->mutex, NULL);
			status = HX_OS_LIMIT;
			break;
		}

		if (bytes <= capacity) {
			hx_os_profiler_entry *entries =
				(hx_os_profiler_entry *)(snapshot + 1);
			hx_os_profile_record *record;
			unsigned char *bins =
				(unsigned char *)snapshot + bins_offset;
			size_t stride = hx_os_profile_histogram_bytes(
				profiler->features);
			size_t i = 0;

			status =
				hx_os_monotonic_read(&snapshot->time_ns, error);
			if (status != HX_OS_OK) {
				(void)hx_os_mutex_unlock(profiler->mutex, NULL);
				hx_os_memory_free(snapshot);

				return status;
			}

			snapshot->count = profiler->records;
			snapshot->features = profiler->features;
			snapshot->bins_offset = bins_offset;
			hx_os_atomic_store(
				&snapshot->references, 1, HX_OS_RELAXED);

			for (record = profiler->first; record;
				record = record->next, ++i) {
				hx_os_profile_entry_copy(entries + i, record);
				if (stride) {
					const void *source =
						(const unsigned char *)record +
						hx_os_profile_bins_offset(
							record->name_bytes);

					memcpy(bins + i * stride, source,
						stride);
				}
			}

			(void)hx_os_mutex_unlock(profiler->mutex, NULL);
			hx_os_profile_snapshot_finish(snapshot);
			*out = snapshot;

			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}

		(void)hx_os_mutex_unlock(profiler->mutex, NULL);
		hx_os_memory_free(snapshot);
		snapshot = hx_os_i_alloc(context, bytes, 0);
		if (!snapshot)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		capacity = bytes;
	}

	hx_os_memory_free(snapshot);

	return hx_os_i_error(error, status, 0, 0);
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
	hx_os_profile_guard guard;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;

	status = hx_os_i_profiler_begin(profiler, &guard, 1, error);
	if (status != HX_OS_OK)
		return status;

	status = hx_os_profile_capture(context, profiler, out, error);
	hx_os_i_profiler_end(&guard);

	return status;
}

void hx_os_profiler_snapshot_retain(hx_os_profiler_snapshot *snapshot)
{
	if (snapshot)
		(void)hx_os_atomic_fetch_add(
			&snapshot->references, 1, HX_OS_RELAXED);
}

void hx_os_profiler_snapshot_release(hx_os_profiler_snapshot *snapshot)
{
	if (snapshot &&
		hx_os_atomic_fetch_add(
			&snapshot->references, -1, HX_OS_ACQ_REL) == 1)
		hx_os_memory_free(snapshot);
}

size_t hx_os_profiler_snapshot_count(const hx_os_profiler_snapshot *snapshot)
{
	return snapshot ? snapshot->count : 0;
}

uint64_t hx_os_profiler_snapshot_time_ns(
	const hx_os_profiler_snapshot *snapshot)
{
	return snapshot ? snapshot->time_ns : 0;
}

const hx_os_profiler_entry *hx_os_profiler_snapshot_data(
	const hx_os_profiler_snapshot *snapshot)
{
	return snapshot ? (const hx_os_profiler_entry *)(snapshot + 1) : NULL;
}

const hx_os_profiler_entry *hx_os_profiler_snapshot_at(
	const hx_os_profiler_snapshot *snapshot, size_t index)
{
	return snapshot && index < snapshot->count
		? hx_os_profiler_snapshot_data(snapshot) + index
		: NULL;
}

uint32_t hx_os_profiler_snapshot_features(
	const hx_os_profiler_snapshot *snapshot)
{
	return snapshot ? snapshot->features : 0;
}

static void hx_os_profile_bin_bounds(
	unsigned index, uint64_t *lower, uint64_t *upper)
{
	if (index < 4)
		*lower = *upper = index;
	else {
		unsigned power = (index - 4) / 4 + 2;
		uint64_t step = UINT64_C(1) << (power - 2);

		*lower = (UINT64_C(1) << power) + (index - 4) % 4 * step;
		*upper = *lower + (step - 1);
	}
}

hx_os_status hx_os_profiler_snapshot_quantile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	int exclusive, uint32_t numerator, uint32_t denominator,
	hx_os_profiler_quantile *out, hx_os_error *error)
{
	const hx_os_profiler_entry *entry;
	const uint64_t *bins;
	hx_os_profiler_quantile result;
	uint64_t minimum, maximum, rank, sum = 0;
	uint32_t feature = exclusive ? HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE
				     : HX_OS_PROFILER_HISTOGRAM_INCLUSIVE;
	size_t stride;
	unsigned i;

	if (!snapshot || !out || !denominator ||
		(exclusive != 0 && exclusive != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (numerator > denominator || !record_id ||
		record_id > snapshot->count)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (!(snapshot->features & feature))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	entry = hx_os_profiler_snapshot_at(snapshot, record_id - 1);
	if (!entry->completed)
		return hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
	if (entry->flags & 1u)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	result.samples = entry->completed;
	minimum = exclusive ? entry->minimum_exclusive_ns : entry->minimum_ns;
	maximum = exclusive ? entry->maximum_exclusive_ns : entry->maximum_ns;
	if (!numerator || numerator == denominator) {
		result.lower_ns = result.upper_ns =
			numerator ? maximum : minimum;
		*out = result;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	/* Split the product before multiplication; denominator is uint32_t. */
	rank = (entry->completed / denominator) * numerator +
		((entry->completed % denominator) * numerator + denominator -
			1) /
			denominator;
	stride = hx_os_profile_histogram_bytes(snapshot->features);
	bins = (const uint64_t *)((const unsigned char *)snapshot +
		snapshot->bins_offset + (record_id - 1) * stride);
	if (exclusive &&
		(snapshot->features & HX_OS_PROFILER_HISTOGRAM_INCLUSIVE))
		bins += HX_OS_PROFILE_BUCKETS;

	for (i = 0; i < HX_OS_PROFILE_BUCKETS; ++i) {
		sum += bins[i];
		if (sum < rank)
			continue;

		hx_os_profile_bin_bounds(i, &result.lower_ns, &result.upper_ns);
		if (result.lower_ns < minimum)
			result.lower_ns = minimum;
		if (result.upper_ns > maximum)
			result.upper_ns = maximum;
		*out = result;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}

	return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
}

hx_os_status hx_os_profiler_snapshot_percentile(
	const hx_os_profiler_snapshot *snapshot, uint32_t record_id,
	unsigned percent, hx_os_profiler_quantile *out, hx_os_error *error)
{
	if (percent > 100)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	return hx_os_profiler_snapshot_quantile(
		snapshot, record_id, 0, (uint32_t)percent, 100, out, error);
}
