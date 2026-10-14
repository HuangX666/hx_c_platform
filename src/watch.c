#include "watch_private.h"

hx_os_status hx_os_watch_options_init(
	hx_os_watch_options *options, size_t capacity)
{
	hx_os_watch_options value = HX_OS_WATCH_OPTIONS_INIT;

	if (!options || capacity < sizeof(value))
		return HX_OS_INVALID;

	memcpy(options, &value, sizeof(value));

	return HX_OS_OK;
}

#if HX_OS_ENABLE_WATCH
void *hx_os_i_watch_alloc(hx_os_watch *watch, size_t bytes, hx_os_error *error)
{
	size_t cost = hx_os_i_allocation_cost(bytes, 0);

	void *memory;

	if (!cost ||
		cost > watch->options.max_memory_bytes - watch->allocated) {
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}

	memory = hx_os_i_zero(watch->context, bytes);
	if (!memory) {
		(void)hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		return NULL;
	}

	watch->allocated += cost;

	return memory;
}

void hx_os_i_watch_free(hx_os_watch *watch, void *memory)
{
	if (memory) {
		watch->allocated -= hx_os_i_allocation_bytes(memory);

		hx_os_memory_free(memory);
	}
}

hx_os_status hx_os_i_watch_root(
	hx_os_watch *watch, const char *path, hx_os_error *error)
{
	size_t length = strlen(path), invalid;

	char *copy;

	if (length >= watch->options.max_path_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (hx_os_text_validate_utf8(path, length, &invalid) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);

	copy = hx_os_i_watch_alloc(watch, length + 1, error);
	if (!copy)
		return error->status;

	memcpy(copy, path, length + 1);

	hx_os_i_watch_free(watch, watch->root);
	watch->root = copy;

	return HX_OS_OK;
}

char *hx_os_i_watch_scratch(
	hx_os_watch *watch, size_t bytes, hx_os_error *error)
{
	char *memory;

	if (bytes > watch->options.max_path_bytes) {
		(void)hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		return NULL;
	}

	if (bytes <= watch->scratch_capacity)
		return watch->scratch;

	memory = hx_os_i_watch_alloc(watch, bytes, error);
	if (!memory)
		return NULL;

	hx_os_i_watch_free(watch, watch->scratch);
	watch->scratch = memory;
	watch->scratch_capacity = bytes;

	return memory;
}

int hx_os_i_watch_stopping(hx_os_watch *watch)
{
	return hx_os_atomic_load(&watch->stopping, HX_OS_ACQUIRE) != 0;
}

void hx_os_i_watch_emit(hx_os_watch *watch, uint32_t kind, const char *path,
	const char *old_path, uint32_t flags, const hx_os_error *error)
{
	hx_os_watch_event event;
	const char *paths[2] = {path, old_path};
	size_t invalid, i;

	if (hx_os_i_watch_stopping(watch))
		return;

	memset(&event, 0, sizeof(event));
	event.size = sizeof(event);
	event.version = 1;
	event.kind = kind;
	event.flags = flags;
	event.path = path;
	event.old_path = old_path;
	if (error)
		event.error = *error;

	for (i = 0; i < 2; ++i) {
		size_t length;
		hx_os_status status;

		if (!paths[i])
			continue;

		length = strlen(paths[i]);

		status = length >= watch->options.max_path_bytes
			? HX_OS_LIMIT
			: hx_os_text_validate_utf8(paths[i], length, &invalid);
		if (status != HX_OS_OK) {
			event.kind = HX_OS_WATCH_OVERFLOW;
			event.path = event.old_path = NULL;
			(void)hx_os_i_error(&event.error, status, 0, 0);
			break;
		}
	}

	watch->function(watch, &event, watch->user);
}

static void hx_os_watch_dispose(hx_os_watch *watch)
{
	hx_os_i_watch_native_destroy(watch);
	hx_os_i_watch_free(watch, watch->root);
	hx_os_i_watch_free(watch, watch->scratch);
	hx_os_event_destroy(watch->started);
	hx_os_event_destroy(watch->gate);
	hx_os_memory_free(watch);
}

static void *hx_os_watch_worker(void *user)
{
	hx_os_watch *watch = user;
	hx_os_status status =
		hx_os_i_watch_native_start(watch, &watch->startup);

	hx_os_event_set(watch->started);
	if (status != HX_OS_OK)
		return NULL;

	status = hx_os_event_wait_until(
		watch->gate, HX_OS_DEADLINE_INFINITE, &watch->result);
	if (!watch->accepted)
		return NULL;

	if (status == HX_OS_OK)
		status = hx_os_i_watch_native_run(watch, &watch->result);
	else {
		hx_os_error ignored;

		hx_os_watch_stop(watch);
		(void)hx_os_i_watch_native_run(watch, &ignored);
		// Preserve a gate failure while still retiring native work.
		if (watch->cleanup)
			watch->cleanup(watch->user);

		return NULL;
	}

	if (hx_os_i_watch_stopping(watch))
		hx_os_error_clear(&watch->result);
	else if (status != HX_OS_OK)
		hx_os_i_watch_emit(watch, HX_OS_WATCH_ERROR, NULL, NULL, 0,
			&watch->result);

	if (watch->cleanup)
		watch->cleanup(watch->user);

	return NULL;
}
#endif

hx_os_status hx_os_watch_create_with_options(hx_os_context *context,
	const char *path, int recursive, const hx_os_watch_options *input,
	hx_os_watch_fn function, hx_os_watch_cleanup_fn cleanup, void *user,
	hx_os_watch **out, hx_os_error *error)
{
#if HX_OS_ENABLE_WATCH
	hx_os_watch_options options = HX_OS_WATCH_OPTIONS_INIT;
	hx_os_watch *watch;
	hx_os_error native;
	hx_os_status status;

	if (!out || !function || (recursive != 0 && recursive != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (input) {
		if (input->size < sizeof(options) || input->version != 1)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		options = *input;
	}

	if (options.buffer_bytes < 4096 || options.buffer_bytes > 65536 ||
		options.max_path_bytes < 2 ||
		options.max_path_bytes > 1024 * 1024 ||
		!options.max_directories || options.max_directories > 65536 ||
		!options.max_depth || options.max_depth > 128 ||
		!options.max_memory_bytes)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;

	if (hx_os_i_allocation_cost(sizeof(*watch), 0) >
		options.max_memory_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	watch = hx_os_i_zero(context, sizeof(*watch));
	if (!watch)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	watch->allocated = hx_os_i_allocation_bytes(watch);
	watch->context = context;
	watch->options = options;
	watch->function = function;
	watch->cleanup = cleanup;
	watch->user = user;
	watch->recursive = recursive;

	status = hx_os_i_watch_root(watch, path, &native);
	if (status == HX_OS_OK)
		status = hx_os_event_create(
			context, 1, 0, &watch->started, &native);

	if (status == HX_OS_OK)
		status = hx_os_event_create(
			context, 1, 0, &watch->gate, &native);

	if (status == HX_OS_OK)
		status = hx_os_i_watch_native_init(watch, &native);

	if (status == HX_OS_OK)
		status = hx_os_thread_create(context, hx_os_watch_worker, watch,
			&watch->thread, &native);

	if (status == HX_OS_OK) {
		status = hx_os_event_wait_until(
			watch->started, HX_OS_DEADLINE_INFINITE, &native);
		if (status == HX_OS_OK) {
			native = watch->startup;

			status = native.status;
		}
	}

	if (status != HX_OS_OK) {
		if (watch->thread) {
			hx_os_watch_stop(watch);
			hx_os_event_set(watch->gate);
			(void)hx_os_thread_join_until(watch->thread,
				HX_OS_DEADLINE_INFINITE, NULL, NULL);

			(void)hx_os_thread_destroy(&watch->thread, NULL);
		}

		hx_os_watch_dispose(watch);
		if (error)
			*error = native;

		return status;
	}

	watch->accepted = 1;
	*out = watch;
	hx_os_event_set(watch->gate);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)path;
	(void)recursive;
	(void)input;
	(void)function;
	(void)cleanup;
	(void)user;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_watch_create(hx_os_context *context, const char *path,
	int recursive, hx_os_watch_fn function, void *user, hx_os_watch **out,
	hx_os_error *error)
{
	return hx_os_watch_create_with_options(context, path, recursive, NULL,
		function, NULL, user, out, error);
}

void hx_os_watch_stop(hx_os_watch *watch)
{
#if HX_OS_ENABLE_WATCH
	if (watch) {
		hx_os_atomic_store(&watch->stopping, 1, HX_OS_RELEASE);
		hx_os_i_watch_native_stop(watch);
	}
#else
	(void)watch;
#endif
}

hx_os_status hx_os_watch_close_until(
	hx_os_watch **watch, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_WATCH
	hx_os_watch *value;
	hx_os_status status;
	int64_t expected = 0;

	if (!watch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*watch)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	value = *watch;
	if (!hx_os_atomic_compare_exchange(&value->close_claim, &expected, 1,
		    HX_OS_ACQUIRE, HX_OS_RELAXED))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	hx_os_watch_stop(value);

	status = hx_os_thread_join_until(value->thread, deadline, NULL, error);
	if (status != HX_OS_OK) {
		hx_os_atomic_store(&value->close_claim, 0, HX_OS_RELEASE);

		return status;
	}

	status = hx_os_thread_destroy(&value->thread, error);
	if (status != HX_OS_OK) {
		hx_os_atomic_store(&value->close_claim, 0, HX_OS_RELEASE);

		return status;
	}

	status = value->result.status;
	if (error)
		*error = value->result;

	hx_os_watch_dispose(value);
	*watch = NULL;

	return status;
#else
	(void)watch;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
