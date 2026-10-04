#include "config_private.h"

static int hx_os_cfg_json_value(
	hx_os_cfg_parser *parser, size_t depth, hx_os_config_node **out)
{
	hx_os_config_node *node;
	char c;

	hx_os_cfg_skip(parser, 1, 0);
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "JSON value expected");

	c = parser->text[parser->position];
	if (c == '{' || c == '[') {
		int object = c == '{';
		char closing = object ? '}' : ']';

		node = hx_os_cfg_node(parser,
			object ? HX_OS_CONFIG_OBJECT : HX_OS_CONFIG_ARRAY,
			depth);
		if (!node)
			return 0;

		++parser->position;
		hx_os_cfg_skip(parser, 1, 0);
		if (parser->position < parser->length &&
			parser->text[parser->position] == closing) {
			++parser->position;
			*out = node;

			return 1;
		}

		for (;;) {
			hx_os_config_slice name = {NULL, 0};
			hx_os_config_node *child;

			if (object) {
				if (!hx_os_cfg_quoted(parser,
					    HX_OS_CFG_JSON_STRING, &name))
					return 0;
				hx_os_cfg_skip(parser, 1, 0);
				if (parser->position == parser->length ||
					parser->text[parser->position++] != ':')
					return hx_os_cfg_reject(parser,
						HX_OS_SYNTAX,
						"JSON colon expected");
			}

			if (!hx_os_cfg_json_value(parser, depth + 1, &child))
				return 0;

			child->name = name;
			if (!hx_os_cfg_append(parser, node, child,
				    parser->document->options.duplicates))
				return 0;
			hx_os_cfg_skip(parser, 1, 0);
			if (parser->position == parser->length)
				break;

			c = parser->text[parser->position++];
			if (c == closing) {
				*out = node;

				return 1;
			}

			if (c != ',')
				break;

			hx_os_cfg_skip(parser, 1, 0);
		}

		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"JSON container separator or terminator expected");
	}

	if (c == '"') {
		node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, depth);
		if (!node ||
			!hx_os_cfg_quoted(
				parser, HX_OS_CFG_JSON_STRING, &node->value))
			return 0;
		*out = node;

		return 1;
	}
	else {
		hx_os_config_slice value;
		size_t start = parser->position;

		while (parser->position < parser->length) {
			c = parser->text[parser->position];
			if (hx_os_cfg_space((unsigned char)c) || c == ',' ||
				c == '}' || c == ']')
				break;
			++parser->position;
		}

		value.data = parser->text + start;
		value.bytes = parser->position - start;

		return hx_os_cfg_scalar(
			parser, value, HX_OS_CFG_JSON_STRING, depth, out);
	}
}

int hx_os_cfg_json(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	if (!hx_os_cfg_json_value(parser, 1, out))
		return 0;

	hx_os_cfg_skip(parser, 1, 0);
	if (parser->position != parser->length)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"Trailing content after JSON value");

	return 1;
}
