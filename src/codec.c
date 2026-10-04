#include "config_private.h"
#include "number_private.h"
#include "hx_codec.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

struct hx_os_codec_result {
	hx_os_config_document *document;
	void *data;
};

int hx_os_codec_available(void)
{
	return HX_OS_ENABLE_CODEC && HX_OS_ENABLE_CONFIG &&
		HX_OS_ENABLE_CONFIG_SERIALIZE;
}

#if HX_OS_ENABLE_CODEC && HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
static int hx_os_codec_multiply(size_t a, size_t b, size_t *out)
{
	if (b && a > SIZE_MAX / b)
		return 0;

	*out = a * b;

	return 1;
}

static int hx_os_codec_fail(
	hx_os_cfg_parser *p, hx_os_status status, const char *message)
{
	return hx_os_cfg_reject(p, status, message);
}

static int hx_os_codec_text(hx_os_cfg_parser *p, hx_os_config_slice value)
{
	size_t bad;

	if (!value.data && value.bytes)
		return hx_os_codec_fail(
			p, HX_OS_INVALID, "Invalid string storage");

	if (value.bytes > p->document->options.max_input_bytes - p->length)
		return hx_os_codec_fail(
			p, HX_OS_LIMIT, "Mapped value limit exceeded");

	p->length += value.bytes;
	if (!hx_os_cfg_work(p, value.bytes))
		return 0;

	if (hx_os_text_validate_utf8(value.data, value.bytes, &bad) != HX_OS_OK)
		return hx_os_codec_fail(p, HX_OS_INVALID_ENCODING,
			"Mapped string is not UTF-8");

	return 1;
}

static int hx_os_codec_check_type(
	hx_os_cfg_parser *p, const hx_os_codec_type *type, size_t depth);

static int hx_os_codec_check_schema(
	hx_os_cfg_parser *p, const hx_os_codec_schema *schema, size_t depth)
{
	size_t i, j;

	if (depth > p->document->options.max_depth)
		return hx_os_codec_fail(p, HX_OS_LIMIT,
			"Schema depth limit exceeded (cyclic schemas are "
			"unsupported)");

	if (!schema || !schema->size || (schema->count && !schema->fields))
		return hx_os_codec_fail(
			p, HX_OS_INVALID, "Invalid object schema");

	if (schema->count > p->document->options.max_nodes)
		return hx_os_codec_fail(
			p, HX_OS_LIMIT, "Schema field limit exceeded");

	for (i = 0; i < schema->count; ++i) {
		const hx_os_codec_field *f = schema->fields + i;
		hx_os_config_slice name;

		if (!hx_os_cfg_work(p, 1))
			return 0;

		if (!f->name || f->offset > schema->size ||
			f->type.size > schema->size - f->offset ||
			(f->flags &
				~(unsigned)(HX_OS_CODEC_OPTIONAL |
					HX_OS_CODEC_XML_ATTRIBUTE)))
			return hx_os_codec_fail(
				p, HX_OS_INVALID, "Invalid mapped field");
		name.data = f->name;
		name.bytes = strlen(f->name);
		if (!hx_os_codec_text(p, name) ||
			!hx_os_codec_check_type(p, &f->type, depth + 1))
			return 0;
		if ((f->flags & HX_OS_CODEC_XML_ATTRIBUTE) &&
			f->type.kind >= HX_OS_CODEC_OBJECT)
			return hx_os_codec_fail(p, HX_OS_INVALID,
				"XML attributes require scalar fields");
		for (j = 0; j < i; ++j) {
			const hx_os_codec_field *other = schema->fields + j;

			if (!hx_os_cfg_work(p, name.bytes + 1))
				return 0;

			if (!strcmp(f->name, other->name) ||
				(f->offset < other->offset + other->type.size &&
					other->offset <
						f->offset + f->type.size))
				return hx_os_codec_fail(p, HX_OS_INVALID,
					"Duplicate or overlapping mapped "
					"fields");
		}
	}

	return 1;
}

static int hx_os_codec_check_type(
	hx_os_cfg_parser *p, const hx_os_codec_type *t, size_t depth)
{
	size_t expected = 0;

	if (!t || !hx_os_cfg_work(p, 1))
		return hx_os_codec_fail(
			p, HX_OS_INVALID, "Missing mapped type");

	if (depth > p->document->options.max_depth)
		return hx_os_codec_fail(
			p, HX_OS_LIMIT, "Schema depth limit exceeded");

	switch (t->kind) {
	case HX_OS_CODEC_BOOL:
		expected = t->size == 1 ? 1 : sizeof(int);
		break;
	case HX_OS_CODEC_I8:
		expected = sizeof(int8_t);
		break;
	case HX_OS_CODEC_U8:
		expected = sizeof(uint8_t);
		break;
	case HX_OS_CODEC_I16:
		expected = sizeof(int16_t);
		break;
	case HX_OS_CODEC_U16:
		expected = sizeof(uint16_t);
		break;
	case HX_OS_CODEC_I32:
		expected = sizeof(int32_t);
		break;
	case HX_OS_CODEC_U32:
		expected = sizeof(uint32_t);
		break;
	case HX_OS_CODEC_I64:
		expected = sizeof(int64_t);
		break;
	case HX_OS_CODEC_U64:
		expected = sizeof(uint64_t);
		break;
	case HX_OS_CODEC_FLOAT:
		expected = sizeof(float);
		break;
	case HX_OS_CODEC_DOUBLE:
		expected = sizeof(double);
		break;
	case HX_OS_CODEC_STRING:
		expected = sizeof(hx_string_view);
		break;
	case HX_OS_CODEC_BUFFER:
		expected = t->size;
		break;
	case HX_OS_CODEC_NODE:
		expected = sizeof(const hx_os_config_node *);
		break;
	case HX_OS_CODEC_OBJECT:
		if (!hx_os_codec_check_schema(p, t->schema, depth))
			return 0;

		expected = t->schema->size;
		break;
	case HX_OS_CODEC_ARRAY:
	case HX_OS_CODEC_FIXED_ARRAY:
		if (!hx_os_codec_check_type(p, t->element, depth + 1))
			return 0;

		if (t->kind == HX_OS_CODEC_ARRAY)
			expected = sizeof(hx_os_codec_array);
		else if (!hx_os_codec_multiply(
				 t->count, t->element->size, &expected))
			return hx_os_codec_fail(
				p, HX_OS_LIMIT, "Array size overflow");
		break;

	default:
		break;
	}

	if (!expected || expected != t->size)
		return hx_os_codec_fail(
			p, HX_OS_INVALID, "Mapped storage size mismatch");

	return 1;
}

static int hx_os_codec_plain(int format)
{
	return format == HX_OS_CONFIG_INI || format == HX_OS_CONFIG_ENV ||
		format == HX_OS_CONFIG_PROPERTIES;
}

static hx_os_config_node *hx_os_codec_node(
	hx_os_cfg_parser *p, int kind, const char *name, size_t depth)
{
	hx_os_config_node *node = hx_os_cfg_node(p, kind, depth);
	if (node) {
		node->name = hx_os_cfg_copy(p, name, name ? strlen(name) : 0);
		if (kind == HX_OS_CONFIG_ELEMENT ||
			kind == HX_OS_CONFIG_ATTRIBUTE)
			if (!hx_os_cfg_xml_valid(node->name, 1)) {
				(void)hx_os_codec_fail(p, HX_OS_UNREPRESENTABLE,
					"Field name cannot be represented as "
					"XML");

				return NULL;
			}
	}

	return p->status == HX_OS_OK ? node : NULL;
}

static hx_os_config_node *hx_os_codec_encode_value(hx_os_cfg_parser *p,
	const hx_os_codec_type *type, const void *data, int format,
	const char *name, size_t depth, int attribute);

static hx_os_config_node *hx_os_codec_encode_object(hx_os_cfg_parser *p,
	const hx_os_codec_schema *schema, const void *object, int format,
	const char *name, size_t depth)
{
	hx_os_config_node *node, *child;
	size_t i;
	int xml = format == HX_OS_CONFIG_XML;

	node = hx_os_codec_node(p,
		xml ? HX_OS_CONFIG_ELEMENT : HX_OS_CONFIG_OBJECT, name, depth);
	if (!node)
		return NULL;

	for (i = 0; i < schema->count && p->status == HX_OS_OK; ++i) {
		const hx_os_codec_field *f = schema->fields + i;
		int attr = xml && (f->flags & HX_OS_CODEC_XML_ATTRIBUTE);

		child = hx_os_codec_encode_value(p, &f->type,
			(const unsigned char *)object + f->offset, format,
			f->name, depth + 1, attr);
		if (attr && !node->attributes)
			node->attributes =
				hx_os_cfg_node(p, HX_OS_CONFIG_OBJECT, depth);

		if (!hx_os_cfg_append(p, attr ? node->attributes : node, child,
			    HX_OS_CONFIG_DUPLICATE_ERROR))
			return NULL;
	}

	return p->status == HX_OS_OK ? node : NULL;
}

static hx_os_config_node *hx_os_codec_encode_scalar(hx_os_cfg_parser *p,
	const hx_os_codec_type *t, const void *data, int format,
	const char *name, size_t depth, int attribute)
{
	char buffer[128];
	hx_os_config_slice value = {buffer, 0};
	hx_os_config_node *node, *text;
	int kind = HX_OS_CONFIG_INTEGER;
	int length = 0;
	size_t required;
	hx_os_status status = HX_OS_OK;

	switch (t->kind) {
	case HX_OS_CODEC_BOOL: {
		int n = 0;

		if (t->size == 1)
			n = *(const unsigned char *)data != 0;
		else
			memcpy(&n, data, sizeof(n));

		value.data = n ? "true" : "false";
		value.bytes = n ? 4 : 5;
		kind = HX_OS_CONFIG_BOOL;
		break;
	}
	case HX_OS_CODEC_I8: {
		int8_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%d", (int)n);
		break;
	}
	case HX_OS_CODEC_U8: {
		uint8_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%u", (unsigned)n);
		break;
	}
	case HX_OS_CODEC_I16: {
		int16_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%d", (int)n);
		break;
	}
	case HX_OS_CODEC_U16: {
		uint16_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%u", (unsigned)n);
		break;
	}
	case HX_OS_CODEC_I32: {
		int32_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%" PRId32, n);
		break;
	}
	case HX_OS_CODEC_U32: {
		uint32_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%" PRIu32, n);
		break;
	}
	case HX_OS_CODEC_I64: {
		int64_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%" PRId64, n);
		break;
	}
	case HX_OS_CODEC_U64: {
		uint64_t n;

		memcpy(&n, data, sizeof(n));
		length = snprintf(buffer, sizeof(buffer), "%" PRIu64, n);
		break;
	}
	case HX_OS_CODEC_FLOAT:
	case HX_OS_CODEC_DOUBLE: {
		double n;

		if (t->kind == HX_OS_CODEC_FLOAT) {
			float f;

			memcpy(&f, data, sizeof(f));
			n = f;
		}
		else
			memcpy(&n, data, sizeof(n));

		status = hx_os_i_format_double(
			n, buffer, sizeof(buffer), &required);
		if (status == HX_OS_OK)
			value.bytes = required - 1;

		kind = HX_OS_CONFIG_REAL;
		break;
	}
	case HX_OS_CODEC_STRING: {
		hx_string_view view;

		memcpy(&view, data, sizeof(view));
		value.data = view.data;
		value.bytes = view.size;
		kind = HX_OS_CONFIG_STRING;
		break;
	}
	case HX_OS_CODEC_BUFFER:
		value.data = data;
		while (value.bytes < t->size && value.data[value.bytes])
			++value.bytes;

		if (value.bytes == t->size)
			status = HX_OS_INVALID;

		kind = HX_OS_CONFIG_STRING;
		break;

	default:
		status = HX_OS_INVALID;
		break;
	}

	if (length < 0 || (size_t)length >= sizeof(buffer))
		status = HX_OS_RANGE;

	if (length > 0)
		value.bytes = (size_t)length;

	if (status != HX_OS_OK) {
		(void)hx_os_codec_fail(p, status, "Cannot encode scalar field");

		return NULL;
	}

	if (!hx_os_codec_text(p, value))
		return NULL;

	if (format == HX_OS_CONFIG_XML) {
		if (!hx_os_cfg_xml_valid(value, 0)) {
			(void)hx_os_codec_fail(p, HX_OS_UNREPRESENTABLE,
				"Scalar contains characters forbidden in XML");

			return NULL;
		}

		node = hx_os_codec_node(p,
			attribute ? HX_OS_CONFIG_ATTRIBUTE
				  : HX_OS_CONFIG_ELEMENT,
			name, depth);
		text = attribute ? node
				 : hx_os_codec_node(p, HX_OS_CONFIG_TEXT, NULL,
					   depth + 1);
		if (!node || !text)
			return NULL;

		text->value = hx_os_cfg_copy(p, value.data, value.bytes);
		if (!attribute && !hx_os_cfg_append(p, node, text, 0))
			return NULL;
	}
	else {
		node = hx_os_codec_node(p,
			hx_os_codec_plain(format) ? HX_OS_CONFIG_STRING : kind,
			name, depth);
		if (node)
			node->value =
				hx_os_cfg_copy(p, value.data, value.bytes);
	}

	return p->status == HX_OS_OK ? node : NULL;
}

static hx_os_config_node *hx_os_codec_encode_value(hx_os_cfg_parser *p,
	const hx_os_codec_type *t, const void *data, int format,
	const char *name, size_t depth, int attribute)
{
	hx_os_config_node *node, *child;
	hx_os_codec_array span;
	size_t i, bytes;

	if (!hx_os_cfg_work(p, 1))
		return NULL;

	if (t->kind == HX_OS_CODEC_OBJECT)
		return hx_os_codec_encode_object(
			p, t->schema, data, format, name, depth);

	if (t->kind == HX_OS_CODEC_NODE) {
		const hx_os_config_node *source;

		/* MSVC x86 otherwise diagnoses the nested pointer qualifier. */
		memcpy((void *)&source, data, sizeof(source));
		if (!source ||
			(format == HX_OS_CONFIG_XML &&
				source->kind != HX_OS_CONFIG_ELEMENT)) {
			(void)hx_os_codec_fail(p, HX_OS_UNREPRESENTABLE,
				"NODE requires a live subtree compatible with "
				"the format");

			return NULL;
		}

		node = hx_os_cfg_clone(p, source, depth);
		if (node) {
			node->name = hx_os_cfg_copy(p, name, strlen(name));
			if (format == HX_OS_CONFIG_XML &&
				!hx_os_cfg_xml_valid(node->name, 1))
				(void)hx_os_codec_fail(p, HX_OS_UNREPRESENTABLE,
					"NODE field name cannot be represented "
					"as XML");
		}

		return p->status == HX_OS_OK ? node : NULL;
	}

	if (t->kind != HX_OS_CODEC_ARRAY && t->kind != HX_OS_CODEC_FIXED_ARRAY)
		return hx_os_codec_encode_scalar(
			p, t, data, format, name, depth, attribute);

	if (t->kind == HX_OS_CODEC_ARRAY)
		memcpy(&span, data, sizeof(span));
	else {
		span.data = data;
		span.count = t->count;
	}

	if ((span.count && !span.data) ||
		!hx_os_codec_multiply(span.count, t->element->size, &bytes) ||
		span.count > p->document->options.max_nodes) {
		(void)hx_os_codec_fail(
			p, HX_OS_LIMIT, "Invalid or oversized array");

		return NULL;
	}

	node = hx_os_codec_node(p,
		format == HX_OS_CONFIG_XML ? HX_OS_CONFIG_ELEMENT
					   : HX_OS_CONFIG_ARRAY,
		name, depth);
	if (!node)
		return NULL;

	for (i = 0; i < span.count && p->status == HX_OS_OK; ++i) {
		child = hx_os_codec_encode_value(p, t->element,
			(const unsigned char *)span.data + i * t->element->size,
			format, "item", depth + 1, 0);
		if (!hx_os_cfg_append(p, node, child, 0))
			return NULL;
	}

	return p->status == HX_OS_OK ? node : NULL;
}

static int hx_os_codec_space_node(const hx_os_config_node *node)
{
	size_t i;

	if (node->kind != HX_OS_CONFIG_TEXT)
		return 0;

	for (i = 0; i < node->value.bytes; ++i)
		if (!hx_os_cfg_space((unsigned char)node->value.data[i]))
			return 0;

	return 1;
}

static const hx_os_config_node *hx_os_codec_find(hx_os_cfg_parser *p,
	const hx_os_config_node *node, const hx_os_codec_field *field, int xml)
{
	const hx_os_config_node *child, *found = NULL;

	size_t length = strlen(field->name);
	if (xml && (field->flags & HX_OS_CODEC_XML_ATTRIBUTE))
		node = node->attributes;

	for (child = node ? node->first : NULL; child; child = child->next) {
		if (!hx_os_cfg_work(p, length + 1))
			return NULL;

		if (child->name.bytes == length &&
			!memcmp(child->name.data, field->name, length)) {
			if (found) {
				(void)hx_os_codec_fail(p, HX_OS_EXISTS,
					"Repeated scalar/object field; declare "
					"an array container");

				return NULL;
			}

			found = child;
		}
	}

	return found;
}

static int hx_os_codec_decode_value(hx_os_cfg_parser *p,
	const hx_os_codec_type *type, const hx_os_config_node *node, void *out,
	int format, size_t depth);

static int hx_os_codec_decode_object(hx_os_cfg_parser *p,
	const hx_os_codec_schema *schema, const hx_os_config_node *node,
	void *out, int format, size_t depth)
{
	size_t i;
	int xml = format == HX_OS_CONFIG_XML;
	const hx_os_config_node *child;

	if (!node ||
		node->kind !=
			(xml ? HX_OS_CONFIG_ELEMENT : HX_OS_CONFIG_OBJECT))
		return hx_os_codec_fail(
			p, HX_OS_TYPE_MISMATCH, "Object field expected");
	if (xml) {
		for (child = node->first; child; child = child->next) {
			if (!hx_os_cfg_work(p, child->value.bytes + 1))
				return 0;

			if (child->kind != HX_OS_CONFIG_ELEMENT &&
				!hx_os_codec_space_node(child))
				return hx_os_codec_fail(p, HX_OS_TYPE_MISMATCH,
					"Mixed XML content requires a NODE "
					"field");
		}
	}

	for (i = 0; i < schema->count; ++i) {
		const hx_os_codec_field *f = schema->fields + i;

		child = hx_os_codec_find(p, node, f, xml);
		if (p->status != HX_OS_OK)
			return 0;

		if (!child && (f->flags & HX_OS_CODEC_OPTIONAL))
			continue;

		if (!child)
			return hx_os_codec_fail(p, HX_OS_NOT_FOUND,
				"Required field is missing");

		if (!hx_os_codec_decode_value(p, &f->type, child,
			    (unsigned char *)out + f->offset, format,
			    depth + 1))
			return 0;
	}

	return 1;
}

static int hx_os_codec_xml_scalar(hx_os_cfg_parser *p,
	const hx_os_config_node *node, hx_os_config_node *scalar)
{
	const hx_os_config_node *child;
	size_t size = 0, offset = 0;
	char *text;

	*scalar = *node;
	if (node->kind == HX_OS_CONFIG_ATTRIBUTE)
		return 1;

	if (node->kind != HX_OS_CONFIG_ELEMENT)
		return hx_os_codec_fail(
			p, HX_OS_TYPE_MISMATCH, "XML scalar expected");

	if (node->attributes && node->attributes->count)
		return hx_os_codec_fail(p, HX_OS_TYPE_MISMATCH,
			"Scalar XML field cannot discard attributes");

	for (child = node->first; child; child = child->next) {
		if (!hx_os_cfg_work(p, child->value.bytes + 1))
			return 0;

		if (child->kind != HX_OS_CONFIG_TEXT)
			return hx_os_codec_fail(p, HX_OS_TYPE_MISMATCH,
				"Scalar XML field cannot contain child "
				"elements");

		if (!hx_os_i_add_size(size, child->value.bytes, &size) ||
			size == SIZE_MAX)
			return hx_os_codec_fail(
				p, HX_OS_LIMIT, "XML scalar limit exceeded");
	}

	text = hx_os_cfg_alloc(p, size + 1);
	if (!text)
		return 0;

	for (child = node->first; child; child = child->next) {
		memcpy(text + offset, child->value.data, child->value.bytes);
		offset += child->value.bytes;
	}

	text[size] = 0;
	scalar->kind = HX_OS_CONFIG_STRING;
	scalar->value.data = text;
	scalar->value.bytes = size;

	return 1;
}

static int hx_os_codec_decode_scalar(hx_os_cfg_parser *p,
	const hx_os_codec_type *t, const hx_os_config_node *node, void *out,
	int format)
{
	hx_os_config_node xml_value;
	hx_os_status status = HX_OS_OK;

	int strings = hx_os_codec_plain(format) || format == HX_OS_CONFIG_XML;
	if (format == HX_OS_CONFIG_XML) {
		if (!hx_os_codec_xml_scalar(p, node, &xml_value))
			return 0;

		node = &xml_value;
	}

	if (!strings) {
		int compatible = t->kind == HX_OS_CODEC_STRING ||
				t->kind == HX_OS_CODEC_BUFFER
			? node->kind == HX_OS_CONFIG_STRING
			: t->kind == HX_OS_CODEC_BOOL
			? node->kind == HX_OS_CONFIG_BOOL
			: (t->kind == HX_OS_CODEC_FLOAT ||
				  t->kind == HX_OS_CODEC_DOUBLE)
			? (node->kind == HX_OS_CONFIG_REAL ||
				  node->kind == HX_OS_CONFIG_INTEGER)
			: node->kind == HX_OS_CONFIG_INTEGER;

		if (!compatible)
			return hx_os_codec_fail(
				p, HX_OS_TYPE_MISMATCH, "Field type mismatch");
	}

	switch (t->kind) {
	case HX_OS_CODEC_STRING:
	case HX_OS_CODEC_BUFFER: {
		hx_os_config_slice value;

		status = hx_os_config_get_string(node, &value);
		if (status != HX_OS_OK)
			break;

		if (t->kind == HX_OS_CODEC_BUFFER) {
			if (value.bytes >= t->size ||
				memchr(value.data, 0, value.bytes)) {
				status = HX_OS_UNREPRESENTABLE;
				break;
			}

			memcpy(out, value.data, value.bytes);
			((char *)out)[value.bytes] = 0;
		}
		else {
			hx_string_view view = {value.data, value.bytes};

			memcpy(out, &view, sizeof(view));
		}

		break;
	}
	case HX_OS_CODEC_BOOL: {
		int value;

		status = hx_os_config_get_bool(node, &value);
		if (status == HX_OS_OK) {
			if (t->size == 1)
				*(unsigned char *)out = (unsigned char)value;
			else
				memcpy(out, &value, sizeof(value));
		}

		break;
	}
	case HX_OS_CODEC_I8:
	case HX_OS_CODEC_I16: {
		int64_t value;
		int64_t low = t->kind == HX_OS_CODEC_I8 ? INT8_MIN : INT16_MIN;
		int64_t high = t->kind == HX_OS_CODEC_I8 ? INT8_MAX : INT16_MAX;

		status = hx_os_config_get_i64(node, &value);
		if (status == HX_OS_OK) {
			if (value < low || value > high)
				status = HX_OS_RANGE;
			else if (t->kind == HX_OS_CODEC_I8) {
				int8_t n = (int8_t)value;

				memcpy(out, &n, sizeof(n));
			}
			else {
				int16_t n = (int16_t)value;

				memcpy(out, &n, sizeof(n));
			}
		}

		break;
	}
	case HX_OS_CODEC_U8:
	case HX_OS_CODEC_U16: {
		uint64_t value;
		uint64_t high =
			t->kind == HX_OS_CODEC_U8 ? UINT8_MAX : UINT16_MAX;

		status = hx_os_config_get_u64(node, &value);
		if (status == HX_OS_OK) {
			if (value > high)
				status = HX_OS_RANGE;
			else if (t->kind == HX_OS_CODEC_U8) {
				uint8_t n = (uint8_t)value;

				memcpy(out, &n, sizeof(n));
			}
			else {
				uint16_t n = (uint16_t)value;

				memcpy(out, &n, sizeof(n));
			}
		}

		break;
	}
	case HX_OS_CODEC_I32:
	case HX_OS_CODEC_I64: {
		int64_t value;

		status = hx_os_config_get_i64(node, &value);
		if (status == HX_OS_OK && t->kind == HX_OS_CODEC_I32) {
			int32_t narrow;

			if (value < INT32_MIN || value > INT32_MAX)
				status = HX_OS_RANGE;
			else {
				narrow = (int32_t)value;
				memcpy(out, &narrow, sizeof(narrow));
			}
		}
		else if (status == HX_OS_OK)
			memcpy(out, &value, sizeof(value));

		break;
	}
	case HX_OS_CODEC_U32:
	case HX_OS_CODEC_U64: {
		uint64_t value;

		status = hx_os_config_get_u64(node, &value);
		if (status == HX_OS_OK && t->kind == HX_OS_CODEC_U32) {
			uint32_t narrow;

			if (value > UINT32_MAX)
				status = HX_OS_RANGE;
			else {
				narrow = (uint32_t)value;
				memcpy(out, &narrow, sizeof(narrow));
			}
		}
		else if (status == HX_OS_OK)
			memcpy(out, &value, sizeof(value));

		break;
	}
	case HX_OS_CODEC_FLOAT:
	case HX_OS_CODEC_DOUBLE: {
		double value;

		status = hx_os_config_get_double(node, &value);
		if (status == HX_OS_OK && t->kind == HX_OS_CODEC_FLOAT) {
			float narrow;

			if (isfinite(value) &&
				(value > FLT_MAX || value < -FLT_MAX))
				status = HX_OS_RANGE;
			else {
				narrow = (float)value;
				if (value != 0 && narrow == 0)
					status = HX_OS_RANGE;
				else
					memcpy(out, &narrow, sizeof(narrow));
			}
		}
		else if (status == HX_OS_OK)
			memcpy(out, &value, sizeof(value));

		break;
	}

	default:
		status = HX_OS_INVALID;
		break;
	}

	return status == HX_OS_OK ||
		hx_os_codec_fail(p, status, "Cannot decode scalar field");
}

static int hx_os_codec_decode_value(hx_os_cfg_parser *p,
	const hx_os_codec_type *t, const hx_os_config_node *node, void *out,
	int format, size_t depth)
{
	const hx_os_config_node *child;
	hx_os_codec_array span;
	void *storage;
	size_t count = 0, i = 0, bytes;
	int xml = format == HX_OS_CONFIG_XML;

	if (!hx_os_cfg_work(p, 1))
		return 0;

	if (depth > p->document->options.max_depth)
		return hx_os_codec_fail(
			p, HX_OS_LIMIT, "Decoded value depth exceeded");

	if (t->kind == HX_OS_CODEC_OBJECT)
		return hx_os_codec_decode_object(
			p, t->schema, node, out, format, depth);

	if (t->kind == HX_OS_CODEC_NODE) {
		memcpy(out, &node, sizeof(node));

		return 1;
	}

	if (t->kind != HX_OS_CODEC_ARRAY && t->kind != HX_OS_CODEC_FIXED_ARRAY)
		return hx_os_codec_decode_scalar(p, t, node, out, format);

	if (node->kind != (xml ? HX_OS_CONFIG_ELEMENT : HX_OS_CONFIG_ARRAY))
		return hx_os_codec_fail(
			p, HX_OS_TYPE_MISMATCH, "Array field expected");

	if (xml && node->attributes && node->attributes->count)
		return hx_os_codec_fail(p, HX_OS_TYPE_MISMATCH,
			"Array XML container cannot discard attributes");

	for (child = node->first; child; child = child->next) {
		if (!hx_os_cfg_work(p, child->value.bytes + 1))
			return 0;

		if (xml && hx_os_codec_space_node(child))
			continue;

		if (xml &&
			(child->kind != HX_OS_CONFIG_ELEMENT ||
				!hx_os_cfg_equal(child->name, "item")))
			return hx_os_codec_fail(p, HX_OS_TYPE_MISMATCH,
				"XML array children must be item elements");
		++count;
	}

	if (t->kind == HX_OS_CODEC_FIXED_ARRAY && count != t->count)
		return hx_os_codec_fail(
			p, HX_OS_RANGE, "Fixed array length mismatch");

	if (!hx_os_codec_multiply(count, t->element->size, &bytes))
		return hx_os_codec_fail(
			p, HX_OS_LIMIT, "Decoded array size overflow");

	storage = t->kind == HX_OS_CODEC_ARRAY
		? (bytes ? hx_os_cfg_alloc(p, bytes) : NULL)
		: out;
	if (bytes && !storage)
		return 0;

	if (bytes)
		memset(storage, 0, bytes);

	for (child = node->first; child; child = child->next) {
		if (xml && hx_os_codec_space_node(child))
			continue;

		if (!hx_os_codec_decode_value(p, t->element, child,
			    (unsigned char *)storage + i * t->element->size,
			    format, depth + 1))
			return 0;
		++i;
	}

	if (t->kind == HX_OS_CODEC_ARRAY) {
		span.data = storage;
		span.count = count;
		memcpy(out, &span, sizeof(span));
	}

	return 1;
}
#endif

static void hx_os_codec_begin(hx_os_cfg_parser *p,
	const hx_os_config_options *options, hx_os_config_error *error)
{
	memset(p, 0, sizeof(*p));
	p->error = error;
	hx_os_cfg_error_init(error,
		options && options->size == sizeof(*options)
			? options->source_name
			: NULL);
}

hx_os_status hx_os_codec_encode(const hx_os_codec_schema *schema,
	const void *object, int format, hx_string *out,
	const hx_os_config_options *input, hx_os_config_error *error)
{
	hx_os_cfg_parser p;

	hx_os_codec_begin(&p, input, error);
#if HX_OS_ENABLE_CODEC && HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	{
		hx_os_config_options options;
		hx_os_error native;
		hx_os_status status;

		if (!object || !out)
			return hx_os_cfg_fail(&p, HX_OS_INVALID, SIZE_MAX,
				"Missing codec input");

		if (!hx_os_cfg_options(&p, input, &options))
			return p.status;

		options.backend = HX_OS_CONFIG_BUILTIN;
		if (!hx_os_cfg_document_create(&p, out->context, &options))
			return p.status;

		if (hx_os_codec_check_schema(&p, schema, 1)) {
			p.length = 0;
			p.document->root = hx_os_codec_encode_object(&p, schema,
				object, format, schema->xml_name, 1);
		}

		status = p.status;
		if (status == HX_OS_OK) {
			status = hx_os_config_serialize(
				p.document, format, out, &options, &native);
			if (status != HX_OS_OK) {
				(void)hx_os_cfg_fail(&p, status, SIZE_MAX,
					"Mapped object cannot be written in "
					"this format or limits");
				if (error)
					error->detail = native;
			}
		}

		hx_os_config_release(p.document);

		return status;
	}
#else
	(void)schema;
	(void)object;
	(void)format;
	(void)out;

	return hx_os_cfg_fail(
		&p, HX_OS_NOT_SUPPORTED, SIZE_MAX, "Codec is disabled");
#endif
}

static hx_os_status hx_os_codec_finish(hx_os_context *context,
	const hx_os_codec_schema *schema, int format, const void *text,
	size_t bytes, const char *path, const hx_os_config_options *options,
	hx_os_codec_result **out, hx_os_config_error *error)
{
	hx_os_cfg_parser p;

	hx_os_codec_begin(&p, options, error);
	if (out)
		*out = NULL;
#if HX_OS_ENABLE_CODEC && HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	{
		hx_os_codec_result *result = NULL;
		hx_os_status status;

		if (!out)
			return hx_os_cfg_fail(&p, HX_OS_INVALID, SIZE_MAX,
				"Missing codec output");

		status = path ? hx_os_config_load(context, format, path,
					options, &p.document, error)
			      : hx_os_config_parse(context, format, text, bytes,
					options, &p.document, error);
		if (status != HX_OS_OK)
			return status;

		if (!hx_os_codec_check_schema(&p, schema, 1))
			goto failed;

		if (format == HX_OS_CONFIG_XML &&
			(!schema->xml_name ||
				!hx_os_cfg_equal(p.document->root->name,
					schema->xml_name))) {
			(void)hx_os_codec_fail(&p, HX_OS_TYPE_MISMATCH,
				"Unexpected XML root element");
			goto failed;
		}

		result = hx_os_cfg_alloc(&p, sizeof(*result));
		if (!result)
			goto failed;

		result->document = p.document;
		result->data = hx_os_cfg_alloc(&p, schema->size);
		if (!result->data)
			goto failed;

		memset(result->data, 0, schema->size);
		if (!hx_os_codec_decode_object(&p, schema, p.document->root,
			    result->data, format, 1))
			goto failed;
		*out = result;

		return HX_OS_OK;

	failed:
		hx_os_config_release(p.document);
		return p.status;
	}
#else
	(void)context;
	(void)schema;
	(void)format;
	(void)text;
	(void)bytes;
	(void)path;

	return hx_os_cfg_fail(
		&p, HX_OS_NOT_SUPPORTED, SIZE_MAX, "Codec is disabled");
#endif
}

hx_os_status hx_os_codec_decode(hx_os_context *context,
	const hx_os_codec_schema *schema, int format, const void *text,
	size_t bytes, const hx_os_config_options *options,
	hx_os_codec_result **out, hx_os_config_error *error)
{
	return hx_os_codec_finish(context, schema, format, text, bytes, NULL,
		options, out, error);
}

const void *hx_os_codec_data(const hx_os_codec_result *result)
{
	return result ? result->data : NULL;
}

void hx_os_codec_release(hx_os_codec_result *result)
{
	if (result)
		hx_os_config_release(result->document);
}

hx_os_status hx_os_codec_load(hx_os_context *context,
	const hx_os_codec_schema *schema, int format, const char *path,
	const hx_os_config_options *options, hx_os_codec_result **out,
	hx_os_config_error *error)
{
	return hx_os_codec_finish(
		context, schema, format, NULL, 0, path, options, out, error);
}

hx_os_status hx_os_codec_save(const hx_os_codec_schema *schema,
	const void *object, int format, const char *path,
	const hx_os_config_options *options, hx_os_config_error *error)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error native;

	hx_os_status status = hx_os_codec_encode(
		schema, object, format, &text, options, error);
	if (status == HX_OS_OK) {
		status = hx_os_file_save_atomic(
			path, hx_string_c_str(&text), text.size, &native);
		if (status != HX_OS_OK && error) {
			hx_os_cfg_parser p;

			hx_os_codec_begin(&p, options, error);
			(void)hx_os_cfg_fail(&p, status, SIZE_MAX,
				"Cannot save mapped object");
			error->detail = native;
		}
	}

	hx_string_destroy(&text);

	return status;
}
