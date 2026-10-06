#include "settings_private.h"

hx_os_status hx_settings_apply_value(hx_settings *settings,
	const hx_settings_value *object, size_t *applied, hx_os_error *error)
{
	(void)settings;
	(void)object;
	if (applied)
		*applied = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_apply(hx_settings *settings, hx_settings *source,
	size_t *applied, hx_os_error *error)
{
	(void)source;
	return hx_settings_apply_value(settings, NULL, applied, error);
}

void hx_settings_value_retain(hx_settings_value *value)
{
	(void)value;
}

void hx_settings_value_release(hx_settings_value *value)
{
	(void)value;
}

int hx_settings_value_kind(const hx_settings_value *value)
{
	(void)value;

	return -1;
}

size_t hx_settings_value_count(const hx_settings_value *value)
{
	(void)value;

	return 0;
}

hx_os_status hx_settings_value_null(
	hx_os_context *context, hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_bool(hx_os_context *context, int value,
	hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_i64(hx_os_context *context, int64_t value,
	hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_double(hx_os_context *context, double value,
	hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_string(hx_os_context *context,
	hx_string_view value, hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_pointer(hx_os_context *context, void *value,
	hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_array(hx_os_context *context,
	const hx_settings_value *const *items, size_t count,
	hx_settings_value **out, hx_os_error *error)
{
	(void)context;
	(void)items;
	(void)count;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_object(hx_os_context *context,
	const hx_settings_pair *items, size_t count, hx_settings_value **out,
	hx_os_error *error)
{
	(void)context;
	(void)items;
	(void)count;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_copy(hx_os_context *context,
	const hx_settings_value *value, hx_settings_value **out,
	hx_os_error *error)
{
	(void)context;
	(void)value;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_bool(
	const hx_settings_value *value, int *out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_i64(
	const hx_settings_value *value, int64_t *out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_double(
	const hx_settings_value *value, double *out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_string(
	const hx_settings_value *value, hx_string *out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_string_ref(const hx_settings_value *value,
	hx_settings_string_ref *out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_get_pointer(
	const hx_settings_value *value, void **out, hx_os_error *error)
{
	(void)value;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_at(const hx_settings_value *value, size_t index,
	hx_settings_value **out, hx_os_error *error)
{
	(void)value;
	(void)index;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_member(const hx_settings_value *value,
	hx_string_view name, hx_settings_value **out, hx_os_error *error)
{
	(void)value;
	(void)name;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_value_name_at(const hx_settings_value *value,
	size_t index, hx_string *out, hx_os_error *error)
{
	(void)value;
	(void)index;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_create(
	hx_os_context *context, hx_settings **out, hx_os_error *error)
{
	(void)context;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_create_with_options(hx_os_context *context,
	const hx_settings_options *options, hx_settings **out,
	hx_os_error *error)
{
	(void)context;
	(void)options;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_copy(hx_os_context *context, hx_settings *source,
	hx_settings **out, hx_os_error *error)
{
	(void)context;
	(void)source;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_value(hx_settings *settings, hx_string_view key,
	const hx_settings_value *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_value(hx_settings *settings,
	hx_string_view key, const hx_settings_value *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_value(hx_settings *settings, hx_string_view key,
	hx_settings_value **out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_unset(hx_settings *settings, hx_string_view key,
	int layer, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)layer;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_snapshot(hx_settings *settings, int layer,
	hx_settings_value **out, hx_os_error *error)
{
	(void)settings;
	(void)layer;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_i64(hx_settings *settings, const char *key,
	int64_t value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_double(hx_settings *settings, const char *key,
	double value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_string(hx_settings *settings, const char *key,
	const char *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_pointer(
	hx_settings *settings, const char *key, void *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_null(
	hx_settings *settings, const char *key, hx_os_error *error)
{
	(void)settings;
	(void)key;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_array(hx_settings *settings, const char *key,
	const hx_settings_value *const *items, size_t count, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)items;
	(void)count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_object(hx_settings *settings, const char *key,
	const hx_settings_value *object, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)object;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_i64(hx_settings *settings, const char *key,
	int64_t value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_double(hx_settings *settings,
	const char *key, double value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_string(hx_settings *settings,
	const char *key, const char *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_pointer(
	hx_settings *settings, const char *key, void *value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_null(
	hx_settings *settings, const char *key, hx_os_error *error)
{
	(void)settings;
	(void)key;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_array(hx_settings *settings,
	const char *key, const hx_settings_value *const *items, size_t count,
	hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)items;
	(void)count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_object(hx_settings *settings,
	const char *key, const hx_settings_value *object, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)object;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_bool(
	hx_settings *settings, const char *key, int *out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_i64(hx_settings *settings, const char *key,
	int64_t *out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_double(
	hx_settings *settings, const char *key, double *out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_string(hx_settings *settings, const char *key,
	hx_string *out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_get_pointer(
	hx_settings *settings, const char *key, void **out, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_close_until(
	hx_settings **settings, hx_os_deadline deadline, hx_os_error *error)
{
	(void)settings;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_destroy(hx_settings **settings, hx_os_error *error)
{
	(void)settings;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_import(hx_settings *settings,
	const hx_os_config_document *document, hx_os_error *error)
{
	(void)settings;
	(void)document;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_to_config(hx_settings *settings, int layer,
	const hx_os_config_options *options, hx_os_config_document **out,
	hx_os_error *error)
{
	(void)settings;
	(void)layer;
	(void)options;
	(void)out;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_load(hx_settings *settings, int format,
	const char *path, const hx_os_config_options *options,
	hx_os_config_error *error)
{
	(void)settings;
	(void)format;
	(void)path;
	(void)options;
	if (error) {
		memset(error, 0, sizeof(*error));
		error->byte_offset = SIZE_MAX;
		(void)hx_os_i_error(&error->detail, HX_OS_NOT_SUPPORTED, 0, 0);
	}

	return HX_OS_NOT_SUPPORTED;
}

hx_os_status hx_settings_serialize(hx_settings *settings, int format,
	hx_string *out, const hx_os_config_options *options, hx_os_error *error)
{
	(void)settings;
	(void)format;
	(void)out;
	(void)options;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_save(hx_settings *settings, int format,
	const char *path, const hx_os_config_options *options,
	hx_os_error *error)
{
	(void)settings;
	(void)format;
	(void)path;
	(void)options;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_to_json(
	hx_settings *settings, hx_string *out, hx_os_error *error)
{
	(void)settings;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_save_json(
	hx_settings *settings, const char *path, hx_os_error *error)
{
	(void)settings;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_load_json(
	hx_settings *settings, const char *path, hx_os_error *error)
{
	(void)settings;
	(void)path;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_string_view(hx_settings *settings,
	hx_string_view key, hx_string_view value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}

hx_os_status hx_settings_set_default_string_view(hx_settings *settings,
	hx_string_view key, hx_string_view value, hx_os_error *error)
{
	(void)settings;
	(void)key;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
}
