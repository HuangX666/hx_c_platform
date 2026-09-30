#include "settings_private.h"
hx_os_status hx_settings_options_init(
	hx_settings_options *options, size_t capacity)
{
	const hx_settings_options defaults = HX_SETTINGS_OPTIONS_INIT;
	if (!options)
		return HX_OS_INVALID;
	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;
	*options = defaults;
	return HX_OS_OK;
}
#if HX_OS_ENABLE_SETTINGS
#if defined(_MSC_VER)
static __declspec(thread) hx_settings_frame *hx_settings_current;
#else
static __thread hx_settings_frame *hx_settings_current;
#endif
hx_settings_limits hx_settings_i_limits(const hx_settings *settings)
{
	hx_settings_limits limits;
	limits.nodes = settings->options.max_nodes;
	limits.memory = settings->options.max_memory_bytes;
	limits.depth = settings->options.max_depth;
	return limits;
}
static int hx_settings_self(hx_settings *settings)
{
	hx_settings_frame *frame;
	for (frame = hx_settings_current; frame; frame = frame->previous)
		if (frame->settings == settings)
			return 1;
	return 0;
}
static int hx_settings_can_notify(hx_settings *settings)
{
	hx_settings_frame *frame;
	size_t count = 0;
	for (frame = hx_settings_current; frame; frame = frame->previous)
		if (frame->settings == settings && frame->notifying)
			++count;
	return count < settings->options.max_callback_depth;
}
hx_os_status hx_settings_i_begin(
	hx_settings *settings, hx_settings_frame *frame, hx_os_error *error)
{
	hx_os_status status = HX_OS_OK;
	hx_settings_frame *active;
	if (!settings)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (active = hx_settings_current; active; active = active->previous)
		if (active->settings == settings && active->notifying < 0)
			return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	(void)hx_os_mutex_lock(settings->mutex, NULL);
	if (settings->closing)
		status = HX_OS_CLOSED;
	else if (settings->operations ==
		settings->options.max_active_operations)
		status = HX_OS_LIMIT;
	else {
		++settings->operations;
		frame->settings = settings;
		frame->notifying = 0;
		frame->roots[0] = settings->roots[0];
		frame->roots[1] = settings->roots[1];
		frame->revision = settings->revision;
		hx_settings_value_retain(frame->roots[0]);
		hx_settings_value_retain(frame->roots[1]);
		frame->previous = hx_settings_current;
	}
	(void)hx_os_mutex_unlock(settings->mutex, NULL);
	if (status == HX_OS_OK)
		hx_settings_current = frame;
	return hx_os_i_error(error, status, 0, 0);
}
void hx_settings_i_end(hx_settings_frame *frame)
{
	hx_settings *settings = frame->settings;
	hx_settings_value_release(frame->roots[0]);
	hx_settings_value_release(frame->roots[1]);
	(void)hx_os_mutex_lock(settings->mutex, NULL);
	--settings->operations;
	if (settings->closing && !settings->operations)
		hx_os_condition_notify_all(settings->condition);
	(void)hx_os_mutex_unlock(settings->mutex, NULL);
	hx_settings_current = frame->previous;
}
static void hx_settings_refresh(hx_settings_frame *frame)
{
	hx_settings *settings = frame->settings;
	hx_settings_value *old[2] = {frame->roots[0], frame->roots[1]};
	(void)hx_os_mutex_lock(settings->mutex, NULL);
	frame->roots[0] = settings->roots[0];
	frame->roots[1] = settings->roots[1];
	frame->revision = settings->revision;
	hx_settings_value_retain(frame->roots[0]);
	hx_settings_value_retain(frame->roots[1]);
	(void)hx_os_mutex_unlock(settings->mutex, NULL);
	hx_settings_value_release(old[0]);
	hx_settings_value_release(old[1]);
}
static void hx_settings_notify(
	hx_settings_frame *frame, const hx_settings_change *change)
{
	hx_settings *settings = frame->settings;
	if (!settings->options.on_change)
		return;
	frame->notifying = 1;
	settings->options.on_change(settings, change, settings->options.user);
	frame->notifying = 0;
}
hx_os_status hx_settings_create(
	hx_os_context *context, hx_settings **out, hx_os_error *error)
{
	return hx_settings_create_with_options(context, NULL, out, error);
}
hx_os_status hx_settings_create_with_options(hx_os_context *context,
	const hx_settings_options *options, hx_settings **out,
	hx_os_error *error)
{
	const hx_settings_options defaults = HX_SETTINGS_OPTIONS_INIT;
	hx_settings *settings;
	hx_settings_limits limits;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!options)
		options = &defaults;
	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_nodes || options->max_nodes > 65536 ||
		!options->max_memory_bytes ||
		options->max_memory_bytes > 16u * 1024u * 1024u ||
		!options->max_depth || options->max_depth > 64 ||
		!options->max_active_operations ||
		options->max_active_operations > 65535 ||
		!options->max_retries || options->max_retries > 64 ||
		!options->max_callback_depth ||
		options->max_callback_depth > 64)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	settings = hx_os_i_zero(context, sizeof(*settings));
	if (!settings)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	settings->context = context;
	settings->options = *options;
	limits = hx_settings_i_limits(settings);
	status = hx_settings_i_object(
		context, NULL, 0, &limits, &settings->roots[0], error);
	if (status == HX_OS_OK) {
		settings->roots[1] = settings->roots[0];
		hx_settings_value_retain(settings->roots[1]);
		status =
			hx_os_mutex_create(context, 0, &settings->mutex, error);
	}
	if (status == HX_OS_OK)
		status = hx_os_condition_create(
			context, &settings->condition, error);
	if (status != HX_OS_OK) {
		if (settings->mutex)
			hx_os_mutex_destroy(settings->mutex);
		hx_settings_value_release(settings->roots[0]);
		hx_settings_value_release(settings->roots[1]);
		hx_os_memory_free(settings);
		return status;
	}
	*out = settings;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
/* prepared is independently held by the caller, so every retry may retain it
 * in a new root without invoking a caller allocator while the mutex is held.
 */
static hx_os_status hx_settings_store(hx_settings_frame *frame,
	hx_string_view key, hx_settings_value *prepared, int layer,
	hx_os_error *error)
{
	hx_settings *settings = frame->settings;
	hx_settings_limits limits = hx_settings_i_limits(settings);
	size_t attempt;
	hx_os_status status = hx_settings_i_key(key, error);
	if (status != HX_OS_OK)
		return status;
	if (layer != HX_SETTINGS_EXPLICIT && layer != HX_SETTINGS_DEFAULTS)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!hx_settings_can_notify(settings))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (attempt = 0; attempt < settings->options.max_retries; ++attempt) {
		hx_settings_value *root = NULL, *old = NULL;
		hx_settings_change change;
		const hx_settings_entry *entry;
		status = hx_settings_i_replace(settings->context,
			frame->roots[layer], key, prepared, &limits, &root,
			error);
		if (status != HX_OS_OK)
			return status;
		(void)hx_os_mutex_lock(settings->mutex, NULL);
		if (settings->revision == UINT64_MAX)
			status = HX_OS_LIMIT;
		else if (settings->revision != frame->revision)
			status = HX_OS_BUSY;
		else {
			old = settings->roots[layer];
			settings->roots[layer] = root;
			hx_settings_value_retain(root);
			change.revision = ++settings->revision;
			status = HX_OS_OK;
		}
		(void)hx_os_mutex_unlock(settings->mutex, NULL);
		if (status == HX_OS_OK) {
			entry = hx_settings_i_find(
				prepared ? root : frame->roots[layer], key);
			change.key = entry->name;
			change.before =
				hx_settings_i_member(frame->roots[layer], key);
			change.after = prepared;
			change.kind =
				prepared ? HX_SETTINGS_SET : HX_SETTINGS_UNSET;
			change.layer = (uint32_t)layer;
			hx_settings_value_release(old);
			hx_settings_notify(frame, &change);
			hx_settings_value_release(root);
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
		hx_settings_value_release(root);
		if (status != HX_OS_BUSY)
			return hx_os_i_error(error, status, 0, 0);
		hx_settings_refresh(frame);
	}
	return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
}
hx_os_status hx_settings_i_commit_root(
	hx_settings_frame *frame, hx_settings_value *root, hx_os_error *error)
{
	hx_settings *settings = frame->settings;
	hx_settings_value *old;
	hx_settings_change change;
	if (!hx_settings_can_notify(settings))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	(void)hx_os_mutex_lock(settings->mutex, NULL);
	if (settings->revision == UINT64_MAX) {
		(void)hx_os_mutex_unlock(settings->mutex, NULL);
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	old = settings->roots[0];
	settings->roots[0] = root;
	hx_settings_value_retain(root);
	change.revision = ++settings->revision;
	(void)hx_os_mutex_unlock(settings->mutex, NULL);
	change.key.data = NULL;
	change.key.size = 0;
	change.before = old;
	change.after = root;
	change.kind = HX_SETTINGS_REPLACE;
	change.layer = HX_SETTINGS_EXPLICIT;
	hx_settings_notify(frame, &change);
	hx_settings_value_release(old);
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
static hx_os_status hx_settings_set_layer(hx_settings *settings,
	hx_string_view key, const hx_settings_value *value, int layer,
	hx_os_error *error)
{
	hx_settings_frame frame;
	hx_settings_limits limits;
	hx_settings_value *prepared = NULL;
	hx_os_status status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	limits = hx_settings_i_limits(settings);
	status = hx_settings_i_clone(
		settings->context, value, &limits, &prepared, error);
	if (status == HX_OS_OK)
		status = hx_settings_store(&frame, key, prepared, layer, error);
	hx_settings_value_release(prepared);
	hx_settings_i_end(&frame);
	return status;
}
hx_os_status hx_settings_set_value(hx_settings *settings, hx_string_view key,
	const hx_settings_value *value, hx_os_error *error)
{
	return hx_settings_set_layer(
		settings, key, value, HX_SETTINGS_EXPLICIT, error);
}
hx_os_status hx_settings_set_default_value(hx_settings *settings,
	hx_string_view key, const hx_settings_value *value, hx_os_error *error)
{
	return hx_settings_set_layer(
		settings, key, value, HX_SETTINGS_DEFAULTS, error);
}
hx_os_status hx_settings_unset(hx_settings *settings, hx_string_view key,
	int layer, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_os_status status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_store(&frame, key, NULL, layer, error);
	hx_settings_i_end(&frame);
	return status;
}
static const hx_settings_value *hx_settings_effective(
	hx_settings_frame *frame, hx_string_view key)
{
	const hx_settings_value *value =
		hx_settings_i_member(frame->roots[0], key);
	return value ? value : hx_settings_i_member(frame->roots[1], key);
}
hx_os_status hx_settings_get_value(hx_settings *settings, hx_string_view key,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_frame frame;
	const hx_settings_value *value;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_i_key(key, error);
	if (status == HX_OS_OK) {
		value = hx_settings_effective(&frame, key);
		if (!value)
			status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);
		else {
			hx_settings_value_retain((hx_settings_value *)value);
			*out = (hx_settings_value *)value;
		}
	}
	hx_settings_i_end(&frame);
	return status;
}
hx_os_status hx_settings_snapshot(hx_settings *settings, int layer,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_settings_limits limits;
	hx_settings_value *root = NULL;
	hx_os_status status;
	if (!out || layer < HX_SETTINGS_EXPLICIT ||
		layer > HX_SETTINGS_EFFECTIVE)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	if (layer != HX_SETTINGS_EFFECTIVE) {
		root = frame.roots[layer];
		hx_settings_value_retain(root);
	}
	else {
		limits = hx_settings_i_limits(settings);
		status = hx_settings_i_merge(settings->context, frame.roots[0],
			frame.roots[1], &limits, &root, error);
	}
	if (status == HX_OS_OK)
		*out = root;
	hx_settings_i_end(&frame);
	return status;
}
hx_os_status hx_settings_copy(hx_os_context *context, hx_settings *source,
	hx_settings **out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_settings *copy = NULL;
	hx_settings_options options;
	hx_settings_limits limits;
	hx_settings_value *roots[2] = {NULL, NULL};
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_settings_i_begin(source, &frame, error);
	if (status != HX_OS_OK)
		return status;
	options = source->options;
	options.on_change = NULL;
	options.cleanup = NULL;
	options.user = NULL;
	limits = hx_settings_i_limits(source);
	status = hx_settings_create_with_options(
		context, &options, &copy, error);
	if (status == HX_OS_OK)
		status = hx_settings_i_clone(
			context, frame.roots[0], &limits, &roots[0], error);
	if (status == HX_OS_OK)
		status = hx_settings_i_clone(
			context, frame.roots[1], &limits, &roots[1], error);
	if (status == HX_OS_OK) {
		hx_settings_value_release(copy->roots[0]);
		hx_settings_value_release(copy->roots[1]);
		copy->roots[0] = roots[0];
		copy->roots[1] = roots[1];
		*out = copy;
	}
	else {
		hx_settings_value_release(roots[0]);
		hx_settings_value_release(roots[1]);
		(void)hx_settings_destroy(&copy, NULL);
	}
	hx_settings_i_end(&frame);
	return status;
}
hx_os_status hx_settings_close_until(
	hx_settings **owner, hx_os_deadline deadline, hx_os_error *error)
{
	hx_settings *settings;
	hx_settings_frame cleanup;
	hx_os_status status = HX_OS_OK;
	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	settings = *owner;
	if (!settings)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (hx_settings_self(settings))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	(void)hx_os_mutex_lock(settings->mutex, NULL);
	settings->closing = 1;
	while (status == HX_OS_OK && settings->operations) {
		status = hx_os_condition_wait_until(
			settings->condition, settings->mutex, deadline, error);
		if (!settings->operations)
			status = HX_OS_OK;
	}
	(void)hx_os_mutex_unlock(settings->mutex, NULL);
	if (status != HX_OS_OK)
		return status;
	memset(&cleanup, 0, sizeof(cleanup));
	cleanup.notifying = -1;
	cleanup.settings = settings;
	cleanup.previous = hx_settings_current;
	hx_settings_current = &cleanup;
	if (settings->options.cleanup)
		settings->options.cleanup(settings->options.user);
	hx_settings_value_release(settings->roots[0]);
	hx_settings_value_release(settings->roots[1]);
	hx_os_condition_destroy(settings->condition);
	hx_os_mutex_destroy(settings->mutex);
	hx_os_memory_free(settings);
	hx_settings_current = cleanup.previous;
	*owner = NULL;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_settings_destroy(hx_settings **settings, hx_os_error *error)
{
	return hx_settings_close_until(settings, HX_OS_DEADLINE_NOW, error);
}

static hx_os_status hx_settings_set_scalar_view(hx_settings *settings,
	hx_string_view key, int kind, hx_settings_scalar scalar,
	hx_string_view text, int layer, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_settings_limits limits;
	hx_settings_value *value = NULL;
	hx_os_status status;
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	limits = hx_settings_i_limits(settings);
	status = hx_settings_i_scalar(
		settings->context, kind, scalar, text, &limits, &value, error);
	if (status == HX_OS_OK)
		status = hx_settings_store(&frame, key, value, layer, error);
	hx_settings_value_release(value);
	hx_settings_i_end(&frame);
	return status;
}
static hx_os_status hx_settings_set_scalar(hx_settings *settings,
	const char *key, int kind, hx_settings_scalar scalar,
	hx_string_view text, int layer, hx_os_error *error)
{
	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_settings_set_scalar_view(settings,
		hx_string_view_from_cstr(key), kind, scalar, text, layer,
		error);
}

hx_os_status hx_settings_set_string_view(hx_settings *settings,
	hx_string_view key, hx_string_view value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	return hx_settings_set_scalar_view(settings, key, HX_SETTINGS_STRING,
		scalar, value, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_default_string_view(hx_settings *settings,
	hx_string_view key, hx_string_view value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	return hx_settings_set_scalar_view(settings, key, HX_SETTINGS_STRING,
		scalar, value, HX_SETTINGS_DEFAULTS, error);
}
static hx_os_status hx_settings_set_array_layer(hx_settings *settings,
	const char *key, const hx_settings_value *const *items, size_t count,
	int layer, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_settings_limits limits;
	hx_settings_value *value = NULL;
	hx_os_status status;
	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	limits = hx_settings_i_limits(settings);
	status = hx_settings_i_array(
		settings->context, items, count, &limits, &value, error);
	if (status == HX_OS_OK)
		status = hx_settings_store(&frame,
			hx_string_view_from_cstr(key), value, layer, error);
	hx_settings_value_release(value);
	hx_settings_i_end(&frame);
	return status;
}

hx_os_status hx_settings_set_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_BOOL, scalar,
		empty, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_i64(hx_settings *settings, const char *key,
	int64_t value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_I64, scalar,
		empty, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_double(hx_settings *settings, const char *key,
	double value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.real = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_DOUBLE, scalar,
		empty, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_pointer(
	hx_settings *settings, const char *key, void *value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.pointer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_POINTER,
		scalar, empty, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_null(
	hx_settings *settings, const char *key, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_NULL, scalar,
		empty, HX_SETTINGS_EXPLICIT, error);
}
hx_os_status hx_settings_set_string(hx_settings *settings, const char *key,
	const char *value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_STRING, scalar,
		hx_string_view_from_cstr(value), HX_SETTINGS_EXPLICIT, error);
}
hx_os_status hx_settings_set_array(hx_settings *settings, const char *key,
	const hx_settings_value *const *items, size_t count, hx_os_error *error)
{
	return hx_settings_set_array_layer(
		settings, key, items, count, HX_SETTINGS_EXPLICIT, error);
}
hx_os_status hx_settings_set_object(hx_settings *settings, const char *key,
	const hx_settings_value *object, hx_os_error *error)
{
	if (!key || !object)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (object->kind != HX_SETTINGS_OBJECT)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	return hx_settings_set_layer(settings, hx_string_view_from_cstr(key),
		object, HX_SETTINGS_EXPLICIT, error);
}

hx_os_status hx_settings_set_default_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_BOOL, scalar,
		empty, HX_SETTINGS_DEFAULTS, error);
}

hx_os_status hx_settings_set_default_i64(hx_settings *settings, const char *key,
	int64_t value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.integer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_I64, scalar,
		empty, HX_SETTINGS_DEFAULTS, error);
}

hx_os_status hx_settings_set_default_double(hx_settings *settings,
	const char *key, double value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.real = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_DOUBLE, scalar,
		empty, HX_SETTINGS_DEFAULTS, error);
}

hx_os_status hx_settings_set_default_pointer(
	hx_settings *settings, const char *key, void *value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	scalar.pointer = value;
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_POINTER,
		scalar, empty, HX_SETTINGS_DEFAULTS, error);
}

hx_os_status hx_settings_set_default_null(
	hx_settings *settings, const char *key, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view empty = {NULL, 0};
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_NULL, scalar,
		empty, HX_SETTINGS_DEFAULTS, error);
}
hx_os_status hx_settings_set_default_string(hx_settings *settings,
	const char *key, const char *value, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	if (!value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_settings_set_scalar(settings, key, HX_SETTINGS_STRING, scalar,
		hx_string_view_from_cstr(value), HX_SETTINGS_DEFAULTS, error);
}
hx_os_status hx_settings_set_default_array(hx_settings *settings,
	const char *key, const hx_settings_value *const *items, size_t count,
	hx_os_error *error)
{
	return hx_settings_set_array_layer(
		settings, key, items, count, HX_SETTINGS_DEFAULTS, error);
}
hx_os_status hx_settings_set_default_object(hx_settings *settings,
	const char *key, const hx_settings_value *object, hx_os_error *error)
{
	if (!key || !object)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (object->kind != HX_SETTINGS_OBJECT)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	return hx_settings_set_layer(settings, hx_string_view_from_cstr(key),
		object, HX_SETTINGS_DEFAULTS, error);
}

hx_os_status hx_settings_get_bool(
	hx_settings *settings, const char *key, int *out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_string_view name;
	hx_os_status status;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	name = hx_string_view_from_cstr(key);
	status = hx_settings_i_key(name, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_bool(
			hx_settings_effective(&frame, name), out, error);
	hx_settings_i_end(&frame);
	return status;
}

hx_os_status hx_settings_get_i64(hx_settings *settings, const char *key,
	int64_t *out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_string_view name;
	hx_os_status status;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	name = hx_string_view_from_cstr(key);
	status = hx_settings_i_key(name, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_i64(
			hx_settings_effective(&frame, name), out, error);
	hx_settings_i_end(&frame);
	return status;
}

hx_os_status hx_settings_get_double(
	hx_settings *settings, const char *key, double *out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_string_view name;
	hx_os_status status;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	name = hx_string_view_from_cstr(key);
	status = hx_settings_i_key(name, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_double(
			hx_settings_effective(&frame, name), out, error);
	hx_settings_i_end(&frame);
	return status;
}

hx_os_status hx_settings_get_pointer(
	hx_settings *settings, const char *key, void **out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_string_view name;
	hx_os_status status;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	name = hx_string_view_from_cstr(key);
	status = hx_settings_i_key(name, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_pointer(
			hx_settings_effective(&frame, name), out, error);
	hx_settings_i_end(&frame);
	return status;
}

hx_os_status hx_settings_get_string(hx_settings *settings, const char *key,
	hx_string *out, hx_os_error *error)
{
	hx_settings_frame frame;
	hx_string_view name;
	hx_os_status status;
	if (!key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	name = hx_string_view_from_cstr(key);
	status = hx_settings_i_key(name, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_string(
			hx_settings_effective(&frame, name), out, error);
	hx_settings_i_end(&frame);
	return status;
}
#endif
