#include "profiler_private.h"
#if HX_OS_ENABLE_COROUTINES
#include "hx_os_coroutine.h"
#endif
hx_os_status hx_os_profiler_options_init(
	hx_os_profiler_options *options, size_t capacity)
{
	const hx_os_profiler_options defaults = HX_OS_PROFILER_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;

	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;

	*options = defaults;

	return HX_OS_OK;
}
#if HX_OS_ENABLE_PROFILER
#if defined(_MSC_VER)
static __declspec(thread) hx_os_profile_guard *hx_os_profile_current;
#else
static __thread hx_os_profile_guard *hx_os_profile_current;
#endif
void *hx_os_i_profiler_exchange_execution(void *value)
{
	hx_os_profile_guard *previous = hx_os_profile_current;

	hx_os_profile_current = value;

	return previous;
}

static hx_os_profile_guard *hx_os_profiler_self(hx_os_profiler *profiler)
{
	hx_os_profile_guard *guard;

	for (guard = hx_os_profile_current; guard; guard = guard->previous)
		if (guard->profiler == profiler)
			return guard;

	return NULL;
}

hx_os_status hx_os_i_profiler_begin(hx_os_profiler *profiler,
	hx_os_profile_guard *guard, int allow_closing, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;

	if (!profiler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_profiler_self(profiler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	if (profiler->finalizing || (profiler->closing && !allow_closing))
		status = HX_OS_CLOSED;
	else if (profiler->operations ==
		profiler->options.max_active_operations)
		status = HX_OS_LIMIT;
	else
		++profiler->operations;

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	if (status == HX_OS_OK) {
		guard->previous = hx_os_profile_current;
		guard->profiler = profiler;
		guard->disposing = 0;
		hx_os_profile_current = guard;
	}

	return hx_os_i_error(error, status, 0, 0);
}

// Finish the operation and release its held mutex after all cleanup is done.
static void hx_os_profiler_end_locked(hx_os_profile_guard *guard)
{
	hx_os_profiler *profiler = guard->profiler;

	--profiler->operations;
	if (profiler->closing && !profiler->operations)
		hx_os_condition_notify_all(profiler->condition);

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	hx_os_profile_current = guard->previous;
}

void hx_os_i_profiler_end(hx_os_profile_guard *guard)
{
	(void)hx_os_mutex_lock(guard->profiler->mutex, NULL);
	hx_os_profiler_end_locked(guard);
}

static uint64_t hx_os_profiler_sum(
	uint64_t value, uint64_t added, hx_os_profile_record *record)
{
	if (UINT64_MAX - value < added) {
		record->flags |= 1u;

		return UINT64_MAX;
	}

	return value + added;
}

static void HX_OS_CALL hx_os_profiler_thread_exit(void *value, void *user)
{
	hx_os_profile_thread *thread = value;
	hx_os_profiler *profiler = user;
	hx_os_profile_frame *frames = (hx_os_profile_frame *)(thread + 1);
	hx_os_profile_guard guard;
	size_t i;

	guard.previous = hx_os_profile_current;
	guard.profiler = profiler;
	guard.disposing = 0;
	hx_os_profile_current = &guard;
	(void)hx_os_mutex_lock(profiler->mutex, NULL);

	for (i = 0; i < thread->depth; ++i) {
		hx_os_profile_record *record = frames[i].record;

		record->aborted =
			hx_os_profiler_sum(record->aborted, 1, record);
	}

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);

	hx_os_memory_free(thread);
	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	--profiler->threads;
	if (profiler->closing)
		hx_os_condition_notify_all(profiler->condition);

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	hx_os_profile_current = guard.previous;
	// The common TLS wrapper finishes retiring after this callback.
	// close must also successfully destroy the key before freeing owner.
}

hx_os_status hx_os_profiler_create(
	hx_os_context *context, hx_os_profiler **out, hx_os_error *error)
{
	return hx_os_profiler_create_with_options(context, NULL, out, error);
}

hx_os_status hx_os_profiler_create_with_options(hx_os_context *context,
	const hx_os_profiler_options *options, hx_os_profiler **out,
	hx_os_error *error)
{
	return hx_os_profiler_create_with_features(
		context, options, 0, out, error);
}

hx_os_status hx_os_profiler_create_with_features(hx_os_context *context,
	const hx_os_profiler_options *options, uint32_t features,
	hx_os_profiler **out, hx_os_error *error)
{
	hx_os_profiler_options defaults = HX_OS_PROFILER_OPTIONS_INIT;
	hx_os_profiler *profiler;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (features &
		~(uint32_t)(HX_OS_PROFILER_HISTOGRAM_INCLUSIVE |
			HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (features ==
		(HX_OS_PROFILER_HISTOGRAM_INCLUSIVE |
			HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE))
		defaults.max_output_bytes = 8u * 1024u * 1024u;
	if (!options)
		options = &defaults;

	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_records || options->max_records > 65535 ||
		!options->max_threads || options->max_threads > 1024 ||
		!options->max_depth || options->max_depth > 64 ||
		!options->max_name_bytes || options->max_name_bytes > 4096 ||
		!options->max_output_bytes ||
		options->max_output_bytes > 64u * 1024u * 1024u ||
		!options->max_active_operations ||
		options->max_active_operations > 65535)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	profiler = hx_os_i_zero(context, sizeof(*profiler));
	if (!profiler)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	profiler->context = context;
	profiler->options = *options;
	profiler->features = features;

	status = hx_os_mutex_create(context, 0, &profiler->mutex, error);
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &profiler->condition, error);

	if (status == HX_OS_OK)
		status = hx_os_tls_key_create(context,
			hx_os_profiler_thread_exit, profiler, &profiler->key,
			error);

	if (status != HX_OS_OK) {
		if (profiler->condition)
			hx_os_condition_destroy(profiler->condition);

		if (profiler->mutex)
			hx_os_mutex_destroy(profiler->mutex);

		hx_os_memory_free(profiler);

		return status;
	}

	*out = profiler;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static void *hx_os_profiler_context_get(hx_os_profiler *profiler)
{
#if HX_OS_ENABLE_COROUTINES
	if (hx_os_coro_current())
		return hx_os_i_coro_local_get(profiler);
#endif
	return hx_os_tls_get(profiler->key);
}

static void hx_os_profiler_context_clear(hx_os_profiler *profiler)
{
#if HX_OS_ENABLE_COROUTINES
	if (hx_os_coro_current()) {
		hx_os_i_coro_local_clear(profiler);

		return;
	}
#endif
	hx_os_tls_clear(profiler->key);
}

static hx_os_status hx_os_profiler_context_set(hx_os_profiler *profiler,
	hx_os_profile_thread *thread, hx_os_error *error)
{
#if HX_OS_ENABLE_COROUTINES
	if (hx_os_coro_current())
		return hx_os_i_coro_local_set(
			profiler, thread, hx_os_profiler_thread_exit, error);
#endif
	return hx_os_tls_set(profiler->key, thread, error);
}

static hx_os_status hx_os_profiler_thread(hx_os_profiler *profiler,
	hx_os_profile_thread **out, int *created, hx_os_error *error)
{
	hx_os_profile_thread *thread;
	hx_os_status status;

	*created = 0;
	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	if (profiler->closing)
		status = HX_OS_CLOSED;
	else if (profiler->threads == profiler->options.max_threads)
		status = HX_OS_LIMIT;
	else {
		++profiler->threads;

		status = HX_OS_OK;
	}

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	thread = hx_os_i_zero(profiler->context,
		sizeof(*thread) +
			profiler->options.max_depth *
				sizeof(hx_os_profile_frame));
	if (!thread)
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	else {
		thread->profiler = profiler;

		status = hx_os_profiler_context_set(profiler, thread, error);
	}

	if (status != HX_OS_OK) {
		hx_os_memory_free(thread);
		(void)hx_os_mutex_lock(profiler->mutex, NULL);
		--profiler->threads;
		if (profiler->closing)
			hx_os_condition_notify_all(profiler->condition);

		(void)hx_os_mutex_unlock(profiler->mutex, NULL);

		return status;
	}

	*out = thread;
	*created = 1;

	return HX_OS_OK;
}

static hx_os_profile_record *hx_os_profiler_find(hx_os_profiler *profiler,
	hx_os_profile_record *parent, unsigned bucket, const char *name,
	size_t bytes)
{
	hx_os_profile_record *record;

	for (record = profiler->buckets[bucket]; record;
		record = record->bucket_next)
		if (record->parent == parent && record->name_bytes == bytes &&
			!memcmp(record + 1, name, bytes))
			return record;
	return NULL;
}

static hx_os_status hx_os_profiler_record(hx_os_profiler *profiler,
	hx_os_profile_record *parent, unsigned bucket, const char *name,
	size_t bytes, hx_os_profile_record **out, int *locked,
	hx_os_error *error)
{
	hx_os_profile_record *record, *candidate;
	hx_os_status status = HX_OS_OK;
	size_t allocation = sizeof(*candidate) + bytes + 1;
	unsigned histograms =
		!!(profiler->features & HX_OS_PROFILER_HISTOGRAM_INCLUSIVE) +
		!!(profiler->features & HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE);

	*locked = 0;
	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	record = hx_os_profiler_find(profiler, parent, bucket, name, bytes);
	if (profiler->closing)
		status = HX_OS_CLOSED;
	else if (!record &&
		profiler->records + profiler->pending_records ==
			profiler->options.max_records)
		status = HX_OS_LIMIT;
	else if (!record)
		++profiler->pending_records;

	if (status == HX_OS_OK && record) {
		// Commit the frame and finish the call under this same lock.
		*out = record;
		*locked = 1;

		return HX_OS_OK;
	}

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	if (histograms)
		allocation = hx_os_profile_bins_offset(bytes) +
			histograms * HX_OS_PROFILE_BUCKETS * sizeof(uint64_t);
	candidate = hx_os_i_zero(profiler->context, allocation);
	if (candidate) {
		candidate->parent = parent;
		candidate->name_bytes = bytes;
		candidate->depth = parent ? parent->depth + 1 : 1;
		candidate->minimum = UINT64_MAX;
		candidate->minimum_exclusive = UINT64_MAX;
		memcpy(candidate + 1, name, bytes);
	}

	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	--profiler->pending_records;
	if (!candidate)
		status = HX_OS_NO_MEMORY;
	else if (profiler->closing)
		status = HX_OS_CLOSED;
	else {
		record = hx_os_profiler_find(
			profiler, parent, bucket, name, bytes);
		if (!record) {
			record = candidate;
			candidate = NULL;
			record->identifier = (uint32_t)++profiler->records;
			profiler->name_bytes += bytes + 1;
			record->bucket_next = profiler->buckets[bucket];
			profiler->buckets[bucket] = record;
			if (profiler->last)
				profiler->last->next = record;
			else
				profiler->first = record;

			profiler->last = record;
		}
	}

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);

	hx_os_memory_free(candidate);
	if (status == HX_OS_OK)
		*out = record;

	return hx_os_i_error(error, status, 0, 0);
}

static hx_os_status hx_os_profiler_enter(hx_os_profiler *profiler,
	const char *name, int root, hx_os_error *error)
{
	hx_os_profile_guard guard;
	hx_os_profile_thread *thread = NULL;
	hx_os_profile_frame *frames;
	hx_os_profile_record *record = NULL, *parent = NULL;
	uint64_t now;
	uint32_t hash = UINT32_C(2166136261);
	size_t bytes = 0, invalid;
	int created = 0, locked = 0;
	hx_os_status status =
		hx_os_i_profiler_begin(profiler, &guard, 0, error);

	if (status != HX_OS_OK)
		return status;

	if (!name || !*name) {
		status = HX_OS_INVALID;
		goto done;
	}

	thread = hx_os_profiler_context_get(profiler);
	if (thread && thread->depth) {
		frames = (hx_os_profile_frame *)(thread + 1);
		parent = frames[thread->depth - 1].record;
		hash ^= parent->identifier;
	}

	while (bytes <= profiler->options.max_name_bytes && name[bytes]) {
		hash = (hash ^ (unsigned char)name[bytes]) * UINT32_C(16777619);
		++bytes;
	}

	if (bytes > profiler->options.max_name_bytes) {
		status = HX_OS_LIMIT;
		goto done;
	}

	status = hx_os_text_validate_utf8(name, bytes, &invalid);
	if (status != HX_OS_OK)
		goto done;

	if ((root && thread && thread->depth) ||
		(!root && (!thread || !thread->depth))) {
		status = root ? HX_OS_BUSY : HX_OS_INVALID;
		goto done;
	}

	if (thread && thread->depth == profiler->options.max_depth) {
		status = HX_OS_LIMIT;
		goto done;
	}

	if (!thread)
		status = hx_os_profiler_thread(
			profiler, &thread, &created, error);
	if (status != HX_OS_OK)
		goto native_done;

	frames = (hx_os_profile_frame *)(thread + 1);

	status = hx_os_profiler_record(profiler, parent, (unsigned)(hash & 63),
		name, bytes, &record, &locked, error);
	if (status == HX_OS_OK)
		status = hx_os_monotonic_read(&now, error);

	if (status == HX_OS_OK) {
		frames[thread->depth].record = record;
		frames[thread->depth].started = now;
		frames[thread->depth].children = 0;
		++thread->depth;
	}

native_done:
	if (status != HX_OS_OK && created) {
		if (locked) {
			(void)hx_os_mutex_unlock(profiler->mutex, NULL);
			locked = 0;
		}
		hx_os_profiler_context_clear(profiler);
	}

	if (locked)
		hx_os_profiler_end_locked(&guard);
	else
		hx_os_i_profiler_end(&guard);

	return status;

done:
	hx_os_i_profiler_end(&guard);
	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_profiler_start_root(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	return hx_os_profiler_enter(profiler, name, 1, error);
}

hx_os_status hx_os_profiler_reach(
	hx_os_profiler *profiler, const char *name, hx_os_error *error)
{
	return hx_os_profiler_enter(profiler, name, 0, error);
}

hx_os_status hx_os_profiler_leave(hx_os_profiler *profiler, hx_os_error *error)
{
	hx_os_profile_guard guard;
	hx_os_profile_thread *thread;
	hx_os_profile_frame *frames, *frame;
	hx_os_profile_record *record;
	uint64_t now, elapsed, exclusive;
	int detach = 0;
	hx_os_status status =
		hx_os_i_profiler_begin(profiler, &guard, 1, error);

	if (status != HX_OS_OK)
		return status;

	thread = hx_os_profiler_context_get(profiler);
	if (!thread || !thread->depth) {
		hx_os_i_profiler_end(&guard);

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	frames = (hx_os_profile_frame *)(thread + 1);
	frame = &frames[thread->depth - 1];
	record = frame->record;

	status = hx_os_monotonic_read(&now, error);
	if (status == HX_OS_OK && now < frame->started)
		status = hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	if (status != HX_OS_OK)
		record->flags |= 2u;
	else {
		double delta, count;

		elapsed = now - frame->started;
		if (elapsed < frame->children)
			record->flags |= 2u;
		exclusive = elapsed >= frame->children
			? elapsed - frame->children
			: 0;

		record->completed =
			hx_os_profiler_sum(record->completed, 1, record);
		record->inclusive =
			hx_os_profiler_sum(record->inclusive, elapsed, record);
		record->exclusive = hx_os_profiler_sum(
			record->exclusive, exclusive, record);
		if (record->minimum > elapsed)
			record->minimum = elapsed;

		if (record->maximum < elapsed)
			record->maximum = elapsed;
		if (record->minimum_exclusive > exclusive)
			record->minimum_exclusive = exclusive;
		if (record->maximum_exclusive < exclusive)
			record->maximum_exclusive = exclusive;
		record->last_inclusive = elapsed;
		record->last_exclusive = exclusive;

		// Welford updates need no retained samples or new allocations.
		count = (double)record->completed;
		delta = (double)elapsed - record->mean_inclusive;
		record->mean_inclusive += delta / count;
		record->m2_inclusive +=
			delta * ((double)elapsed - record->mean_inclusive);
		delta = (double)exclusive - record->mean_exclusive;
		record->mean_exclusive += delta / count;
		record->m2_exclusive +=
			delta * ((double)exclusive - record->mean_exclusive);
		if (profiler->features) {
			uint64_t *bins = (uint64_t *)((unsigned char *)record +
				hx_os_profile_bins_offset(record->name_bytes));
			unsigned index;

			if (profiler->features &
				HX_OS_PROFILER_HISTOGRAM_INCLUSIVE) {
				index = hx_os_profile_bin(elapsed);
				bins[index] = hx_os_profiler_sum(
					bins[index], 1, record);
				bins += HX_OS_PROFILE_BUCKETS;
			}
			if (profiler->features &
				HX_OS_PROFILER_HISTOGRAM_EXCLUSIVE) {
				index = hx_os_profile_bin(exclusive);
				bins[index] = hx_os_profiler_sum(
					bins[index], 1, record);
			}
		}

		--thread->depth;
		if (thread->depth) {
			hx_os_profile_frame *parent =
				&frames[thread->depth - 1];

			parent->children = hx_os_profiler_sum(
				parent->children, elapsed, parent->record);
		}

		detach = profiler->closing && !thread->depth;
	}

	if (!detach) {
		hx_os_profiler_end_locked(&guard);

		return status;
	}

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	hx_os_profiler_context_clear(profiler);
	hx_os_i_profiler_end(&guard);

	return status;
}

hx_os_status hx_os_profiler_restart_thread(
	hx_os_profiler *profiler, hx_os_error *error)
{
	hx_os_profile_guard guard;
	hx_os_status status =
		hx_os_i_profiler_begin(profiler, &guard, 1, error);

	if (status != HX_OS_OK)
		return status;

	hx_os_profiler_context_clear(profiler);
	hx_os_i_profiler_end(&guard);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_profiler_stop(hx_os_profiler *profiler)
{
	hx_os_profile_guard *guard;

	if (!profiler)
		return;

	guard = hx_os_profiler_self(profiler);
	if (guard && guard->disposing)
		return;

	(void)hx_os_mutex_lock(profiler->mutex, NULL);
	profiler->closing = 1;
	hx_os_condition_notify_all(profiler->condition);
	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
}

hx_os_status hx_os_profiler_close_until(
	hx_os_profiler **owner, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_profiler *profiler;
	hx_os_profile_thread *thread;
	hx_os_profile_guard guard;
	hx_os_profile_record *record;
	hx_os_status status = HX_OS_OK;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	profiler = *owner;
	if (!profiler)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_profiler_self(profiler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	thread = hx_os_profiler_context_get(profiler);
	if (thread && thread->depth)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	guard.previous = hx_os_profile_current;
	guard.profiler = profiler;
	guard.disposing = 0;
	hx_os_profile_current = &guard;
	hx_os_profiler_stop(profiler);
	if (thread)
		hx_os_profiler_context_clear(profiler);

	(void)hx_os_mutex_lock(profiler->mutex, NULL);

	while (status == HX_OS_OK &&
		(profiler->threads || profiler->operations)) {
		status = hx_os_condition_wait_until(
			profiler->condition, profiler->mutex, deadline, error);
		if (!profiler->threads && !profiler->operations)
			status = HX_OS_OK;
	}

	if (status == HX_OS_OK)
		profiler->finalizing = 1;

	(void)hx_os_mutex_unlock(profiler->mutex, NULL);
	if (status == HX_OS_OK)
		status = hx_os_tls_key_destroy(&profiler->key, error);

	if (status != HX_OS_OK) {
		hx_os_profile_current = guard.previous;

		return status;
	}

	guard.disposing = 1;
	record = profiler->first;

	while (record) {
		hx_os_profile_record *next = record->next;

		hx_os_memory_free(record);
		record = next;
	}

	hx_os_condition_destroy(profiler->condition);
	hx_os_mutex_destroy(profiler->mutex);
	hx_os_memory_free(profiler);
	hx_os_profile_current = guard.previous;
	*owner = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_profiler_destroy(
	hx_os_profiler **profiler, hx_os_error *error)
{
	return hx_os_profiler_close_until(profiler, HX_OS_DEADLINE_NOW, error);
}
#endif
