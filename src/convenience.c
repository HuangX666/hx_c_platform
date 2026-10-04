#include "internal.h"
#include "hx_os_paths.h"
#include "hx_os_wait.h"
#include "hx_settings.h"
#include "hx_os_signal.h"
#include <assert.h>

void hx_settings_string_ref_destroy(hx_settings_string_ref *ref)
{
	if (ref) {
		hx_settings_value_release(ref->value);
		memset(ref, 0, sizeof(*ref));
	}
}

void hx_settings_string_ref_copy(
	hx_settings_string_ref *out, const hx_settings_string_ref *source)
{
	assert(out && source);
	if (out != source) {
		hx_settings_value_retain(source->value);

		hx_settings_value_release(out->value);
		*out = *source;
	}
}

hx_os_status hx_settings_get_string_ref_view(hx_settings *settings,
	hx_string_view key, hx_settings_string_ref *out, hx_os_error *error)
{
	hx_settings_value *value = NULL;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_settings_get_value(settings, key, &value, error);
	if (status == HX_OS_OK)
		status = hx_settings_value_get_string_ref(value, out, error);

	hx_settings_value_release(value);

	return status;
}

hx_os_status hx_settings_get_string_ref(hx_settings *settings, const char *key,
	hx_settings_string_ref *out, hx_os_error *error)
{
	if (!key)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_settings_get_string_ref_view(
		settings, hx_string_view_from_cstr(key), out, error);
}

hx_settings_update hx_settings_update_begin(hx_settings *settings)
{
	hx_settings_update update;

	memset(&update, 0, sizeof(update));
	update.settings = settings;
#if HX_OS_ENABLE_SETTINGS
	update.error.status = settings ? HX_OS_OK : HX_OS_CLOSED;
#else
	update.error.status = HX_OS_NOT_SUPPORTED;
#endif
	return update;
}

static void hx_settings_update_record(
	hx_settings_update *update, hx_os_status status)
{
	update->error.status = status;
	if (status == HX_OS_OK && update->applied != SIZE_MAX)
		++update->applied;
}

void hx_settings_update_set_bool(
	hx_settings_update *update, const char *key, int value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_bool(
				update->settings, key, value, &update->error));
}

void hx_settings_update_set_i64(
	hx_settings_update *update, const char *key, int64_t value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_i64(
				update->settings, key, value, &update->error));
}

void hx_settings_update_set_double(
	hx_settings_update *update, const char *key, double value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_double(
				update->settings, key, value, &update->error));
}

void hx_settings_update_set_string(
	hx_settings_update *update, const char *key, const char *value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_string(
				update->settings, key, value, &update->error));
}

void hx_settings_update_set_string_view(
	hx_settings_update *update, hx_string_view key, hx_string_view value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_string_view(
				update->settings, key, value, &update->error));
}

void hx_settings_update_set_value(hx_settings_update *update,
	hx_string_view key, const hx_settings_value *value)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_set_value(
				update->settings, key, value, &update->error));
}

void hx_settings_update_unset(hx_settings_update *update, hx_string_view key)
{
	assert(update);
	if (update->error.status == HX_OS_OK)
		hx_settings_update_record(update,
			hx_settings_unset(update->settings, key,
				HX_SETTINGS_EXPLICIT, &update->error));
}

hx_os_status hx_settings_update_finish(
	const hx_settings_update *update, size_t *applied, hx_os_error *error)
{
	if (!update)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (applied)
		*applied = update->applied;

	if (error)
		*error = update->error;

	return update->error.status;
}

hx_os_calldata_edit hx_os_calldata_edit_begin(hx_os_calldata *data)
{
	hx_os_calldata_edit edit;

	memset(&edit, 0, sizeof(edit));
	edit.data = data;
#if HX_OS_ENABLE_SIGNALS || HX_OS_ENABLE_PROCEDURES
	edit.error.status = data ? HX_OS_OK : HX_OS_INVALID;
#else
	edit.error.status = HX_OS_NOT_SUPPORTED;
#endif
	return edit;
}

static void hx_os_calldata_edit_record(
	hx_os_calldata_edit *edit, hx_os_status status)
{
	edit->error.status = status;
}

void hx_os_calldata_edit_set_bool(
	hx_os_calldata_edit *edit, const char *name, int value)
{
	assert(edit);
	if (edit->error.status == HX_OS_OK)
		hx_os_calldata_edit_record(edit,
			hx_os_calldata_set_bool(
				edit->data, name, value, &edit->error));
}

void hx_os_calldata_edit_set_i64(
	hx_os_calldata_edit *edit, const char *name, int64_t value)
{
	assert(edit);
	if (edit->error.status == HX_OS_OK)
		hx_os_calldata_edit_record(edit,
			hx_os_calldata_set_i64(
				edit->data, name, value, &edit->error));
}

void hx_os_calldata_edit_set_double(
	hx_os_calldata_edit *edit, const char *name, double value)
{
	assert(edit);
	if (edit->error.status == HX_OS_OK)
		hx_os_calldata_edit_record(edit,
			hx_os_calldata_set_double(
				edit->data, name, value, &edit->error));
}

void hx_os_calldata_edit_set_string(
	hx_os_calldata_edit *edit, const char *name, hx_string_view value)
{
	assert(edit);
	if (edit->error.status == HX_OS_OK)
		hx_os_calldata_edit_record(edit,
			hx_os_calldata_set_string(
				edit->data, name, value, &edit->error));
}

void hx_os_calldata_edit_set_pointer(
	hx_os_calldata_edit *edit, const char *name, void *value)
{
	assert(edit);
	if (edit->error.status == HX_OS_OK)
		hx_os_calldata_edit_record(edit,
			hx_os_calldata_set_pointer(
				edit->data, name, value, &edit->error));
}

hx_os_status hx_os_calldata_edit_finish(
	const hx_os_calldata_edit *edit, hx_os_error *error)
{
	if (!edit)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (error)
		*error = edit->error;

	return edit->error.status;
}

hx_os_status hx_os_directory_visit_ex(hx_os_context *context, const char *path,
	size_t max_entries, hx_os_directory_visit_fn visit, void *user,
	size_t *completed, hx_os_error *error)
{
	hx_os_directory *directory = NULL;
	hx_string name;
	hx_os_status status;
	size_t count = 0;

	if (completed)
		*completed = 0;

	if (!visit || !max_entries)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_directory_open(context, path, &directory, error);
	if (status != HX_OS_OK)
		return status;

	hx_string_init(&name, context);

	status = hx_string_reserve(&name, 255, error);

	while (status == HX_OS_OK) {
		size_t required = 0;
		hx_string_view entry;
		hx_os_error callback_error;

		status = hx_os_directory_next(
			directory, name.data, name.capacity, &required, error);
		if (status == HX_OS_EOF)
			break;

		if (status == HX_OS_OUTPUT_FULL) {
			if (!required || required > 4096) {
				status =
					hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
				break;
			}

			status = hx_string_reserve(&name, required - 1, error);
			continue;
		}

		if (status != HX_OS_OK)
			break;

		if (count == max_entries) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}

		entry.data = name.data;
		entry.size = required - 1;
		hx_os_error_clear(&callback_error);
		++count;
		if (completed)
			*completed = count;

		status = visit(entry, user, &callback_error);
		if (error) {
			*error = callback_error;
			error->status = status;
		}
	}

	hx_string_destroy(&name);
	hx_os_directory_close(directory);

	return status == HX_OS_EOF ? hx_os_i_error(error, HX_OS_OK, 0, 0)
				   : status;
}

hx_os_status hx_os_directory_visit(const char *path,
	hx_os_directory_visit_fn visit, void *user, hx_os_error *error)
{
	return hx_os_directory_visit_ex(
		NULL, path, 65536, visit, user, NULL, error);
}

hx_os_status hx_os_thread_pool_shutdown_ex(hx_os_thread_pool **pool, int mode,
	hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!pool)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*pool)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	status = hx_os_thread_pool_close_until(*pool, mode, deadline, error);

	return status == HX_OS_OK ? hx_os_thread_pool_destroy(pool, error)
				  : status;
}

hx_os_status hx_os_thread_pool_shutdown(
	hx_os_thread_pool **pool, hx_os_deadline deadline, hx_os_error *error)
{
	return hx_os_thread_pool_shutdown_ex(
		pool, HX_OS_POOL_DRAIN, deadline, error);
}

hx_os_status hx_os_repeat_until(uint64_t interval_ms, size_t ticks,
	hx_os_repeat_fn function, void *user, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	uint64_t now, interval, due;
	size_t count = 0;
	hx_os_status status;

	if (completed)
		*completed = 0;

	if (!interval_ms || interval_ms > (UINT64_MAX - 1) / 1000000 ||
		!ticks || !function)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	interval = interval_ms * 1000000;

	status = hx_os_monotonic_read(&now, error);
	if (status != HX_OS_OK)
		return status;

	if (now > UINT64_MAX - 1 - interval)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	due = now + interval;

	for (;;) {
		uint64_t steps;

		if (due >= deadline && deadline != HX_OS_DEADLINE_INFINITE) {
			status = hx_os_sleep_until(deadline, error);

			return status == HX_OS_OK
				? hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0)
				: status;
		}

		status = hx_os_sleep_until(due, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		if (now >= deadline && deadline != HX_OS_DEADLINE_INFINITE)
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);

		{
			hx_os_error callback_error;

			hx_os_error_clear(&callback_error);

			status = function(user, &callback_error);
			if (error)
				*error = callback_error;
		}

		++count;
		if (completed)
			*completed = count;

		if (status == HX_OS_EOF ||
			(status == HX_OS_OK && count == ticks))
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (status != HX_OS_OK) {
			if (error && error->status != status)
				(void)hx_os_i_error(error, status, 0, 0);

			return status;
		}

		status = hx_os_monotonic_read(&now, error);
		if (status != HX_OS_OK)
			return status;

		steps = (now - due) / interval + 1;
		if (steps > (UINT64_MAX - 1 - due) / interval)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

		due += steps * interval;
	}
}

hx_os_status hx_os_repeat(uint64_t interval_ms, size_t ticks,
	hx_os_repeat_fn function, void *user, hx_os_error *error)
{
	return hx_os_repeat_until(interval_ms, ticks, function, user,
		HX_OS_DEADLINE_INFINITE, NULL, error);
}

hx_os_status hx_os_path_config_file(const char *application, const char *name,
	hx_string *out, hx_os_error *error)
{
	hx_string path;
	hx_os_status status;

	if (!out || !application || !*application || !name || !*name ||
		strpbrk(application, "/\\:") || strpbrk(name, "/\\:") ||
		!strcmp(application, ".") || !strcmp(application, "..") ||
		!strcmp(name, ".") || !strcmp(name, ".."))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_string_init(&path, out->context);

	status = hx_os_path_config_home_string(&path, error);
	if (status == HX_OS_OK && path.size &&
		path.data[path.size - 1] != '/' &&
		path.data[path.size - 1] != '\\')
		status = hx_string_append_cstr(&path, "/", error);
	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, application, error);

	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, "/", error);

	if (status == HX_OS_OK)
		status = hx_string_append_cstr(&path, name, error);

	if (status == HX_OS_OK)
		hx_string_move(out, &path);

	hx_string_destroy(&path);

	return status;
}

hx_os_status hx_os_file_read_text_encoded(const char *path,
	hx_os_encoding encoding, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_FILE_UTILS
	void *bytes = NULL;
	size_t size = 0, skip = 0;
	hx_os_status status;
	hx_os_encoding bom;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!hx_os_encoding_supported(encoding))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	status = hx_os_file_load(
		out->context, path, 16u * 1024u * 1024u, &bytes, &size, error);
	if (status == HX_OS_OK) {
		unsigned char expected[4];
		size_t expected_bytes = 0;
		hx_os_status detected;

		/* Explicit UTF-16LE plus U+0000 shares the UTF-32LE signature.
		 * Prefer the caller's matching marker before generic detection.
		 */
		if (hx_os_text_bom_bytes(encoding, expected, &expected_bytes) ==
				HX_OS_OK &&
			expected_bytes <= size &&
			!memcmp(bytes, expected, expected_bytes)) {
			bom = encoding;
			skip = expected_bytes;
			detected = HX_OS_OK;
		}
		else
			detected =
				hx_os_text_bom_detect(bytes, size, &bom, &skip);

		if (detected == HX_OS_OK && bom != encoding)
			status = hx_os_i_error(
				error, HX_OS_INVALID_ENCODING, 0, 0);
		else if (detected != HX_OS_OK)
			skip = 0;

		if (status == HX_OS_OK)
			status = hx_string_assign_encoded(out, encoding,
				bytes ? (const unsigned char *)bytes + skip
				      : NULL,
				size - skip, error);
	}

	hx_os_memory_free(bytes);

	return status;
#else
	(void)path;
	(void)encoding;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
