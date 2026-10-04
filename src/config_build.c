#include "config_private.h"
#include "number_private.h"

#if HX_OS_ENABLE_CONFIG
static int hx_os_cfg_build_slice(hx_os_cfg_parser *p, hx_os_config_slice s)
{
	size_t bad;

	if ((!s.data && s.bytes) || s.bytes == SIZE_MAX)
		return hx_os_cfg_reject(
			p, HX_OS_INVALID, "Invalid value slice");

	if (s.bytes > p->document->options.max_input_bytes - p->length)
		return hx_os_cfg_reject(
			p, HX_OS_LIMIT, "Value byte limit exceeded");

	p->length += s.bytes;
	if (!hx_os_cfg_work(p, s.bytes))
		return 0;

	if (hx_os_text_validate_utf8(s.data, s.bytes, &bad) != HX_OS_OK)
		return hx_os_cfg_reject(
			p, HX_OS_INVALID_ENCODING, "Value is not UTF-8");

	return 1;
}

static int hx_os_cfg_build_child(int parent, int child)
{
	if (parent == HX_OS_CONFIG_ELEMENT)
		return child == HX_OS_CONFIG_ELEMENT ||
			child == HX_OS_CONFIG_TEXT;

	if (parent == HX_OS_CONFIG_ATTRIBUTE)
		return child == HX_OS_CONFIG_ATTRIBUTE;

	return child <= HX_OS_CONFIG_ARRAY || child == HX_OS_CONFIG_DATETIME;
}

static hx_os_config_node *hx_os_cfg_build(hx_os_cfg_parser *p,
	const hx_os_config_value *v, const hx_os_config_slice *name,
	size_t depth)
{
	hx_os_config_node *node, *child;
	hx_os_config_slice key = name ? *name : v->name;
	size_t i;
	int real,
		container = v->kind == HX_OS_CONFIG_OBJECT ||
		v->kind == HX_OS_CONFIG_ARRAY ||
		v->kind == HX_OS_CONFIG_ELEMENT;

	if (!hx_os_cfg_work(p, 1))
		return NULL;

	if (v->count > p->document->options.max_nodes ||
		v->attribute_count > p->document->options.max_nodes ||
		v->count > SIZE_MAX / sizeof(*v) ||
		v->attribute_count > SIZE_MAX / sizeof(*v)) {
		(void)hx_os_cfg_reject(p, HX_OS_LIMIT,
			"Value description count limit exceeded");

		return NULL;
	}

	if (v->kind < HX_OS_CONFIG_NULL || v->kind > HX_OS_CONFIG_ATTRIBUTE ||
		(v->count && (!container || !v->children)) ||
		(v->attribute_count &&
			(v->kind != HX_OS_CONFIG_ELEMENT || !v->attributes)) ||
		(container && v->value.bytes))
		goto invalid;
	if (!hx_os_cfg_build_slice(p, key) ||
		!hx_os_cfg_build_slice(p, v->value))
		return NULL;
	if ((v->kind == HX_OS_CONFIG_ELEMENT ||
		    v->kind == HX_OS_CONFIG_ATTRIBUTE) &&
		!hx_os_cfg_xml_valid(key, 1))
		goto invalid;
	if ((v->kind == HX_OS_CONFIG_TEXT ||
		    v->kind == HX_OS_CONFIG_ATTRIBUTE) &&
		!hx_os_cfg_xml_valid(v->value, 0))
		goto invalid;
	if (v->kind == HX_OS_CONFIG_BOOL &&
		!hx_os_cfg_equal(v->value, "true") &&
		!hx_os_cfg_equal(v->value, "false"))
		goto invalid;
	if (v->kind == HX_OS_CONFIG_NULL && v->value.bytes &&
		!hx_os_cfg_equal(v->value, "null"))
		goto invalid;
	if ((v->kind == HX_OS_CONFIG_INTEGER || v->kind == HX_OS_CONFIG_REAL) &&
		(!hx_os_i_number_valid(
			 v->value.data, v->value.bytes, 0, &real) ||
			(v->kind == HX_OS_CONFIG_INTEGER && real)))
		goto invalid;
	node = hx_os_cfg_node(p, v->kind, depth);
	if (!node)
		return NULL;

	node->name = hx_os_cfg_copy(p, key.data, key.bytes);
	node->value = v->kind == HX_OS_CONFIG_NULL
		? hx_os_cfg_copy(p, "null", 4)
		: hx_os_cfg_copy(p, v->value.data, v->value.bytes);

	for (i = 0; i < v->count && p->status == HX_OS_OK; ++i) {
		if (!hx_os_cfg_build_child(v->kind, v->children[i].kind))
			goto invalid;

		child = hx_os_cfg_build(p, v->children + i, NULL, depth + 1);
		if (!hx_os_cfg_append(
			    p, node, child, HX_OS_CONFIG_DUPLICATE_ERROR))
			return NULL;
	}

	if (v->attribute_count) {
		node->attributes =
			hx_os_cfg_node(p, HX_OS_CONFIG_OBJECT, depth);

		for (i = 0; i < v->attribute_count && p->status == HX_OS_OK;
			++i) {
			if (v->attributes[i].kind != HX_OS_CONFIG_ATTRIBUTE)
				goto invalid;

			child = hx_os_cfg_build(
				p, v->attributes + i, NULL, depth + 1);
			if (!hx_os_cfg_append(p, node->attributes, child,
				    HX_OS_CONFIG_DUPLICATE_ERROR))
				return NULL;
		}
	}

	return p->status == HX_OS_OK ? node : NULL;

invalid:
	(void)hx_os_cfg_reject(p, HX_OS_INVALID, "Invalid value description");
	return NULL;
}

static hx_os_config_node *hx_os_cfg_copy_tree(hx_os_cfg_parser *p,
	const hx_os_config_node *source, size_t depth,
	const hx_os_config_node *target, const hx_os_config_value *replacement,
	int attributes)
{
	hx_os_config_node *node, *copy;
	const hx_os_config_node *child;

	if (source == target) {
		if (attributes) {
			(void)hx_os_cfg_reject(p, HX_OS_INVALID,
				"Replace attributes individually or replace "
				"their element");

			return NULL;
		}

		return hx_os_cfg_build(p, replacement, &source->name, depth);
	}

	if (!hx_os_cfg_work(p, 1) || !hx_os_cfg_build_slice(p, source->name) ||
		!hx_os_cfg_build_slice(p, source->value))
		return NULL;
	node = hx_os_cfg_node(p, source->kind, depth);
	if (!node)
		return NULL;

	node->name = hx_os_cfg_copy(p, source->name.data, source->name.bytes);
	node->value =
		hx_os_cfg_copy(p, source->value.data, source->value.bytes);
	node->flags = source->flags;

	for (child = source->first; child && p->status == HX_OS_OK;
		child = child->next) {
		copy = hx_os_cfg_copy_tree(
			p, child, depth + 1, target, replacement, 0);
		if (copy &&
			!hx_os_cfg_build_child(attributes
					? HX_OS_CONFIG_ATTRIBUTE
					: node->kind,
				copy->kind)) {
			(void)hx_os_cfg_reject(p, HX_OS_TYPE_MISMATCH,
				"Replacement does not fit its parent");

			return NULL;
		}

		if (!hx_os_cfg_append(
			    p, node, copy, HX_OS_CONFIG_DUPLICATE_ERROR))
			return NULL;
	}

	if (source->attributes)
		node->attributes = hx_os_cfg_copy_tree(
			p, source->attributes, depth, target, replacement, 1);

	return p->status == HX_OS_OK ? node : NULL;
}

hx_os_config_node *hx_os_cfg_clone(
	hx_os_cfg_parser *parser, const hx_os_config_node *source, size_t depth)
{
	return hx_os_cfg_copy_tree(parser, source, depth, NULL, NULL, 0);
}
#endif

static hx_os_status hx_os_cfg_construct(hx_os_context *context,
	const hx_os_config_document *source, const hx_os_config_node *target,
	const hx_os_config_value *value, const hx_os_config_options *input,
	hx_os_config_document **out, hx_os_config_error *error)
{
	hx_os_cfg_parser p;

	memset(&p, 0, sizeof(p));
	p.error = error;
	hx_os_cfg_error_init(error,
		input && input->size == sizeof(*input) ? input->source_name
						       : NULL);
	if (out)
		*out = NULL;
#if HX_OS_ENABLE_CONFIG
	{
		hx_os_config_options options;

		if (!out || !value ||
			(source && (!target || target->document != source)))
			return hx_os_cfg_fail(&p, HX_OS_INVALID, SIZE_MAX,
				"Invalid document construction input");
		if (!hx_os_cfg_options(&p, input, &options))
			return p.status;

		if (!hx_os_cfg_document_create(&p, context, &options))
			return p.status;

		p.document->root = source
			? hx_os_cfg_copy_tree(
				  &p, source->root, 1, target, value, 0)
			: hx_os_cfg_build(&p, value, NULL, 1);
		if (p.status == HX_OS_OK && p.document->root &&
			p.document->root->kind <= HX_OS_CONFIG_ELEMENT) {
			*out = p.document;

			return HX_OS_OK;
		}

		if (p.status == HX_OS_OK)
			(void)hx_os_cfg_reject(
				&p, HX_OS_INVALID, "Invalid document root");

		hx_os_config_release(p.document);

		return p.status;
	}
#else
	(void)context;
	(void)source;
	(void)target;
	(void)value;

	return hx_os_cfg_fail(&p, HX_OS_NOT_SUPPORTED, SIZE_MAX,
		"Configuration module is disabled");
#endif
}

hx_os_status hx_os_config_create(hx_os_context *context,
	const hx_os_config_value *root, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_config_error *error)
{
	return hx_os_cfg_construct(
		context, NULL, NULL, root, options, out, error);
}

hx_os_status hx_os_config_replace(const hx_os_config_document *source,
	const hx_os_config_node *target, const hx_os_config_value *value,
	const hx_os_config_options *options, hx_os_config_document **out,
	hx_os_config_error *error)
{
	if (!source)
		return hx_os_cfg_construct(
			NULL, NULL, NULL, NULL, options, out, error);

	return hx_os_cfg_construct(
		source->context, source, target, value, options, out, error);
}

hx_os_status hx_os_config_parse_node(hx_os_context *context, int format,
	const hx_os_config_node *node, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_config_error *error)
{
	hx_os_cfg_parser p;

	memset(&p, 0, sizeof(p));
	p.error = error;
	if (out)
		*out = NULL;

	hx_os_cfg_error_init(error, NULL);
	if (!node ||
		(node->kind != HX_OS_CONFIG_STRING &&
			node->kind != HX_OS_CONFIG_TEXT &&
			node->kind != HX_OS_CONFIG_ATTRIBUTE))
		return hx_os_cfg_fail(&p, HX_OS_TYPE_MISMATCH, SIZE_MAX,
			"Embedded document must be a string");
	return hx_os_config_parse(context, format, node->value.data,
		node->value.bytes, options, out, error);
}

hx_os_status hx_os_config_convert(hx_os_context *context, int input_format,
	const void *input, size_t bytes, int output_format, hx_string *out,
	const hx_os_config_options *input_options, hx_os_config_error *error)
{
	hx_os_config_document *doc = NULL;
	hx_os_config_options options;
	hx_os_error native;

	hx_os_status status = hx_os_config_parse(context, input_format, input,
		bytes, input_options, &doc, error);
	if (status != HX_OS_OK)
		return status;

	options = doc->options;
	options.backend = HX_OS_CONFIG_BUILTIN;

	status = hx_os_config_serialize(
		doc, output_format, out, &options, &native);
	if (status != HX_OS_OK && error) {
		hx_os_cfg_parser p;

		memset(&p, 0, sizeof(p));
		p.error = error;
		(void)hx_os_cfg_fail(&p, status, SIZE_MAX,
			"Document cannot be written in the requested format or "
			"limits");
		error->detail = native;
	}

	hx_os_config_release(doc);

	return status;
}
