#include "config_private.h"

enum {
	HX_OS_CFG_TOML_IMPLICIT = 1,
	HX_OS_CFG_TOML_DECLARED = 2,
	HX_OS_CFG_TOML_DOTTED = 4,
	HX_OS_CFG_TOML_SEALED = 8
};

static int hx_os_cfg_toml_key(hx_os_cfg_parser *parser, hx_os_config_slice *key)
{
	size_t start;
	char c;
	hx_os_cfg_skip(parser, 0, 0);
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "TOML key expected");
	c = parser->text[parser->position];
	if (c == '"' || c == '\'')
		return hx_os_cfg_quoted(parser, HX_OS_CFG_TOML_STRING, key);
	start = parser->position;
	while (parser->position < parser->length) {
		c = parser->text[parser->position];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			    (c >= '0' && c <= '9') || c == '_' || c == '-'))
			break;
		++parser->position;
	}
	if (parser->position == start)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "Invalid bare TOML key");
	*key = hx_os_cfg_copy(
		parser, parser->text + start, parser->position - start);
	return key->data != NULL;
}

static int hx_os_cfg_toml_path(hx_os_cfg_parser *parser,
	hx_os_config_node **parent, hx_os_config_slice *key, int header)
{
	for (;;) {
		hx_os_config_node *child;
		if (!hx_os_cfg_toml_key(parser, key))
			return 0;
		hx_os_cfg_skip(parser, 0, 0);
		if (parser->position == parser->length ||
			parser->text[parser->position] != '.')
			return 1;
		++parser->position;
		child = hx_os_cfg_lookup(parser, *parent, *key);
		if (child &&
			(child->kind != HX_OS_CONFIG_OBJECT ||
				(child->flags & HX_OS_CFG_TOML_SEALED)))
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"TOML dotted key conflicts with an existing "
				"value");
		if (!child) {
			child = hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT,
				(*parent)->depth + 1);
			if (!child)
				return 0;
			child->name = *key;
			child->flags = header ? HX_OS_CFG_TOML_IMPLICIT
					      : HX_OS_CFG_TOML_DOTTED;
			if (!hx_os_cfg_append(parser, *parent, child,
				    HX_OS_CONFIG_DUPLICATE_ERROR))
				return 0;
		}
		*parent = child;
	}
}

static int hx_os_cfg_toml_value(
	hx_os_cfg_parser *parser, size_t depth, hx_os_config_node **out);

static int hx_os_cfg_toml_assignment(
	hx_os_cfg_parser *parser, hx_os_config_node *parent)
{
	hx_os_config_slice key;
	hx_os_config_node *value;
	if (!hx_os_cfg_toml_path(parser, &parent, &key, 0))
		return 0;
	if (parser->position == parser->length ||
		parser->text[parser->position++] != '=')
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "TOML equals sign expected");
	hx_os_cfg_skip(parser, 0, 0);
	if (!hx_os_cfg_toml_value(parser, parent->depth + 1, &value))
		return 0;
	value->name = key;
	return hx_os_cfg_append(
		parser, parent, value, HX_OS_CONFIG_DUPLICATE_ERROR);
}

static int hx_os_cfg_toml_container(hx_os_cfg_parser *parser, size_t depth,
	int array, hx_os_config_node **out)
{
	char closing = array ? ']' : '}';
	hx_os_config_node *node = hx_os_cfg_node(parser,
		array ? HX_OS_CONFIG_ARRAY : HX_OS_CONFIG_OBJECT, depth);
	if (!node)
		return 0;
	++parser->position;
	hx_os_cfg_skip(parser, array, array);
	if (parser->position < parser->length &&
		parser->text[parser->position] == closing)
		++parser->position;
	else {
		for (;;) {
			char separator;
			if (array) {
				hx_os_config_node *value;
				if (!hx_os_cfg_toml_value(
					    parser, depth + 1, &value) ||
					!hx_os_cfg_append(parser, node, value,
						HX_OS_CONFIG_DUPLICATE_ERROR))
					return 0;
			}
			else if (!hx_os_cfg_toml_assignment(parser, node))
				return 0;
			hx_os_cfg_skip(parser, array, array);
			if (parser->position == parser->length)
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Unterminated TOML container");
			separator = parser->text[parser->position++];
			if (separator == closing)
				break;
			if (separator != ',')
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"TOML comma expected");
			hx_os_cfg_skip(parser, array, array);
			if (array && parser->position < parser->length &&
				parser->text[parser->position] == closing) {
				++parser->position;
				break;
			}
		}
	}
	node->flags = HX_OS_CFG_TOML_SEALED;
	*out = node;
	return 1;
}

static int hx_os_cfg_toml_value(
	hx_os_cfg_parser *parser, size_t depth, hx_os_config_node **out)
{
	char c;
	size_t start;
	hx_os_config_slice value;
	hx_os_config_node *node;
	hx_os_cfg_skip(parser, 0, 0);
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "TOML value expected");
	c = parser->text[parser->position];
	if (c == '[' || c == '{')
		return hx_os_cfg_toml_container(parser, depth, c == '[', out);
	if (c == '"' || c == '\'') {
		if (parser->length - parser->position >= 3 &&
			parser->text[parser->position + 1] == c &&
			parser->text[parser->position + 2] == c)
			return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
				"Multiline TOML strings require the TOMLC "
				"backend");
		node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, depth);
		if (!node ||
			!hx_os_cfg_quoted(
				parser, HX_OS_CFG_TOML_STRING, &node->value))
			return 0;
		*out = node;
		return 1;
	}
	start = parser->position;
	while (parser->position < parser->length) {
		c = parser->text[parser->position];
		if (hx_os_cfg_space((unsigned char)c) || c == ',' || c == ']' ||
			c == '}' || c == '#')
			break;
		++parser->position;
	}
	value.data = parser->text + start;
	value.bytes = parser->position - start;
	if ((value.bytes >= 10 && value.data[4] == '-' &&
		    value.data[7] == '-') ||
		(value.bytes >= 5 && value.data[2] == ':'))
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"TOML date/time values require the TOMLC backend");
	return hx_os_cfg_scalar(
		parser, value, HX_OS_CFG_TOML_STRING, depth, out);
}

int hx_os_cfg_toml(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_config_node *root =
		hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT, 1);
	hx_os_config_node *table = root;
	if (!root)
		return 0;
	while (parser->position < parser->length) {
		hx_os_cfg_skip(parser, 1, 1);
		if (parser->position == parser->length)
			break;
		if (parser->text[parser->position] == '[') {
			hx_os_config_node *parent = root, *existing;
			hx_os_config_slice key;
			++parser->position;
			if (parser->position < parser->length &&
				parser->text[parser->position] == '[')
				return hx_os_cfg_reject(parser,
					HX_OS_NOT_SUPPORTED,
					"Arrays of tables require the TOMLC "
					"backend");
			if (!hx_os_cfg_toml_path(parser, &parent, &key, 1))
				return 0;
			if (parser->position == parser->length ||
				parser->text[parser->position++] != ']')
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"TOML table terminator expected");
			existing = hx_os_cfg_lookup(parser, parent, key);
			if (existing) {
				if (existing->kind != HX_OS_CONFIG_OBJECT ||
					existing->flags !=
						HX_OS_CFG_TOML_IMPLICIT)
					return hx_os_cfg_reject(parser,
						HX_OS_EXISTS,
						"TOML table is already "
						"defined");
				table = existing;
			}
			else {
				table = hx_os_cfg_node(parser,
					HX_OS_CONFIG_OBJECT, parent->depth + 1);
				if (!table)
					return 0;
				table->name = key;
				if (!hx_os_cfg_append(parser, parent, table,
					    HX_OS_CONFIG_DUPLICATE_ERROR))
					return 0;
			}
			table->flags = HX_OS_CFG_TOML_DECLARED;
		}
		else if (!hx_os_cfg_toml_assignment(parser, table))
			return 0;
		hx_os_cfg_skip(parser, 0, 1);
		if (parser->position < parser->length &&
			parser->text[parser->position] != '\n' &&
			parser->text[parser->position] != '\r')
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Trailing content after TOML assignment");
	}
	*out = root;
	return 1;
}
