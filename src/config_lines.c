#include "config_private.h"

static int hx_os_cfg_line_value(hx_os_cfg_parser *parser,
	hx_os_config_node *object, hx_os_config_slice name,
	hx_os_config_slice value)
{
	hx_os_config_node *node =
		hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, object->depth + 1);

	if (!node)
		return 0;

	node->name = name;
	node->value = value;

	return hx_os_cfg_append(
		parser, object, node, parser->document->options.duplicates);
}

static hx_os_config_slice hx_os_cfg_inline_comment(
	hx_os_config_slice value, char comment)
{
	size_t i;

	for (i = 0; i < value.bytes; ++i) {
		if (value.data[i] == comment &&
			((!i && comment == '#') ||
				(i &&
					hx_os_cfg_space(
						(unsigned char)
							value.data[i - 1])))) {
			value.bytes = i;
			break;
		}
	}

	return hx_os_cfg_trim(value);
}

static int hx_os_cfg_ini_line(hx_os_cfg_parser *parser, hx_os_config_node *root,
	hx_os_config_node **section, hx_os_config_slice line)
{
	size_t i;
	hx_os_config_slice key, value;

	if (!line.bytes || line.data[0] == ';' || line.data[0] == '#')
		return 1;

	if (line.data[0] == '[') {
		hx_os_config_node *node;

		for (i = 1; i < line.bytes && line.data[i] != ']'; ++i) {
		}

		if (i == line.bytes)
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Unterminated INI section");

		key.data = line.data + 1;
		key.bytes = i - 1;
		key = hx_os_cfg_trim(key);
		if (!key.bytes)
			return hx_os_cfg_reject(
				parser, HX_OS_SYNTAX, "Empty INI section");

		value.data = line.data + i + 1;
		value.bytes = line.bytes - i - 1;
		value = hx_os_cfg_trim(value);
		if (value.bytes && value.data[0] != ';' && value.data[0] != '#')
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Trailing content after INI section");

		node = hx_os_cfg_lookup(parser, root, key);
		if (node && node->kind != HX_OS_CONFIG_OBJECT)
			return hx_os_cfg_reject(parser, HX_OS_TYPE_MISMATCH,
				"INI section conflicts with a scalar key");

		if (!node) {
			node = hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT, 2);
			if (!node)
				return 0;

			node->name =
				hx_os_cfg_copy(parser, key.data, key.bytes);
			if (!hx_os_cfg_append(parser, root, node,
				    HX_OS_CONFIG_DUPLICATE_ERROR))
				return 0;
		}

		*section = node;

		return 1;
	}

	for (i = 0;
		i < line.bytes && line.data[i] != '=' && line.data[i] != ':';
		++i) {
	}

	if (i == line.bytes)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"INI key/value separator expected");

	key.data = line.data;
	key.bytes = i;
	key = hx_os_cfg_trim(key);
	if (!key.bytes)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX, "Empty INI key");

	value.data = line.data + i + 1;
	value.bytes = line.bytes - i - 1;
	value = hx_os_cfg_inline_comment(value, ';');
	key = hx_os_cfg_copy(parser, key.data, key.bytes);
	value = hx_os_cfg_copy(parser, value.data, value.bytes);

	return hx_os_cfg_line_value(parser, *section, key, value);
}

static int hx_os_cfg_env_name(hx_os_config_slice key)
{
	size_t i;

	if (!key.bytes)
		return 0;

	for (i = 0; i < key.bytes; ++i) {
		unsigned char c = (unsigned char)key.data[i];

		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			    c == '_' || (i && c >= '0' && c <= '9')))
			return 0;
	}

	return 1;
}

static int hx_os_cfg_env_line(hx_os_cfg_parser *parser, hx_os_config_node *root,
	hx_os_config_slice line)
{
	hx_os_config_slice key, value;
	size_t i;

	if (!line.bytes || line.data[0] == '#')
		return 1;

	if (line.bytes > 6 && !memcmp(line.data, "export", 6) &&
		(line.data[6] == ' ' || line.data[6] == '\t')) {
		line.data += 7;
		line.bytes -= 7;
		line = hx_os_cfg_trim(line);
	}

	for (i = 0; i < line.bytes && line.data[i] != '='; ++i) {
	}

	key.data = line.data;
	key.bytes = i;
	key = hx_os_cfg_trim(key);
	if (i == line.bytes || !hx_os_cfg_env_name(key))
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"ENV assignment requires an identifier and equals "
			"sign");

	key = hx_os_cfg_copy(parser, key.data, key.bytes);
	value.data = line.data + i + 1;
	value.bytes = line.bytes - i - 1;
	value = hx_os_cfg_trim(value);
	if (value.bytes && (value.data[0] == '"' || value.data[0] == '\'')) {
		size_t length = parser->length;
		size_t end = (size_t)(value.data - parser->text) + value.bytes;

		parser->position = (size_t)(value.data - parser->text);
		parser->length = end;
		if (!hx_os_cfg_quoted(parser, HX_OS_CFG_ENV_STRING, &value)) {
			parser->length = length;

			return 0;
		}

		hx_os_cfg_skip(parser, 0, 0);
		if (parser->position < end &&
			parser->text[parser->position] != '#') {
			parser->length = length;

			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Trailing content after quoted ENV value");
		}

		parser->length = length;
	}
	else {
		value = hx_os_cfg_inline_comment(value, '#');
		value = hx_os_cfg_copy(parser, value.data, value.bytes);
	}

	return hx_os_cfg_line_value(parser, root, key, value);
}

static int hx_os_cfg_property_line(hx_os_cfg_parser *parser,
	hx_os_config_node *root, const char *text, size_t bytes)
{
	size_t i = 0, start, key_end;
	hx_os_config_slice key, value;

	while (i < bytes &&
		(text[i] == ' ' || text[i] == '\t' || text[i] == '\f'))
		++i;
	if (i == bytes || text[i] == '#' || text[i] == '!')
		return 1;

	start = i;

	while (i < bytes) {
		char c = text[i];

		if (c == '=' || c == ':' || c == ' ' || c == '\t' || c == '\f')
			break;

		if (c == '\\' && i + 1 < bytes)
			++i;

		++i;
	}

	key_end = i;

	while (i < bytes &&
		(text[i] == ' ' || text[i] == '\t' || text[i] == '\f'))
		++i;
	if (i < bytes && (text[i] == '=' || text[i] == ':'))
		++i;

	while (i < bytes &&
		(text[i] == ' ' || text[i] == '\t' || text[i] == '\f'))
		++i;
	if (!hx_os_cfg_unescape(parser, text + start, key_end - start,
		    HX_OS_CFG_PROPERTY_STRING, 0, &key) ||
		!hx_os_cfg_unescape(parser, text + i, bytes - i,
			HX_OS_CFG_PROPERTY_STRING, 0, &value))
		return 0;
	return hx_os_cfg_line_value(parser, root, key, value);
}

static int hx_os_cfg_properties(
	hx_os_cfg_parser *parser, hx_os_config_node *root)
{
	size_t position = parser->position;

	char *logical = hx_os_cfg_alloc(parser, parser->length + 1);
	if (!logical)
		return 0;

	while (position < parser->length) {
		size_t written = 0;
		int continued = 0;
		int comment = 0;

		parser->position = position;
		do {
			size_t start = position, end, slashes = 0;

			while (position < parser->length &&
				parser->text[position] != '\r' &&
				parser->text[position] != '\n')
				++position;
			end = position;
			if (!written && !continued) {
				size_t first = start;

				while (first < end &&
					(parser->text[first] == ' ' ||
						parser->text[first] == '\t' ||
						parser->text[first] == '\f'))
					++first;
				comment = first < end &&
					(parser->text[first] == '#' ||
						parser->text[first] == '!');
			}

			if (position < parser->length &&
				parser->text[position++] == '\r' &&
				position < parser->length &&
				parser->text[position] == '\n')
				++position;
			if (continued)
				while (start < end &&
					(parser->text[start] == ' ' ||
						parser->text[start] == '\t' ||
						parser->text[start] == '\f'))
					++start;

			while (end > start + slashes &&
				parser->text[end - slashes - 1] == '\\')
				++slashes;
			continued = !comment && (slashes & 1) != 0;
			if (continued)
				--end;

			memcpy(logical + written, parser->text + start,
				end - start);
			written += end - start;
		} while (continued && position < parser->length);
		logical[written] = 0;
		if (!hx_os_cfg_property_line(parser, root, logical, written))
			return 0;
	}

	parser->position = parser->length;

	return 1;
}

int hx_os_cfg_lines(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_config_node *root =
		hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT, 1);
	hx_os_config_node *section = root;

	if (!root)
		return 0;

	*out = root;
	if (parser->format == HX_OS_CONFIG_PROPERTIES)
		return hx_os_cfg_properties(parser, root);

	while (parser->position < parser->length) {
		hx_os_config_slice line;
		size_t start = parser->position, next;

		while (parser->position < parser->length &&
			parser->text[parser->position] != '\r' &&
			parser->text[parser->position] != '\n')
			++parser->position;
		line.data = parser->text + start;
		line.bytes = parser->position - start;
		if (parser->position < parser->length &&
			parser->text[parser->position++] == '\r' &&
			parser->position < parser->length &&
			parser->text[parser->position] == '\n')
			++parser->position;
		next = parser->position;
		parser->position = start;
		line = hx_os_cfg_trim(line);
		if (parser->format == HX_OS_CONFIG_INI) {
			if (!hx_os_cfg_ini_line(parser, root, &section, line))
				return 0;
		}
		else if (!hx_os_cfg_env_line(parser, root, line))
			return 0;

		parser->position = next;
	}

	return 1;
}
