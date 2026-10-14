#include "config_private.h"
#include "number_private.h"
#include <inttypes.h>
#include <stdio.h>

#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
typedef struct hx_os_cfg_writer {
	hx_os_config_options options;
	char *buffer;
	size_t position;
	size_t nodes;
	size_t work;
	hx_os_status status;
} hx_os_cfg_writer;

static int hx_os_cfg_write_work(hx_os_cfg_writer *writer, size_t amount)
{
	if (writer->status != HX_OS_OK)
		return 0;

	if (amount > writer->options.max_work - writer->work) {
		writer->status = HX_OS_LIMIT;

		return 0;
	}

	writer->work += amount;

	return 1;
}

static void hx_os_cfg_write(
	hx_os_cfg_writer *writer, const char *data, size_t bytes)
{
	if (!hx_os_cfg_write_work(writer, bytes))
		return;

	if (bytes > writer->options.max_input_bytes - writer->position) {
		writer->status = HX_OS_LIMIT;

		return;
	}

	if (writer->buffer && bytes)
		memcpy(writer->buffer + writer->position, data, bytes);

	writer->position += bytes;
}

static int hx_os_cfg_write_node(hx_os_cfg_writer *writer, size_t depth)
{
	if (!hx_os_cfg_write_work(writer, 1))
		return 0;

	if (depth > writer->options.max_depth ||
		writer->nodes == writer->options.max_nodes) {
		writer->status = HX_OS_LIMIT;

		return 0;
	}

	++writer->nodes;

	return 1;
}

static void hx_os_cfg_write_string(
	hx_os_cfg_writer *writer, hx_os_config_slice value, int json)
{
	static const char hex[] = "0123456789abcdef";
	size_t i;

	if (json)
		hx_os_cfg_write(writer, "\"", 1);

	for (i = 0; i < value.bytes && writer->status == HX_OS_OK; ++i) {
		unsigned char c = (unsigned char)value.data[i];

		if (!json && value.bytes - i >= 3 &&
			!memcmp(value.data + i, "\xef\xbb\xbf", 3)) {
			// A leading properties key must not turn into a BOM.
			hx_os_cfg_write(writer, "\\ufeff", 6);
			i += 2;
			continue;
		}

		// YAML treats NEL and Unicode separators as line breaks even in
		// quoted scalars. Escape them and the C1 controls explicitly.
		if (json == 2 && c >= 0x80) {
			uint32_t point;
			size_t used;

			if (hx_os_i_utf_decode(HX_OS_UTF8,
				    (const unsigned char *)value.data + i,
				    value.bytes - i, &point,
				    &used) != HX_OS_OK) {
				writer->status = HX_OS_INVALID_ENCODING;

				return;
			}

			if ((point >= 0x80 && point < 0xa0) ||
				point == 0x2028 || point == 0x2029 ||
				point == 0xfffe || point == 0xffff) {
				char escape[6] = {'\\', 'u', 0, 0, 0, 0};

				escape[2] = hex[(point >> 12) & 15];
				escape[3] = hex[(point >> 8) & 15];
				escape[4] = hex[(point >> 4) & 15];
				escape[5] = hex[point & 15];
				hx_os_cfg_write(writer, escape, sizeof(escape));
				i += used - 1;
				continue;
			}

			hx_os_cfg_write(writer, value.data + i, used);
			i += used - 1;
			continue;
		}

		if (c < 0x20 || (json == 2 && c == 0x7f)) {
			char escape[6] = {'\\', 'u', '0', '0', 0, 0};

			escape[4] = hex[c >> 4];
			escape[5] = hex[c & 15];
			hx_os_cfg_write(writer, escape, sizeof(escape));
		}
		else if (c == '\\' || (json && c == '"') ||
			(!json &&
				(c == ' ' || c == '=' || c == ':' || c == '#' ||
					c == '!'))) {
			hx_os_cfg_write(writer, "\\", 1);
			hx_os_cfg_write(writer, value.data + i, 1);
		}
		else {
			size_t start = i++;
			size_t span = value.bytes - start;

			// Do not scan beyond the remaining work/output budgets.
			if (span > writer->options.max_work - writer->work)
				span = writer->options.max_work - writer->work;
			if (span > writer->options.max_input_bytes -
					writer->position)
				span = writer->options.max_input_bytes -
					writer->position;
			while (i - start < span) {
				c = (unsigned char)value.data[i];
				if (c < 0x20 || c == '\\' ||
					(json && c == '"') ||
					(json == 2 && c >= 0x7f) ||
					(!json &&
						(c == ' ' || c == '=' ||
							c == ':' || c == '#' ||
							c == '!' || c == 0xef)))
					break;
				++i;
			}
			hx_os_cfg_write(writer, value.data + start, i - start);
			--i;
		}
	}

	if (json)
		hx_os_cfg_write(writer, "\"", 1);
}

static void hx_os_cfg_write_number(
	hx_os_cfg_writer *writer, const hx_os_config_node *node)
{
	hx_os_config_slice value = node->value;
	int real, negative;
	uint64_t magnitude;
	size_t i;

	if (!hx_os_cfg_write_work(writer, value.bytes))
		return;

	if (hx_os_i_number_valid(value.data, value.bytes, 0, &real)) {
		if (node->kind == HX_OS_CONFIG_INTEGER && real) {
			writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		hx_os_cfg_write(writer, value.data, value.bytes);
		if (node->kind == HX_OS_CONFIG_REAL && !real)
			hx_os_cfg_write(writer, ".0", 2);

		return;
	}

	if (node->kind == HX_OS_CONFIG_INTEGER) {
		char text[32];
		int length;

		if (hx_os_i_number_integer(value.data, value.bytes, &magnitude,
			    &negative) != HX_OS_OK) {
			writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		length = snprintf(text, sizeof(text), "%" PRIu64, magnitude);
		if (length < 0 || (size_t)length >= sizeof(text)) {
			writer->status = HX_OS_RANGE;

			return;
		}

		if (negative)
			hx_os_cfg_write(writer, "-", 1);

		hx_os_cfg_write(writer, text, (size_t)length);

		return;
	}

	i = value.bytes && (value.data[0] == '+' || value.data[0] == '-') ? 1
									  : 0;
	if (!hx_os_i_number_valid(value.data, value.bytes, 1, &real) || !real ||
		(value.bytes - i == 3 &&
			(value.data[i] == 'i' || value.data[i] == 'n'))) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	for (i = value.data[0] == '+' ? 1 : 0;
		i < value.bytes && writer->status == HX_OS_OK; ++i)
		if (value.data[i] != '_')
			hx_os_cfg_write(writer, value.data + i, 1);
}

// Input documents are immutable trees; recursion is limited to 128 frames.
static void hx_os_cfg_write_json(
	hx_os_cfg_writer *writer, const hx_os_config_node *node, size_t depth)
{
	const hx_os_config_node *child;
	int object;

	if (!hx_os_cfg_write_node(writer, depth))
		return;

	switch (node->kind) {
	case HX_OS_CONFIG_NULL:
		hx_os_cfg_write(writer, "null", 4);
		break;
	case HX_OS_CONFIG_BOOL:
		if (hx_os_cfg_equal(node->value, "true"))
			hx_os_cfg_write(writer, "true", 4);
		else if (hx_os_cfg_equal(node->value, "false"))
			hx_os_cfg_write(writer, "false", 5);
		else
			writer->status = HX_OS_UNREPRESENTABLE;

		break;
	case HX_OS_CONFIG_STRING:
		hx_os_cfg_write_string(writer, node->value, 1);
		break;
	case HX_OS_CONFIG_INTEGER:
	case HX_OS_CONFIG_REAL:
		hx_os_cfg_write_number(writer, node);
		break;
	case HX_OS_CONFIG_OBJECT:
	case HX_OS_CONFIG_ARRAY:
		object = node->kind == HX_OS_CONFIG_OBJECT;
		hx_os_cfg_write(writer, object ? "{" : "[", 1);

		for (child = node->first; child && writer->status == HX_OS_OK;
			child = child->next) {
			if (child != node->first)
				hx_os_cfg_write(writer, ",", 1);

			if (object) {
				hx_os_cfg_write_string(writer, child->name, 1);
				hx_os_cfg_write(writer, ":", 1);
			}

			hx_os_cfg_write_json(writer, child, depth + 1);
		}

		hx_os_cfg_write(writer, object ? "}" : "]", 1);
		break;

	default:
		writer->status = HX_OS_UNREPRESENTABLE;
		break;
	}
}

static int hx_os_cfg_ini_representable(
	hx_os_cfg_writer *writer, hx_os_config_slice value, int role)
{
	size_t i;

	if (!hx_os_cfg_write_work(writer, value.bytes))
		return 0;

	if (!value.bytes)
		return role == 0;

	if (hx_os_cfg_space((unsigned char)value.data[0]) ||
		hx_os_cfg_space((unsigned char)value.data[value.bytes - 1]))
		return 0;
	if (role == 1 &&
		(value.data[0] == '[' || value.data[0] == '#' ||
			value.data[0] == ';'))
		return 0;
	for (i = 0; i < value.bytes; ++i) {
		unsigned char c = (unsigned char)value.data[i];

		if ((c < 0x20 && c != '\t') ||
			(role == 1 && (c == '=' || c == ':')) ||
			(role == 2 && c == ']') ||
			(role == 0 && i && c == ';' &&
				hx_os_cfg_space(
					(unsigned char)value.data[i - 1])))
			return 0;
	}

	return 1;
}

static void hx_os_cfg_write_assignment(hx_os_cfg_writer *writer,
	const hx_os_config_node *node, int properties, size_t depth)
{
	if (!hx_os_cfg_write_node(writer, depth))
		return;

	if (node->kind != HX_OS_CONFIG_STRING) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	if (properties)
		hx_os_cfg_write_string(writer, node->name, 0);
	else {
		if (!writer->position && node->name.bytes >= 3 &&
			!memcmp(node->name.data, "\xef\xbb\xbf", 3)) {
			writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		if (!hx_os_cfg_ini_representable(writer, node->name, 1) ||
			!hx_os_cfg_ini_representable(writer, node->value, 0)) {
			if (writer->status == HX_OS_OK)
				writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		hx_os_cfg_write(writer, node->name.data, node->name.bytes);
	}

	hx_os_cfg_write(writer, "=", 1);
	if (properties)
		hx_os_cfg_write_string(writer, node->value, 0);
	else
		hx_os_cfg_write(writer, node->value.data, node->value.bytes);

	hx_os_cfg_write(writer, "\n", 1);
}

static void hx_os_cfg_write_lines(
	hx_os_cfg_writer *writer, const hx_os_config_node *root, int properties)
{
	const hx_os_config_node *node, *child;

	if (!hx_os_cfg_write_node(writer, 1))
		return;

	if (root->kind != HX_OS_CONFIG_OBJECT) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	// Global INI keys precede sections; preserve order inside each group.
	for (node = root->first; node && writer->status == HX_OS_OK;
		node = node->next) {
		if (!hx_os_cfg_write_work(writer, 1))
			return;

		if (properties || node->kind != HX_OS_CONFIG_OBJECT)
			hx_os_cfg_write_assignment(writer, node, properties, 2);
	}

	if (properties)
		return;

	for (node = root->first; node && writer->status == HX_OS_OK;
		node = node->next) {
		if (!hx_os_cfg_write_work(writer, 1))
			return;

		if (node->kind != HX_OS_CONFIG_OBJECT)
			continue;

		if (!hx_os_cfg_write_node(writer, 2))
			return;

		if (!hx_os_cfg_ini_representable(writer, node->name, 2)) {
			if (writer->status == HX_OS_OK)
				writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		hx_os_cfg_write(writer, "[", 1);
		hx_os_cfg_write(writer, node->name.data, node->name.bytes);
		hx_os_cfg_write(writer, "]\n", 2);

		for (child = node->first; child && writer->status == HX_OS_OK;
			child = child->next)
			hx_os_cfg_write_assignment(writer, child, 0, 3);
	}
}

hx_os_status hx_os_cfg_ini_validate(const hx_os_config_document *document)
{
	hx_os_cfg_writer writer;

	memset(&writer, 0, sizeof(writer));
	writer.options = document->options;
	hx_os_cfg_write_lines(&writer, document->root, 0);

	return writer.status;
}

static void hx_os_cfg_write_indent(hx_os_cfg_writer *writer, size_t depth)
{
	while (depth-- && writer->status == HX_OS_OK)
		hx_os_cfg_write(writer, "  ", 2);
}

static void hx_os_cfg_write_yaml(
	hx_os_cfg_writer *writer, const hx_os_config_node *node, size_t depth)
{
	const hx_os_config_node *child;
	int object = node->kind == HX_OS_CONFIG_OBJECT;

	if (node->kind != HX_OS_CONFIG_ARRAY && !object) {
		if (node->kind == HX_OS_CONFIG_STRING) {
			if (hx_os_cfg_write_node(writer, depth))
				hx_os_cfg_write_string(writer, node->value, 2);
		}
		else
			hx_os_cfg_write_json(writer, node, depth);

		hx_os_cfg_write(writer, "\n", 1);

		return;
	}

	if (!hx_os_cfg_write_node(writer, depth))
		return;

	if (!node->count) {
		hx_os_cfg_write(writer, object ? "{}\n" : "[]\n", 3);

		return;
	}

	for (child = node->first; child && writer->status == HX_OS_OK;
		child = child->next) {
		hx_os_cfg_write_indent(writer, depth - 1);
		if (object) {
			hx_os_cfg_write_string(writer, child->name, 2);
			hx_os_cfg_write(writer, ":", 1);
		}
		else
			hx_os_cfg_write(writer, "-", 1);

		if ((child->kind == HX_OS_CONFIG_OBJECT ||
			    child->kind == HX_OS_CONFIG_ARRAY) &&
			child->count)
			hx_os_cfg_write(writer, "\n", 1);
		else
			hx_os_cfg_write(writer, " ", 1);

		hx_os_cfg_write_yaml(writer, child, depth + 1);
	}
}

static void hx_os_cfg_write_toml(
	hx_os_cfg_writer *writer, const hx_os_config_node *node, size_t depth)
{
	const hx_os_config_node *child;
	int object = node->kind == HX_OS_CONFIG_OBJECT;

	if (node->kind == HX_OS_CONFIG_NULL ||
		(node->kind >= HX_OS_CONFIG_ELEMENT &&
			node->kind != HX_OS_CONFIG_DATETIME)) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	if (!hx_os_cfg_write_node(writer, depth))
		return;

	if (node->kind == HX_OS_CONFIG_STRING)
		hx_os_cfg_write_string(writer, node->value, 2);
	else if (node->kind == HX_OS_CONFIG_DATETIME)
		hx_os_cfg_write(writer, node->value.data, node->value.bytes);
	else if (node->kind == HX_OS_CONFIG_INTEGER) {
		int64_t integer;

		if (hx_os_config_get_i64(node, &integer) != HX_OS_OK)
			writer->status = HX_OS_UNREPRESENTABLE;
		else
			hx_os_cfg_write_number(writer, node);
	}
	else if (node->kind == HX_OS_CONFIG_REAL ||
		node->kind == HX_OS_CONFIG_BOOL) {
		if (node->kind == HX_OS_CONFIG_REAL &&
			(hx_os_cfg_equal(node->value, "nan") ||
				hx_os_cfg_equal(node->value, "+nan") ||
				hx_os_cfg_equal(node->value, "-nan") ||
				hx_os_cfg_equal(node->value, "inf") ||
				hx_os_cfg_equal(node->value, "+inf") ||
				hx_os_cfg_equal(node->value, "-inf")))
			hx_os_cfg_write(
				writer, node->value.data, node->value.bytes);
		else if (node->kind == HX_OS_CONFIG_BOOL)
			hx_os_cfg_write(
				writer, node->value.data, node->value.bytes);
		else
			hx_os_cfg_write_number(writer, node);
	}
	else {
		int root = depth == 1;

		if (!root)
			hx_os_cfg_write(writer, object ? "{" : "[", 1);

		for (child = node->first; child && writer->status == HX_OS_OK;
			child = child->next) {
			if (!root && child != node->first)
				hx_os_cfg_write(writer, ", ", 2);

			if (object) {
				hx_os_cfg_write_string(writer, child->name, 2);
				hx_os_cfg_write(writer, " = ", 3);
			}

			hx_os_cfg_write_toml(writer, child, depth + 1);
			if (root)
				hx_os_cfg_write(writer, "\n", 1);
		}

		if (!root)
			hx_os_cfg_write(writer, object ? "}" : "]", 1);
	}
}

static void hx_os_cfg_write_env(
	hx_os_cfg_writer *writer, const hx_os_config_node *root)
{
	const hx_os_config_node *child;

	if (root->kind != HX_OS_CONFIG_OBJECT) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	if (!hx_os_cfg_write_node(writer, 1))
		return;

	for (child = root->first; child && writer->status == HX_OS_OK;
		child = child->next) {
		size_t i;

		if (!hx_os_cfg_write_node(writer, 2))
			return;

		if (child->kind != HX_OS_CONFIG_STRING || !child->name.bytes) {
			writer->status = HX_OS_UNREPRESENTABLE;

			return;
		}

		for (i = 0; i < child->name.bytes; ++i) {
			unsigned char c = (unsigned char)child->name.data[i];

			if (!(c == '_' || (c >= 'A' && c <= 'Z') ||
				    (c >= 'a' && c <= 'z') ||
				    (i && c >= '0' && c <= '9'))) {
				writer->status = HX_OS_UNREPRESENTABLE;

				return;
			}
		}

		hx_os_cfg_write(writer, child->name.data, child->name.bytes);
		hx_os_cfg_write(writer, "=", 1);
		hx_os_cfg_write_string(writer, child->value, 2);
		hx_os_cfg_write(writer, "\n", 1);
	}
}

static void hx_os_cfg_write_xml_text(
	hx_os_cfg_writer *writer, hx_os_config_slice text, int attribute)
{
	size_t i;

	for (i = 0; i < text.bytes && writer->status == HX_OS_OK; ++i) {
		char c = text.data[i];
		const char *escaped = c == '&'	 ? "&amp;"
			: c == '<'		 ? "&lt;"
			: c == '>'		 ? "&gt;"
			: c == '"' && attribute	 ? "&quot;"
			: c == '\r'		 ? "&#13;"
			: c == '\n' && attribute ? "&#10;"
			: c == '\t' && attribute ? "&#9;"
						 : NULL;

		if (escaped)
			hx_os_cfg_write(writer, escaped, strlen(escaped));
		else
			hx_os_cfg_write(writer, text.data + i, 1);
	}
}

static void hx_os_cfg_write_xml(
	hx_os_cfg_writer *writer, const hx_os_config_node *node, size_t depth)
{
	const hx_os_config_node *child;

	if (!hx_os_cfg_write_node(writer, depth))
		return;

	if (node->kind == HX_OS_CONFIG_TEXT) {
		hx_os_cfg_write_xml_text(writer, node->value, 0);

		return;
	}

	if (node->kind != HX_OS_CONFIG_ELEMENT) {
		writer->status = HX_OS_UNREPRESENTABLE;

		return;
	}

	hx_os_cfg_write(writer, "<", 1);
	hx_os_cfg_write(writer, node->name.data, node->name.bytes);
	if (node->attributes) {
		if (!hx_os_cfg_write_node(writer, depth))
			return;

		for (child = node->attributes->first;
			child && writer->status == HX_OS_OK;
			child = child->next) {
			if (!hx_os_cfg_write_node(writer, depth + 1))
				return;

			hx_os_cfg_write(writer, " ", 1);
			hx_os_cfg_write(
				writer, child->name.data, child->name.bytes);
			hx_os_cfg_write(writer, "=\"", 2);
			hx_os_cfg_write_xml_text(writer, child->value, 1);
			hx_os_cfg_write(writer, "\"", 1);
		}
	}

	if (!node->first) {
		hx_os_cfg_write(writer, "/>", 2);

		return;
	}

	hx_os_cfg_write(writer, ">", 1);

	for (child = node->first; child && writer->status == HX_OS_OK;
		child = child->next)
		hx_os_cfg_write_xml(writer, child, depth + 1);
	hx_os_cfg_write(writer, "</", 2);
	hx_os_cfg_write(writer, node->name.data, node->name.bytes);
	hx_os_cfg_write(writer, ">", 1);
}

static void hx_os_cfg_write_document(hx_os_cfg_writer *writer,
	const hx_os_config_document *document, int format)
{
	if (format == HX_OS_CONFIG_JSON)
		hx_os_cfg_write_json(writer, document->root, 1);
	else if (format == HX_OS_CONFIG_XML)
		hx_os_cfg_write_xml(writer, document->root, 1);
	else if (format == HX_OS_CONFIG_YAML)
		hx_os_cfg_write_yaml(writer, document->root, 1);
	else if (format == HX_OS_CONFIG_TOML)
		if (document->root->kind != HX_OS_CONFIG_OBJECT)
			writer->status = HX_OS_UNREPRESENTABLE;
		else
			hx_os_cfg_write_toml(writer, document->root, 1);
	else if (format == HX_OS_CONFIG_ENV)
		hx_os_cfg_write_env(writer, document->root);
	else
		hx_os_cfg_write_lines(writer, document->root,
			format == HX_OS_CONFIG_PROPERTIES);
}
#endif

hx_os_status hx_os_config_serialize(const hx_os_config_document *document,
	int format, hx_string *out, const hx_os_config_options *input,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE
	hx_os_cfg_writer writer;
	hx_os_cfg_parser parser;
	hx_string text;
	hx_os_status status;
	size_t capacity, cost, expected;

	if (!document || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (format < HX_OS_CONFIG_JSON || format > HX_OS_CONFIG_PROPERTIES)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	memset(&writer, 0, sizeof(writer));
	memset(&parser, 0, sizeof(parser));
	if (!hx_os_cfg_options(&parser, input, &writer.options))
		return hx_os_i_error(error, parser.status, 0, 0);

	if (writer.options.backend != HX_OS_CONFIG_BUILTIN)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	hx_os_cfg_write_document(&writer, document, format);
	if (writer.status != HX_OS_OK)
		return hx_os_i_error(error, writer.status, 0, 0);

	expected = writer.position;
	if (expected == SIZE_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	capacity = expected + 1 > 48 ? expected + 1 : 48;
	cost = expected ? hx_os_i_allocation_cost(capacity, 0) : 0;
	if ((expected && !cost) || cost > writer.options.max_memory_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	hx_string_init(&text, out->context);

	status =
		expected ? hx_string_reserve(&text, expected, error) : HX_OS_OK;
	if (status == HX_OS_OK) {
		writer.buffer = text.data;
		writer.position = 0;
		writer.nodes = 0;
		hx_os_cfg_write_document(&writer, document, format);

		status = writer.status;
		if (status == HX_OS_OK && writer.position != expected)
			status = HX_OS_IO_ERROR;

		if (status == HX_OS_OK) {
			text.size = writer.position;
			if (text.data)
				text.data[text.size] = 0;

			hx_string_move(out, &text);
		}
	}

	hx_string_destroy(&text);
	if (status == HX_OS_NO_MEMORY)
		return status;

	return hx_os_i_error(error, status, 0, 0);
#else
	(void)document;
	(void)format;
	(void)out;
	(void)input;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_config_save(const hx_os_config_document *document,
	int format, const char *path, const hx_os_config_options *options,
	hx_os_error *error)
{
#if HX_OS_ENABLE_CONFIG && HX_OS_ENABLE_CONFIG_SERIALIZE &&                    \
	HX_OS_ENABLE_FILE_UTILS
	hx_string text;
	hx_os_status status;

	if (!document)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_string_init(&text, document->context);

	status =
		hx_os_config_serialize(document, format, &text, options, error);
	if (status == HX_OS_OK)
		status = hx_os_file_save_atomic(
			path, hx_string_c_str(&text), text.size, error);

	hx_string_destroy(&text);

	return status;
#else
	(void)document;
	(void)format;
	(void)path;
	(void)options;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
