#include "config_private.h"

enum { HX_INI_SET, HX_INI_REMOVE_KEY, HX_INI_REMOVE_SECTION };

#if HX_OS_ENABLE_CONFIG
static hx_os_status hx_os_ini_utf8(const char *text)
{
	size_t bad = 0;

	return text ? hx_os_text_validate_utf8(text, strlen(text), &bad)
		    : HX_OS_OK;
}
#endif

hx_os_status hx_os_ini_create(hx_os_context *context,
	hx_os_config_document **out, hx_os_config_error *error)
{
	return hx_os_config_parse(
		context, HX_OS_CONFIG_INI, "", 0, NULL, out, error);
}

hx_os_status hx_os_ini_load(const char *path, hx_os_config_document **out,
	hx_os_config_error *error)
{
	return hx_os_config_load(
		NULL, HX_OS_CONFIG_INI, path, NULL, out, error);
}

hx_os_status hx_os_ini_save(const hx_os_config_document *document,
	const char *path, hx_os_error *error)
{
	return hx_os_config_save(document, HX_OS_CONFIG_INI, path, NULL, error);
}

hx_os_status hx_os_ini_get(const hx_os_config_document *document,
	const char *section, const char *key, hx_string *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG
	const hx_os_config_node *group, *node;
	hx_os_config_slice text;
	hx_string_view view;
	hx_os_status status;

	if (!document || !key || !*key || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_ini_utf8(section) != HX_OS_OK ||
		hx_os_ini_utf8(key) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	group = document->root;
	if (section && *section) {
		status = hx_os_config_member(
			group, section, strlen(section), &group);
		if (status != HX_OS_OK)
			return hx_os_i_error(error, status, 0, 0);
	}
	status = hx_os_config_member(group, key, strlen(key), &node);
	if (status == HX_OS_OK)
		status = hx_os_config_get_string(node, &text);
	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);
	view.data = text.data;
	view.size = text.bytes;

	return hx_string_assign(out, view, error);
#else
	(void)document;
	(void)section;
	(void)key;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
typedef struct hx_os_ini_edit {
	hx_os_config_slice section, key, value;
	int operation, found;
} hx_os_ini_edit;

static int hx_os_ini_equal(hx_os_config_slice a, hx_os_config_slice b)
{
	return a.bytes == b.bytes &&
		(!a.bytes || !memcmp(a.data, b.data, a.bytes));
}

static hx_os_config_node *hx_os_ini_node(hx_os_cfg_parser *parser,
	hx_os_config_slice name, hx_os_config_slice text, int kind,
	size_t depth)
{
	hx_os_config_value value;

	memset(&value, 0, sizeof(value));
	value.kind = kind;
	value.name = name;
	value.value = text;

	return hx_os_cfg_build_value(parser, &value, depth);
}

static int hx_os_ini_append(hx_os_cfg_parser *parser, hx_os_config_node *parent,
	hx_os_config_node *child)
{
	return child &&
		hx_os_cfg_append(
			parser, parent, child, HX_OS_CONFIG_DUPLICATE_ERROR);
}

static hx_os_config_node *hx_os_ini_group(hx_os_cfg_parser *parser,
	const hx_os_config_node *source, hx_os_config_slice name,
	hx_os_ini_edit *edit, size_t depth)
{
	hx_os_config_slice empty = {NULL, 0};
	hx_os_config_node *group =
		hx_os_ini_node(parser, name, empty, HX_OS_CONFIG_OBJECT, depth);
	const hx_os_config_node *child;

	if (!group)
		return NULL;
	for (child = source ? source->first : NULL;
		child && parser->status == HX_OS_OK; child = child->next) {
		hx_os_config_node *copy;

		if (hx_os_ini_equal(child->name, edit->key)) {
			edit->found = 1;
			if (child->kind != HX_OS_CONFIG_STRING) {
				hx_os_cfg_reject(parser, HX_OS_TYPE_MISMATCH,
					"INI key conflicts with a non-string "
					"value");
				break;
			}
			if (edit->operation == HX_INI_REMOVE_KEY)
				continue;
			copy = hx_os_ini_node(parser, edit->key, edit->value,
				HX_OS_CONFIG_STRING, depth + 1);
		}
		else
			copy = hx_os_cfg_clone(parser, child, depth + 1);
		if (!hx_os_ini_append(parser, group, copy))
			break;
	}
	if (parser->status == HX_OS_OK && !edit->found &&
		edit->operation == HX_INI_SET)
		hx_os_ini_append(parser, group,
			hx_os_ini_node(parser, edit->key, edit->value,
				HX_OS_CONFIG_STRING, depth + 1));

	return group;
}

static hx_os_config_node *hx_os_ini_tree(hx_os_cfg_parser *parser,
	const hx_os_config_node *source, hx_os_ini_edit *edit)
{
	hx_os_config_slice empty = {NULL, 0};
	hx_os_config_node *root;
	const hx_os_config_node *child;
	int section_found = 0;

	if (!edit->section.bytes)
		return hx_os_ini_group(parser, source, source->name, edit, 1);
	root = hx_os_ini_node(
		parser, source->name, empty, HX_OS_CONFIG_OBJECT, 1);
	if (!root)
		return NULL;
	for (child = source->first; child && parser->status == HX_OS_OK;
		child = child->next) {
		hx_os_config_node *copy;

		if (hx_os_ini_equal(child->name, edit->section)) {
			section_found = 1;
			if (child->kind != HX_OS_CONFIG_OBJECT) {
				hx_os_cfg_reject(parser, HX_OS_TYPE_MISMATCH,
					"INI section conflicts with a scalar "
					"key");
				break;
			}
			if (edit->operation == HX_INI_REMOVE_SECTION) {
				edit->found = 1;
				continue;
			}
			copy = hx_os_ini_group(
				parser, child, child->name, edit, 2);
		}
		else
			copy = hx_os_cfg_clone(parser, child, 2);
		if (!hx_os_ini_append(parser, root, copy))
			break;
	}
	if (parser->status == HX_OS_OK && !section_found &&
		edit->operation == HX_INI_SET)
		hx_os_ini_append(parser, root,
			hx_os_ini_group(parser, NULL, edit->section, edit, 2));

	return root;
}
#endif

static hx_os_status hx_os_ini_change(hx_os_config_document **owner,
	const char *section, const char *key, const char *text, int operation,
	hx_os_config_error *error)
{
	hx_os_cfg_parser parser;

	memset(&parser, 0, sizeof(parser));
	parser.error = error;
	hx_os_cfg_error_init(error, NULL);
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	{
		hx_os_config_document *source;
		hx_os_config_options options;
		hx_os_ini_edit edit;
		hx_os_status status;

		if (!owner || !*owner ||
			(operation != HX_INI_REMOVE_SECTION &&
				(!key || !*key)) ||
			(operation == HX_INI_SET && !text) ||
			(operation == HX_INI_REMOVE_SECTION &&
				(!section || !*section)))
			return hx_os_cfg_fail(&parser, HX_OS_INVALID, SIZE_MAX,
				"Invalid INI edit parameters");
		if (hx_os_ini_utf8(section) != HX_OS_OK ||
			hx_os_ini_utf8(key) != HX_OS_OK ||
			hx_os_ini_utf8(text) != HX_OS_OK)
			return hx_os_cfg_fail(&parser, HX_OS_INVALID_ENCODING,
				SIZE_MAX, "INI edit text is not UTF-8");
		source = *owner;
		if (source->root->kind != HX_OS_CONFIG_OBJECT)
			return hx_os_cfg_fail(&parser, HX_OS_TYPE_MISMATCH,
				SIZE_MAX, "INI requires an object root");
		memset(&edit, 0, sizeof(edit));
		edit.section.data = section;
		edit.section.bytes = section ? strlen(section) : 0;
		edit.key.data = key;
		edit.key.bytes = key ? strlen(key) : 0;
		edit.value.data = text;
		edit.value.bytes = text ? strlen(text) : 0;
		edit.operation = operation;
		options = source->options;
		options.backend = HX_OS_CONFIG_BUILTIN;
		if (!hx_os_cfg_document_create(
			    &parser, source->context, &options))
			return parser.status;
		parser.document->root =
			hx_os_ini_tree(&parser, source->root, &edit);
		if (parser.status == HX_OS_OK && operation != HX_INI_SET &&
			!edit.found)
			hx_os_cfg_reject(&parser, HX_OS_NOT_FOUND,
				"INI entry was not found");
		if (parser.status == HX_OS_OK) {
			status = hx_os_cfg_ini_validate(parser.document);
			if (status != HX_OS_OK)
				hx_os_cfg_reject(&parser, status,
					"Value cannot be represented by the "
					"INI profile");
		}
		if (parser.status == HX_OS_OK) {
			*owner = parser.document;
			hx_os_config_release(source);
		}
		else
			hx_os_config_release(parser.document);

		return parser.status;
	}
#else
	(void)owner;
	(void)section;
	(void)key;
	(void)text;
	(void)operation;
	return hx_os_cfg_fail(&parser, HX_OS_NOT_SUPPORTED, SIZE_MAX,
		"INI editing requires configuration parsing and serialization");
#endif
}

hx_os_status hx_os_ini_set(hx_os_config_document **owner, const char *section,
	const char *key, const char *value, hx_os_config_error *error)
{
	return hx_os_ini_change(owner, section, key, value, HX_INI_SET, error);
}

hx_os_status hx_os_ini_remove_key(hx_os_config_document **owner,
	const char *section, const char *key, hx_os_config_error *error)
{
	return hx_os_ini_change(
		owner, section, key, NULL, HX_INI_REMOVE_KEY, error);
}

hx_os_status hx_os_ini_remove_section(hx_os_config_document **owner,
	const char *section, hx_os_config_error *error)
{
	return hx_os_ini_change(
		owner, section, NULL, NULL, HX_INI_REMOVE_SECTION, error);
}
