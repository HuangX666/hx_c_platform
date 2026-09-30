#include "config_private.h"
#include <ini.h>

typedef struct hx_os_cfg_ini_state {
	hx_os_cfg_parser *parser;
	hx_os_config_node *root;
	size_t *offsets;
	size_t lines;
} hx_os_cfg_ini_state;

static int hx_os_cfg_ini_handler(void *user, const char *section,
	const char *name, const char *value, int line)
{
	hx_os_cfg_ini_state *state = user;
	hx_os_cfg_parser *parser = state->parser;
	hx_os_config_node *parent = state->root, *node;
	hx_os_config_slice key = {section, strlen(section)};
	if (line > 0 && (size_t)line <= state->lines)
		parser->position = state->offsets[line - 1];
	key = hx_os_cfg_trim(key);
	if (key.bytes) {
		node = hx_os_cfg_lookup(parser, parent, key);
		if (node && node->kind != HX_OS_CONFIG_OBJECT)
			return hx_os_cfg_reject(parser, HX_OS_TYPE_MISMATCH,
				"INI section conflicts with a scalar key");
		if (!node) {
			node = hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT, 2);
			if (!node)
				return 0;
			node->name =
				hx_os_cfg_copy(parser, key.data, key.bytes);
			if (!hx_os_cfg_append(parser, parent, node,
				    HX_OS_CONFIG_DUPLICATE_ERROR))
				return 0;
		}
		parent = node;
	}
	if (!name)
		return key.bytes ? 1
				 : hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					   "Empty INI section");
	if (!name[0])
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX, "Empty INI key");
	node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, parent->depth + 1);
	if (!node)
		return 0;
	node->name = hx_os_cfg_copy(parser, name, strlen(name));
	node->value = hx_os_cfg_copy(parser, value, strlen(value));
	return hx_os_cfg_append(
		parser, parent, node, parser->document->options.duplicates);
}

static int hx_os_cfg_ini_preflight(
	hx_os_cfg_ini_state *state, hx_os_config_slice *normalized)
{
	hx_os_cfg_parser *parser = state->parser;
	size_t i = parser->position, lines = 1, written = 0, index = 0;
	char *buffer;
	for (; i < parser->length; ++i) {
		if (parser->text[i] == '\n' || parser->text[i] == '\r') {
			if (parser->text[i] == '\r' && i + 1 < parser->length &&
				parser->text[i + 1] == '\n')
				++i;
			++lines;
		}
	}
	if (lines > SIZE_MAX / sizeof(size_t))
		return hx_os_cfg_reject(
			parser, HX_OS_LIMIT, "Too many INI lines");
	state->offsets = hx_os_cfg_alloc(parser, lines * sizeof(size_t));
	buffer = hx_os_cfg_alloc(parser, parser->length + 2);
	if (!state->offsets || !buffer)
		return 0;
	i = parser->position;
	while (i < parser->length) {
		size_t start = i, end;
		hx_os_config_slice line;
		state->offsets[index++] = start;
		while (i < parser->length && parser->text[i] != '\r' &&
			parser->text[i] != '\n')
			++i;
		end = i;
		if (i < parser->length && parser->text[i++] == '\r' &&
			i < parser->length && parser->text[i] == '\n')
			++i;
		parser->position = start;
		if (end - start > 1024 * 1024)
			return hx_os_cfg_reject(parser, HX_OS_LIMIT,
				"inih line exceeds its compiled limit");
		line.data = parser->text + start;
		line.bytes = end - start;
		line = hx_os_cfg_trim(line);
		if (line.bytes && line.data[0] == '[') {
			size_t closing = 1;
			while (closing < line.bytes &&
				line.data[closing] != ']')
				++closing;
			if (closing == line.bytes)
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Unterminated INI section");
			if (closing - 1 > 49)
				return hx_os_cfg_reject(parser, HX_OS_LIMIT,
					"inih section names are limited to 49 "
					"UTF-8 bytes");
			++closing;
			while (closing < line.bytes &&
				hx_os_cfg_space(
					(unsigned char)line.data[closing]))
				++closing;
			if (closing < line.bytes && line.data[closing] != ';' &&
				line.data[closing] != '#')
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Trailing content after INI section");
		}
		memcpy(buffer + written, parser->text + start, end - start);
		written += end - start;
		buffer[written++] = '\n';
	}
	buffer[written] = 0;
	state->lines = index;
	normalized->data = buffer;
	normalized->bytes = written;
	return 1;
}

int hx_os_cfg_inih(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_ini_state state;
	hx_os_config_slice input;
	hx_os_cfg_parser *previous;
	int status;
	memset(&state, 0, sizeof(state));
	state.parser = parser;
	state.root = hx_os_cfg_node(parser, HX_OS_CONFIG_OBJECT, 1);
	if (!state.root || !hx_os_cfg_ini_preflight(&state, &input))
		return 0;
	previous = hx_os_cfg_vendor_enter(parser);
	status = ini_parse_string_length(
		input.data, input.bytes, hx_os_cfg_ini_handler, &state);
	if (status) {
		if (status > 0 && (size_t)status <= state.lines)
			parser->position = state.offsets[status - 1];
		(void)hx_os_cfg_reject(parser,
			status == -2 ? HX_OS_NO_MEMORY : HX_OS_SYNTAX,
			"inih rejected the document");
		if (parser->error && parser->status == HX_OS_SYNTAX) {
			parser->error->detail.domain = HX_OS_ERROR_CONFIG;
			parser->error->detail.native_code = status;
			parser->error->detail.cause_code = HX_OS_CONFIG_INIH;
		}
	}
	hx_os_cfg_vendor_leave(parser, previous);
	*out = state.root;
	return parser->status == HX_OS_OK;
}
