#include "config_private.h"
#include <tomlc17.h>

typedef struct hx_os_cfg_toml_native {
	hx_os_cfg_parser *parser;
	size_t *lines;
	size_t count;
} hx_os_cfg_toml_native;

static int hx_os_cfg_toml_lines(hx_os_cfg_toml_native *state)
{
	hx_os_cfg_parser *parser = state->parser;
	size_t i, count = 1, index = 0;

	for (i = parser->position; i < parser->length; ++i)
		if (parser->text[i] == '\n')
			++count;

	if (count > SIZE_MAX / sizeof(size_t))
		return hx_os_cfg_reject(
			parser, HX_OS_LIMIT, "Too many TOML lines");

	state->lines = hx_os_cfg_alloc(parser, count * sizeof(size_t));
	if (!state->lines)
		return 0;

	state->lines[index++] = parser->position;

	for (i = parser->position; i < parser->length; ++i)
		if (parser->text[i] == '\n')
			state->lines[index++] = i + 1;

	state->count = count;

	return 1;
}

static int hx_os_cfg_toml_native_value(hx_os_cfg_toml_native *state,
	toml_datum_t value, size_t depth, hx_os_config_node **out)
{
	hx_os_cfg_parser *parser = state->parser;
	hx_os_config_node *node;
	hx_os_config_slice text = {NULL, 0};
	int kind, i, count;
	int datetime =
		value.type == TOML_DATETIME || value.type == TOML_DATETIMETZ;

	switch (value.type) {
	case TOML_STRING:
		kind = HX_OS_CONFIG_STRING;
		text.data = value.u.str.ptr;
		text.bytes = (size_t)value.u.str.len;
		break;
	case TOML_INT64:
		kind = HX_OS_CONFIG_INTEGER;
		break;
	case TOML_FP64:
		kind = HX_OS_CONFIG_REAL;
		break;
	case TOML_BOOLEAN:
		kind = HX_OS_CONFIG_BOOL;
		break;
	case TOML_ARRAY:
		kind = HX_OS_CONFIG_ARRAY;
		break;
	case TOML_TABLE:
		kind = HX_OS_CONFIG_OBJECT;
		break;
	case TOML_DATE:
	case TOML_TIME:
	case TOML_DATETIME:
	case TOML_DATETIMETZ:
		kind = HX_OS_CONFIG_DATETIME;
		break;

	default:
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"Unknown TOMLC value type");
	}

	if (value.lineno > 0 && (size_t)value.lineno <= state->count &&
		value.colno > 0) {
		size_t start = state->lines[value.lineno - 1];
		size_t column = (size_t)value.colno - 1;

		if (column <= parser->length - start)
			parser->position = start + column;
	}

	node = hx_os_cfg_node(parser, kind, depth);
	if (!node)
		return 0;

	if (kind == HX_OS_CONFIG_ARRAY || kind == HX_OS_CONFIG_OBJECT) {
		count = kind == HX_OS_CONFIG_ARRAY ? value.u.arr.size
						   : value.u.tab.size;

		for (i = 0; i < count; ++i) {
			hx_os_config_node *child;
			toml_datum_t item = kind == HX_OS_CONFIG_ARRAY
				? value.u.arr.elem[i]
				: value.u.tab.value[i];

			if (!hx_os_cfg_toml_native_value(
				    state, item, depth + 1, &child))
				return 0;
			if (kind == HX_OS_CONFIG_OBJECT)
				child->name = hx_os_cfg_copy(parser,
					value.u.tab.key[i],
					(size_t)value.u.tab.len[i]);

			if (!hx_os_cfg_append(parser, node, child,
				    HX_OS_CONFIG_DUPLICATE_ERROR))
				return 0;
		}
	}
	else {
		if (!text.data) {
			size_t start = parser->position, end = start;

			while (end < parser->length) {
				unsigned char c =
					(unsigned char)parser->text[end];

				if (hx_os_cfg_space(c) || c == '#' ||
					c == ',' || c == ']' || c == '}') {
					if (c == ' ' && end - start == 10 &&
						datetime) {
						++end;
						continue;
					}

					break;
				}

				++end;
			}

			text.data = parser->text + start;
			text.bytes = end - start;
		}

		node->value = hx_os_cfg_copy(parser, text.data, text.bytes);
		if (!node->value.data)
			return 0;
	}

	*out = node;

	return 1;
}

int hx_os_cfg_tomlc(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_parser *previous;
	hx_os_cfg_toml_native state;
	toml_result_t result;

	if (parser->length > INT_MAX)
		return hx_os_cfg_reject(parser, HX_OS_LIMIT,
			"TOMLC input exceeds its integer length limit");

	memset(&state, 0, sizeof(state));
	state.parser = parser;
	if (!hx_os_cfg_toml_lines(&state))
		return 0;

	previous = hx_os_cfg_vendor_enter(parser);

	result = toml_parse(parser->text, (int)parser->length);
	if (result.ok)
		(void)hx_os_cfg_toml_native_value(
			&state, result.toptab, 1, out);
	else
		(void)hx_os_cfg_fail(parser, HX_OS_SYNTAX, SIZE_MAX,
			"TOMLC rejected the document");

	toml_free(result);
	hx_os_cfg_vendor_leave(parser, previous);

	return parser->status == HX_OS_OK;
}
