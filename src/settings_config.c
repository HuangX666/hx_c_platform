#include "settings_private.h"
#include "config_private.h"
#include "number_private.h"
#include <inttypes.h>
#include <stdio.h>
#if HX_OS_ENABLE_CONFIG
typedef struct hx_settings_measure {
	hx_settings_limits limits;
	size_t nodes;
	size_t memory;
} hx_settings_measure;
/* Validate the complete destination budget before allocating converted
 * children. Temporary pair arrays are bounded by the source node count.
 */
static hx_os_status hx_settings_measure_node(const hx_os_config_node *node,
	size_t depth, hx_settings_measure *measure, hx_os_error *error)
{
	const hx_os_config_node *child;
	size_t bytes = sizeof(hx_settings_value), cost;
	hx_os_status status;
	if (!node || node->kind < HX_OS_CONFIG_NULL ||
		node->kind > HX_OS_CONFIG_ARRAY)
		return hx_os_i_error(error, HX_OS_UNREPRESENTABLE, 0, 0);
	if (depth > measure->limits.depth ||
		measure->nodes == measure->limits.nodes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	++measure->nodes;
	if (node->kind == HX_OS_CONFIG_STRING) {
		if (!hx_os_i_add_size(bytes, node->value.bytes, &bytes) ||
			!hx_os_i_add_size(bytes, 1, &bytes))
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}
	else if (node->kind == HX_OS_CONFIG_ARRAY ||
		node->kind == HX_OS_CONFIG_OBJECT) {
		size_t unit = node->kind == HX_OS_CONFIG_ARRAY
			? sizeof(hx_settings_value *)
			: sizeof(hx_settings_entry);
		if (node->count > (SIZE_MAX - bytes) / unit)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		bytes += node->count * unit;
		if (node->kind == HX_OS_CONFIG_OBJECT)
			for (child = node->first; child; child = child->next)
				if (!hx_os_i_add_size(
					    bytes, child->name.bytes, &bytes) ||
					!hx_os_i_add_size(bytes, 1, &bytes))
					return hx_os_i_error(
						error, HX_OS_LIMIT, 0, 0);
	}
	cost = hx_os_i_allocation_cost(bytes, 0);
	if (!cost || cost > measure->limits.memory - measure->memory)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	measure->memory += cost;
	for (child = node->first; child; child = child->next) {
		status = hx_settings_measure_node(
			child, depth + 1, measure, error);
		if (status != HX_OS_OK)
			return status;
	}
	return HX_OS_OK;
}
static hx_os_status hx_settings_convert_node(hx_os_context *context,
	const hx_os_config_node *node, const hx_settings_limits *limits,
	hx_settings_value **out, hx_os_error *error)
{
	hx_settings_scalar scalar = {0};
	hx_string_view text = {NULL, 0};
	hx_os_status status = HX_OS_OK;
	int kind = node->kind, boolean;
	*out = NULL;
	switch (kind) {
	case HX_OS_CONFIG_NULL:
		break;
	case HX_OS_CONFIG_BOOL:
		status = hx_os_config_get_bool(node, &boolean);
		if (status == HX_OS_OK)
			scalar.integer = boolean;
		break;
	case HX_OS_CONFIG_INTEGER:
		status = hx_os_config_get_i64(node, &scalar.integer);
		break;
	case HX_OS_CONFIG_REAL:
		status = hx_os_config_get_double(node, &scalar.real);
		break;
	case HX_OS_CONFIG_STRING:
		text.data = node->value.data;
		text.size = node->value.bytes;
		break;
	case HX_OS_CONFIG_ARRAY:
	case HX_OS_CONFIG_OBJECT: {
		hx_settings_pair *pairs = NULL;
		hx_settings_value **items = NULL;
		const hx_os_config_node *child;
		size_t i = 0, count = node->count;
		if (count) {
			if (kind == HX_OS_CONFIG_ARRAY)
				items = hx_os_i_zero(
					context, count * sizeof(*items));
			else
				pairs = hx_os_i_zero(
					context, count * sizeof(*pairs));
			if (!items && !pairs)
				return hx_os_i_error(
					error, HX_OS_NO_MEMORY, 0, 0);
		}
		for (child = node->first; child; child = child->next, ++i) {
			hx_settings_value *value = NULL;
			status = hx_settings_convert_node(
				context, child, limits, &value, error);
			if (status != HX_OS_OK)
				break;
			if (items)
				items[i] = value;
			else {
				pairs[i].name.data = child->name.data;
				pairs[i].name.size = child->name.bytes;
				pairs[i].value = value;
			}
		}
		if (status == HX_OS_OK) {
			if (kind == HX_OS_CONFIG_ARRAY)
				status = hx_settings_i_array_retain(context,
					(const hx_settings_value *const *)items,
					count, limits, out, error);
			else
				status = hx_settings_i_object_retain(context,
					pairs, count, limits, out, error);
		}
		for (i = 0; i < count; ++i)
			hx_settings_value_release(items
					? items[i]
					: (hx_settings_value *)pairs[i].value);
		hx_os_memory_free(items);
		hx_os_memory_free(pairs);
		return status;
	}
	default:
		status = HX_OS_UNREPRESENTABLE;
		break;
	}
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	/* JSON-compatible kind values match the first seven settings kinds. */
	return hx_settings_i_scalar(
		context, kind, scalar, text, limits, out, error);
}
hx_os_status hx_settings_i_import(hx_settings_frame *frame,
	const hx_os_config_document *document, hx_os_error *error)
{
	hx_settings_measure measure;
	hx_settings_value *value = NULL;
	const hx_os_config_node *root;
	hx_os_status status;
	if (!document)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	root = hx_os_config_root(document);
	if (!root || root->kind != HX_OS_CONFIG_OBJECT)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);
	memset(&measure, 0, sizeof(measure));
	measure.limits = hx_settings_i_limits(frame->settings);
	status = hx_settings_measure_node(root, 1, &measure, error);
	if (status == HX_OS_OK)
		status = hx_settings_convert_node(frame->settings->context,
			root, &measure.limits, &value, error);
	if (status == HX_OS_OK)
		status = hx_settings_i_commit_root(frame, value, error);
	hx_settings_value_release(value);
	return status;
}
static hx_os_config_node *hx_settings_export_node(
	hx_os_cfg_parser *parser, const hx_settings_value *value, size_t depth)
{
	hx_os_config_node *node;
	hx_os_status status = HX_OS_OK;
	char number[128];
	const char *text = NULL;
	size_t bytes = 0, required, i;
	int length, kind = value->kind;
	if (kind < HX_SETTINGS_NULL || kind > HX_SETTINGS_ARRAY) {
		(void)hx_os_cfg_reject(parser, HX_OS_UNREPRESENTABLE,
			"Settings value cannot be serialized");
		return NULL;
	}
	if (!hx_os_cfg_work(parser, 1))
		return NULL;
	node = hx_os_cfg_node(parser, kind, depth);
	if (!node)
		return NULL;
	switch (kind) {
	case HX_SETTINGS_NULL:
		break;
	case HX_SETTINGS_BOOL:
		text = value->scalar.integer ? "true" : "false";
		bytes = strlen(text);
		break;
	case HX_SETTINGS_I64:
		length = snprintf(number, sizeof(number), "%" PRId64,
			value->scalar.integer);
		if (length < 0 || (size_t)length >= sizeof(number))
			status = HX_OS_RANGE;
		else {
			text = number;
			bytes = (size_t)length;
		}
		break;
	case HX_SETTINGS_DOUBLE:
		status = hx_os_i_format_double(
			value->scalar.real, number, sizeof(number), &required);
		if (status == HX_OS_OK) {
			text = number;
			bytes = required - 1;
		}
		break;
	case HX_SETTINGS_STRING:
		text = (const char *)(value + 1);
		bytes = value->count;
		break;
	case HX_SETTINGS_ARRAY:
	case HX_SETTINGS_OBJECT:
		for (i = 0; i < value->count && parser->status == HX_OS_OK;
			++i) {
			const hx_settings_entry *entries =
				(const hx_settings_entry *)(value + 1);
			const hx_settings_value *child =
				kind == HX_SETTINGS_ARRAY
				? ((hx_settings_value *const *)(value + 1))[i]
				: entries[i].value;
			hx_os_config_node *copy = hx_settings_export_node(
				parser, child, depth + 1);
			if (!copy)
				break;
			if (kind == HX_SETTINGS_OBJECT)
				copy->name = hx_os_cfg_copy(parser,
					entries[i].name.data,
					entries[i].name.size);
			if (parser->status != HX_OS_OK)
				break;
			/* Sorted immutable keys are already unique. The
			 * parser's duplicate-key search is unnecessary for this
			 * builder.
			 */
			if (node->last)
				node->last->next = copy;
			else
				node->first = copy;
			node->last = copy;
			++node->count;
		}
		break;
	}
	if (status != HX_OS_OK)
		(void)hx_os_cfg_reject(
			parser, status, "Settings number conversion failed");
	else if (text)
		node->value = hx_os_cfg_copy(parser, text, bytes);
	return parser->status == HX_OS_OK ? node : NULL;
}
hx_os_status hx_settings_i_document(hx_os_context *context,
	const hx_settings_value *value, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_error *error)
{
	hx_os_cfg_parser parser;
	hx_os_config_options settings;
	if (!out || !value)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	memset(&parser, 0, sizeof(parser));
	if (!hx_os_cfg_options(&parser, options, &settings))
		return hx_os_i_error(error, parser.status, 0, 0);
	if (settings.backend != HX_OS_CONFIG_BUILTIN)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	if (!hx_os_cfg_document_create(&parser, context, &settings))
		return hx_os_i_error(error, parser.status, 0, 0);
	parser.document->root = hx_settings_export_node(&parser, value, 1);
	if (parser.status != HX_OS_OK) {
		hx_os_config_release(parser.document);
		return hx_os_i_error(error, parser.status, 0, 0);
	}
	*out = parser.document;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif
hx_os_status hx_settings_import(hx_settings *settings,
	const hx_os_config_document *document, hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG
	hx_settings_frame frame;
	hx_os_status status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_i_import(&frame, document, error);
	hx_settings_i_end(&frame);
	return status;
#else
	(void)settings;
	(void)document;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_settings_to_config(hx_settings *settings, int layer,
	const hx_os_config_options *options, hx_os_config_document **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG
	hx_settings_frame frame;
	hx_settings_value *merged = NULL;
	const hx_settings_value *root;
	hx_settings_limits limits;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (layer < HX_SETTINGS_EXPLICIT || layer > HX_SETTINGS_EFFECTIVE)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	root = frame.roots[layer == HX_SETTINGS_DEFAULTS ? 1 : 0];
	if (layer == HX_SETTINGS_EFFECTIVE) {
		limits = hx_settings_i_limits(settings);
		status = hx_settings_i_merge(settings->context, frame.roots[0],
			frame.roots[1], &limits, &merged, error);
		root = merged;
	}
	if (status == HX_OS_OK)
		status = hx_settings_i_document(
			settings->context, root, options, out, error);
	hx_settings_value_release(merged);
	hx_settings_i_end(&frame);
	return status;
#else
	(void)settings;
	(void)layer;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
static void hx_settings_load_error(
	hx_os_config_error *error, const hx_os_error *detail, int reset)
{
	if (!error)
		return;
	if (reset)
		memset(error, 0, sizeof(*error));
	error->detail = *detail;
	error->byte_offset = SIZE_MAX;
	error->line = 0;
	error->column = 0;
	(void)snprintf(error->message, sizeof(error->message), "%s",
		"Settings operation failed");
}
hx_os_status hx_settings_load(hx_settings *settings, int format,
	const char *path, const hx_os_config_options *options,
	hx_os_config_error *error)
{
	hx_os_error detail;
#if HX_OS_ENABLE_CONFIG
	hx_settings_frame frame;
	hx_os_config_document *document = NULL;
	hx_os_status status = hx_settings_i_begin(settings, &frame, &detail);
	if (status != HX_OS_OK) {
		hx_settings_load_error(error, &detail, 1);
		return status;
	}
	status = hx_os_config_load(
		settings->context, format, path, options, &document, error);
	if (status == HX_OS_OK) {
		status = hx_settings_i_import(&frame, document, &detail);
		if (status != HX_OS_OK)
			hx_settings_load_error(error, &detail, 0);
	}
	hx_os_config_release(document);
	hx_settings_i_end(&frame);
	return status;
#else
	(void)settings;
	(void)format;
	(void)path;
	(void)options;
	(void)hx_os_i_error(&detail, HX_OS_NOT_SUPPORTED, 0, 0);
	hx_settings_load_error(error, &detail, 1);
	return HX_OS_NOT_SUPPORTED;
#endif
}
hx_os_status hx_settings_serialize(hx_settings *settings, int format,
	hx_string *out, const hx_os_config_options *options, hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	hx_settings_frame frame;
	hx_os_config_document *document = NULL;
	hx_os_status status;
	if (format != HX_OS_CONFIG_JSON && format != HX_OS_CONFIG_INI &&
		format != HX_OS_CONFIG_PROPERTIES)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_i_document(
		settings->context, frame.roots[0], options, &document, error);
	if (status == HX_OS_OK)
		status = hx_os_config_serialize(
			document, format, out, options, error);
	hx_os_config_release(document);
	hx_settings_i_end(&frame);
	return status;
#else
	(void)settings;
	(void)format;
	(void)out;
	(void)options;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_settings_save(hx_settings *settings, int format,
	const char *path, const hx_os_config_options *options,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE &&                    \
	HX_OS_ENABLE_FILE_UTILS
	hx_settings_frame frame;
	hx_os_config_document *document = NULL;
	hx_os_status status;
	if (format != HX_OS_CONFIG_JSON && format != HX_OS_CONFIG_INI &&
		format != HX_OS_CONFIG_PROPERTIES)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	status = hx_settings_i_begin(settings, &frame, error);
	if (status != HX_OS_OK)
		return status;
	status = hx_settings_i_document(
		settings->context, frame.roots[0], options, &document, error);
	if (status == HX_OS_OK)
		status = hx_os_config_save(
			document, format, path, options, error);
	hx_os_config_release(document);
	hx_settings_i_end(&frame);
	return status;
#else
	(void)settings;
	(void)format;
	(void)path;
	(void)options;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_settings_to_json(
	hx_settings *settings, hx_string *out, hx_os_error *error)
{
	return hx_settings_serialize(
		settings, HX_OS_CONFIG_JSON, out, NULL, error);
}
hx_os_status hx_settings_save_json(
	hx_settings *settings, const char *path, hx_os_error *error)
{
	return hx_settings_save(settings, HX_OS_CONFIG_JSON, path, NULL, error);
}
hx_os_status hx_settings_load_json(
	hx_settings *settings, const char *path, hx_os_error *error)
{
	hx_os_config_error detail;
	hx_os_status status = hx_settings_load(
		settings, HX_OS_CONFIG_JSON, path, NULL, &detail);
	if (status == HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	if (error)
		*error = detail.detail;
	return status;
}
