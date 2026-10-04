#include "config_private.h"

typedef struct hx_os_cfg_yaml_line {
	size_t offset;
	size_t bytes;
	size_t indent;
} hx_os_cfg_yaml_line;

typedef struct hx_os_cfg_yaml_state {
	hx_os_cfg_parser *parser;
	hx_os_cfg_yaml_line *lines;
	size_t count;
	size_t current;
} hx_os_cfg_yaml_state;

static hx_os_config_slice hx_os_cfg_yaml_content(const char *text, size_t bytes)
{
	hx_os_config_slice result = {text, bytes};
	char quote = 0;
	int token_start = 1;
	size_t i;

	for (i = 0; i < bytes; ++i) {
		char c = text[i];

		if (quote) {
			if (c == '\\' && quote == '"' && i + 1 < bytes)
				++i;
			else if (c == '\'' && quote == '\'' && i + 1 < bytes &&
				text[i + 1] == '\'')
				++i;
			else if (c == quote)
				quote = 0;
		}
		else if (token_start && (c == '\'' || c == '"')) {
			quote = c;
			token_start = 0;
		}
		else if (c == '#' &&
			(!i || hx_os_cfg_space((unsigned char)text[i - 1]))) {
			result.bytes = i;
			break;
		}
		else if ((c == ':' || (c == '-' && token_start)) &&
			(i + 1 == bytes ||
				hx_os_cfg_space((unsigned char)text[i + 1])))
			token_start = 1;
		else if (!hx_os_cfg_space((unsigned char)c))
			token_start = 0;
	}

	return hx_os_cfg_trim(result);
}

static size_t hx_os_cfg_yaml_colon(hx_os_config_slice value)
{
	char quote = 0;
	size_t i;

	for (i = 0; i < value.bytes; ++i) {
		char c = value.data[i];

		if (quote) {
			if (c == '\\' && quote == '"' && i + 1 < value.bytes)
				++i;
			else if (c == '\'' && quote == '\'' &&
				i + 1 < value.bytes &&
				value.data[i + 1] == '\'')
				++i;
			else if (c == quote)
				quote = 0;
		}
		else if (!i && (c == '\'' || c == '"'))
			quote = c;
		else if (c == ':' &&
			(i + 1 == value.bytes ||
				hx_os_cfg_space(
					(unsigned char)value.data[i + 1])))
			return i;
	}

	return SIZE_MAX;
}

static int hx_os_cfg_yaml_sequence(hx_os_config_slice value)
{
	return value.bytes && value.data[0] == '-' &&
		(value.bytes == 1 || value.data[1] == ' ' ||
			value.data[1] == '\t');
}

static int hx_os_cfg_yaml_text(hx_os_cfg_parser *parser,
	hx_os_config_slice value, int key, size_t depth,
	hx_os_config_node **out)
{
	hx_os_config_node *node;

	if (value.bytes && (value.data[0] == '\'' || value.data[0] == '"')) {
		size_t length = parser->length;
		size_t end = (size_t)(value.data - parser->text) + value.bytes;
		hx_os_config_slice decoded;

		parser->position = (size_t)(value.data - parser->text);
		parser->length = end;
		if (!hx_os_cfg_quoted(
			    parser, HX_OS_CFG_YAML_STRING, &decoded)) {
			parser->length = length;

			return 0;
		}

		hx_os_cfg_skip(parser, 0, 0);
		parser->length = length;
		if (parser->position != end)
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Trailing content after YAML quoted scalar");

		node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, depth);
		if (!node)
			return 0;

		node->value = decoded;
		*out = node;

		return 1;
	}

	if (!key &&
		(hx_os_cfg_equal(value, "{}") ||
			hx_os_cfg_equal(value, "[]"))) {
		node = hx_os_cfg_node(parser,
			value.data[0] == '{' ? HX_OS_CONFIG_OBJECT
					     : HX_OS_CONFIG_ARRAY,
			depth);
		*out = node;

		return node != NULL;
	}

	if (value.bytes &&
		(strchr("&*!|>{}[]%@\x60", value.data[0]) ||
			(strchr("?:-", value.data[0]) &&
				(value.bytes == 1 ||
					hx_os_cfg_space((
						unsigned char)value.data[1])))))
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"YAML feature is outside the builtin configuration "
			"profile");
	if (hx_os_cfg_yaml_colon(value) != SIZE_MAX)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"Colon followed by whitespace inside plain YAML "
			"scalar");

	if (!key)
		return hx_os_cfg_scalar(
			parser, value, HX_OS_CFG_YAML_STRING, depth, out);

	if (!value.bytes || value.data[0] == '?' ||
		hx_os_cfg_yaml_colon(value) != SIZE_MAX)
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"Only nonempty scalar YAML keys are supported");
	node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, depth);
	if (!node)
		return 0;

	node->value = hx_os_cfg_copy(parser, value.data, value.bytes);
	*out = node;

	return node->value.data != NULL;
}

static hx_os_config_slice hx_os_cfg_yaml_line_text(
	hx_os_cfg_yaml_state *state, size_t index)
{
	hx_os_cfg_yaml_line *line = &state->lines[index];
	hx_os_config_slice value = {
		state->parser->text + line->offset, line->bytes};

	return value;
}

static int hx_os_cfg_yaml_block(hx_os_cfg_yaml_state *state, size_t indent,
	size_t depth, hx_os_config_node **out);

static int hx_os_cfg_yaml_pair(hx_os_cfg_yaml_state *state,
	hx_os_config_node *object, size_t indent, hx_os_config_slice text)
{
	hx_os_cfg_parser *parser = state->parser;

	size_t colon = hx_os_cfg_yaml_colon(text);

	hx_os_config_slice key_text, value;
	hx_os_config_node *key, *child;

	if (colon == SIZE_MAX)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "YAML mapping entry expected");

	key_text.data = text.data;
	key_text.bytes = colon;
	key_text = hx_os_cfg_trim(key_text);
	if (!hx_os_cfg_yaml_text(parser, key_text, 1, object->depth + 1, &key))
		return 0;

	value.data = text.data + colon + 1;
	value.bytes = text.bytes - colon - 1;
	value = hx_os_cfg_trim(value);
	if (!value.bytes && state->current < state->count &&
		state->lines[state->current].indent > indent) {
		if (!hx_os_cfg_yaml_block(state,
			    state->lines[state->current].indent,
			    object->depth + 1, &child))
			return 0;
	}
	else if (!hx_os_cfg_yaml_text(
			 parser, value, 0, object->depth + 1, &child))
		return 0;
	child->name = key->value;

	return hx_os_cfg_append(
		parser, object, child, parser->document->options.duplicates);
}

static int hx_os_cfg_yaml_map_rest(
	hx_os_cfg_yaml_state *state, hx_os_config_node *object, size_t indent)
{
	while (state->current < state->count &&
		state->lines[state->current].indent == indent) {
		hx_os_config_slice text =
			hx_os_cfg_yaml_line_text(state, state->current);

		state->parser->position = state->lines[state->current].offset;
		++state->current;
		if (hx_os_cfg_yaml_sequence(text))
			return hx_os_cfg_reject(state->parser, HX_OS_SYNTAX,
				"YAML mapping and sequence indentation "
				"conflicts");

		if (!hx_os_cfg_yaml_pair(state, object, indent, text))
			return 0;
	}

	return 1;
}

static int hx_os_cfg_yaml_block(hx_os_cfg_yaml_state *state, size_t indent,
	size_t depth, hx_os_config_node **out)
{
	hx_os_cfg_parser *parser = state->parser;
	hx_os_config_slice first =
		hx_os_cfg_yaml_line_text(state, state->current);
	hx_os_config_node *node;

	int sequence = hx_os_cfg_yaml_sequence(first);
	parser->position = state->lines[state->current].offset;
	if (!sequence && hx_os_cfg_yaml_colon(first) == SIZE_MAX) {
		++state->current;

		return hx_os_cfg_yaml_text(parser, first, 0, depth, out);
	}

	node = hx_os_cfg_node(parser,
		sequence ? HX_OS_CONFIG_ARRAY : HX_OS_CONFIG_OBJECT, depth);
	if (!node)
		return 0;

	if (!sequence) {
		if (!hx_os_cfg_yaml_map_rest(state, node, indent))
			return 0;
	}
	else {
		while (state->current < state->count &&
			state->lines[state->current].indent == indent) {
			hx_os_config_slice text =
				hx_os_cfg_yaml_line_text(state, state->current);
			hx_os_config_node *child;
			size_t offset = state->lines[state->current].offset;
			size_t item_indent;

			parser->position = offset;
			if (!hx_os_cfg_yaml_sequence(text))
				break;

			++state->current;
			++text.data;
			--text.bytes;
			text = hx_os_cfg_trim(text);
			item_indent = indent +
				(size_t)(text.data - (parser->text + offset));
			if (!text.bytes && state->current < state->count &&
				state->lines[state->current].indent > indent) {
				if (!hx_os_cfg_yaml_block(state,
					    state->lines[state->current].indent,
					    depth + 1, &child))
					return 0;
			}
			else if (hx_os_cfg_yaml_colon(text) != SIZE_MAX) {
				child = hx_os_cfg_node(
					parser, HX_OS_CONFIG_OBJECT, depth + 1);
				if (!child ||
					!hx_os_cfg_yaml_pair(state, child,
						item_indent, text) ||
					!hx_os_cfg_yaml_map_rest(
						state, child, item_indent))
					return 0;
			}
			else if (hx_os_cfg_yaml_sequence(text))
				return hx_os_cfg_reject(parser,
					HX_OS_NOT_SUPPORTED,
					"Compact nested YAML sequences require "
					"libyaml");
			else if (!hx_os_cfg_yaml_text(
					 parser, text, 0, depth + 1, &child))
				return 0;
			if (!hx_os_cfg_append(parser, node, child,
				    parser->document->options.duplicates))
				return 0;
		}
	}

	if (state->current < state->count &&
		state->lines[state->current].indent > indent)
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"Multiline YAML scalars or inconsistent indentation");
	*out = node;

	return 1;
}

static int hx_os_cfg_yaml_scan(
	hx_os_cfg_yaml_state *state, size_t *count, int store)
{
	hx_os_cfg_parser *parser = state->parser;
	size_t position = parser->position;
	int started = 0, ended = 0, marker = 0;

	*count = 0;

	while (position < parser->length) {
		size_t start = position, end, indent = 0;
		hx_os_config_slice line;

		while (position < parser->length &&
			parser->text[position] != '\n' &&
			parser->text[position] != '\r')
			++position;
		end = position;
		if (position < parser->length &&
			parser->text[position++] == '\r' &&
			position < parser->length &&
			parser->text[position] == '\n')
			++position;
		while (start + indent < end &&
			parser->text[start + indent] == ' ')
			++indent;
		if (start + indent < end &&
			parser->text[start + indent] == '\t') {
			parser->position = start + indent;

			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Tabs cannot indent YAML");
		}

		line = hx_os_cfg_yaml_content(
			parser->text + start + indent, end - start - indent);
		if (!line.bytes)
			continue;

		if (!indent && hx_os_cfg_equal(line, "---")) {
			if (started || marker || ended)
				goto multiple;

			marker = 1;
			continue;
		}

		if (!indent && hx_os_cfg_equal(line, "...")) {
			if (ended)
				goto multiple;

			ended = 1;
			continue;
		}

		if (ended)
			goto multiple;

		started = 1;
		if (store) {
			state->lines[*count].offset =
				(size_t)(line.data - parser->text);
			state->lines[*count].bytes = line.bytes;
			state->lines[*count].indent = indent;
		}

		++*count;
		continue;

	multiple:
		parser->position = start;
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"Only one YAML document per configuration is "
			"supported");
	}

	return 1;
}

int hx_os_cfg_yaml(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_yaml_state state;
	size_t count = 0;

	memset(&state, 0, sizeof(state));
	state.parser = parser;
	if (!hx_os_cfg_yaml_scan(&state, &count, 0))
		return 0;

	if (!count) {
		hx_os_config_slice empty = {"", 0};

		return hx_os_cfg_scalar(
			parser, empty, HX_OS_CFG_YAML_STRING, 1, out);
	}

	if (count > SIZE_MAX / sizeof(*state.lines))
		return hx_os_cfg_reject(
			parser, HX_OS_LIMIT, "Too many YAML lines");

	state.lines = hx_os_cfg_alloc(parser, count * sizeof(*state.lines));
	if (!state.lines || !hx_os_cfg_yaml_scan(&state, &count, 1))
		return 0;

	state.count = count;
	if (!hx_os_cfg_yaml_block(&state, state.lines[0].indent, 1, out))
		return 0;

	if (state.current != count) {
		parser->position = state.lines[state.current].offset;

		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"YAML content is outside the builtin configuration "
			"profile");
	}

	parser->position = parser->length;

	return 1;
}
