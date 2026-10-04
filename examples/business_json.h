#ifndef HX_EXAMPLE_BUSINESS_JSON_H
#define HX_EXAMPLE_BUSINESS_JSON_H

#include <hx_config.h>
#include <hx_settings.h>
#include <stdio.h>
#include <string.h>

enum { JSON_MESSAGE_CAPACITY = 1024 };

typedef struct json_field {
	const char *key;
	hx_os_config_slice text;
	int64_t number;
	int kind;
} json_field;

static inline json_field json_int(const char *key, int64_t number)
{
	json_field field = {key, {NULL, 0}, number, HX_OS_CONFIG_INTEGER};
	return field;
}

static inline json_field json_bool(const char *key, int value)
{
	json_field field = {key, {NULL, 0}, value, HX_OS_CONFIG_BOOL};
	return field;
}

static inline json_field json_slice(const char *key, hx_os_config_slice text)
{
	json_field field = {key, {NULL, 0}, 0, HX_OS_CONFIG_STRING};
	field.text = text;
	return field;
}

static inline json_field json_text(const char *key, const char *text)
{
	hx_os_config_slice slice = {text, strlen(text)};
	return json_slice(key, slice);
}

static inline void json_limits(hx_os_config_options *options)
{
	hx_os_config_options_init(options);
	options->backend = HX_OS_CONFIG_BUILTIN;
	options->duplicates = HX_OS_CONFIG_DUPLICATE_ERROR;
	options->max_input_bytes = JSON_MESSAGE_CAPACITY - 1;
	options->max_memory_bytes = 64 * 1024;
	options->max_nodes = 64;
	options->max_depth = 8;
	options->max_work = 32768;
}

/* Only JSON encoding is shared. Each transport owns its schema and logic.
 * The existing serializer escapes strings, including quotes and backslashes.
 */
static inline int json_encode(
	const json_field *fields, size_t count, hx_string *out)
{
	hx_settings *object = NULL;
	hx_settings_options settings_options;
	hx_os_config_options options;
	hx_os_error error = {0};
	hx_os_status status;
	size_t index;
	(void)hx_settings_options_init(
		&settings_options, sizeof(settings_options));
	settings_options.max_nodes = 64;
	settings_options.max_memory_bytes = 64 * 1024;
	settings_options.max_depth = 8;
	json_limits(&options);
	status = hx_settings_create_with_options(
		NULL, &settings_options, &object, &error);
	for (index = 0; status == HX_OS_OK && index < count; ++index) {
		const json_field *field = &fields[index];
		if (field->kind == HX_OS_CONFIG_INTEGER)
			status = hx_settings_set_i64(
				object, field->key, field->number, &error);
		else if (field->kind == HX_OS_CONFIG_BOOL)
			status = hx_settings_set_bool(
				object, field->key, field->number != 0, &error);
		else {
			hx_string_view key = {field->key, strlen(field->key)};
			hx_string_view value = {
				field->text.data, field->text.bytes};
			status = hx_settings_set_string_view(
				object, key, value, &error);
		}
	}
	if (status == HX_OS_OK)
		status = hx_settings_serialize(
			object, HX_OS_CONFIG_JSON, out, &options, &error);
	if (hx_settings_destroy(&object, NULL) != HX_OS_OK)
		status = HX_OS_BUSY;
	if (status != HX_OS_OK)
		fprintf(stderr, "JSON encode failed: %s\n",
			hx_os_status_name(status));
	return status == HX_OS_OK;
}

/* Own the document until all borrowed nodes and slices are no longer used.
 * Reject non-object roots and duplicate keys; never parse with sscanf.
 */
static inline hx_os_config_document *json_decode(const void *data, size_t bytes)
{
	hx_os_config_document *document = NULL;
	hx_os_config_options options;
	hx_os_config_error error;
	hx_os_status status;
	json_limits(&options);
	status = hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, data, bytes,
		&options, &document, &error);
	if (status != HX_OS_OK ||
		hx_os_config_kind(hx_os_config_root(document)) !=
			HX_OS_CONFIG_OBJECT) {
		hx_os_config_release(document);
		return NULL;
	}
	return document;
}

static inline const hx_os_config_node *json_member(
	const hx_os_config_node *object, const char *key)
{
	const hx_os_config_node *node = NULL;
	(void)hx_os_config_member(object, key, strlen(key), &node);
	return node;
}

/* Config's numeric getters also accept strings; business schemas do not. */
static inline int json_get_int(
	const hx_os_config_node *object, const char *key, int64_t *number)
{
	const hx_os_config_node *node = json_member(object, key);
	return hx_os_config_kind(node) == HX_OS_CONFIG_INTEGER &&
		hx_os_config_get_i64(node, number) == HX_OS_OK;
}

static inline int json_has_int(
	const hx_os_config_node *object, const char *key, int64_t expected)
{
	int64_t value;
	return json_get_int(object, key, &value) && value == expected;
}

static inline int json_has_text(
	const hx_os_config_node *object, const char *key, const char *expected)
{
	hx_os_config_slice value;
	return hx_os_config_get_string(json_member(object, key), &value) ==
		HX_OS_OK &&
		value.bytes == strlen(expected) &&
		!memcmp(value.data, expected, value.bytes);
}

static inline int json_has_bool(
	const hx_os_config_node *object, const char *key, int expected)
{
	int value;
	const hx_os_config_node *node = json_member(object, key);
	return hx_os_config_kind(node) == HX_OS_CONFIG_BOOL &&
		hx_os_config_get_bool(node, &value) == HX_OS_OK &&
		value == expected;
}

#endif
