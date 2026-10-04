#include "config_private.h"
#include <yaml.h>

typedef struct hx_os_cfg_yaml_native {
	hx_os_cfg_parser *parser;
	yaml_parser_t native;
	yaml_event_t event;
	size_t base;
	size_t offset;
	size_t index;
	int event_live;
} hx_os_cfg_yaml_native;

static void hx_os_cfg_yaml_mark(hx_os_cfg_yaml_native *state, size_t index)
{
	if (index < state->index) {
		state->index = 0;
		state->offset = state->base;
	}

	while (state->index < index && state->offset < state->parser->length) {
		unsigned char c =
			(unsigned char)state->parser->text[state->offset++];

		if ((c & 0xc0) != 0x80)
			++state->index;

		while (state->offset < state->parser->length &&
			((unsigned char)state->parser->text[state->offset] &
				0xc0) == 0x80)
			++state->offset;
	}

	state->parser->position = state->offset;
}

static int hx_os_cfg_yaml_event(hx_os_cfg_yaml_native *state)
{
	if (state->event_live) {
		yaml_event_delete(&state->event);
		state->event_live = 0;
	}

	if (!hx_os_cfg_work(state->parser, 1))
		return 0;

	if (!yaml_parser_parse(&state->native, &state->event)) {
		hx_os_cfg_yaml_mark(state, state->native.problem_mark.index);
		(void)hx_os_cfg_reject(state->parser, HX_OS_SYNTAX,
			"libyaml rejected the document");
		if (state->parser->error &&
			state->parser->status == HX_OS_SYNTAX) {
			state->parser->error->detail.domain =
				HX_OS_ERROR_CONFIG;
			state->parser->error->detail.native_code =
				state->native.error;
			state->parser->error->detail.cause_code =
				HX_OS_CONFIG_LIBYAML;
		}

		return 0;
	}

	state->event_live = 1;
	hx_os_cfg_yaml_mark(state, state->event.start_mark.index);

	return 1;
}

static int hx_os_cfg_yaml_native_scalar(hx_os_cfg_yaml_native *state,
	size_t depth, int key, hx_os_config_node **out)
{
	hx_os_cfg_parser *parser = state->parser;
	yaml_event_t *event = &state->event;
	hx_os_config_slice value = {(const char *)event->data.scalar.value,
		event->data.scalar.length};
	const char *tag = (const char *)event->data.scalar.tag;
	hx_os_config_node *node;
	int force_string =
		key || event->data.scalar.style != YAML_PLAIN_SCALAR_STYLE;

	if (event->data.scalar.anchor)
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"YAML anchors and aliases are disabled");

	if (tag) {
		if (!strcmp(tag, "tag:yaml.org,2002:str"))
			force_string = 1;
		else if (key ||
			(strcmp(tag, "tag:yaml.org,2002:int") &&
				strcmp(tag, "tag:yaml.org,2002:float") &&
				strcmp(tag, "tag:yaml.org,2002:bool") &&
				strcmp(tag, "tag:yaml.org,2002:null")))
			return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
				"YAML tag is outside the configuration schema");
		else
			force_string = 0;
	}

	if (force_string) {
		node = hx_os_cfg_node(parser, HX_OS_CONFIG_STRING, depth);
		if (!node)
			return 0;

		node->value = hx_os_cfg_copy(parser, value.data, value.bytes);
		if (!node->value.data)
			return 0;
	}
	else if (!hx_os_cfg_scalar(
			 parser, value, HX_OS_CFG_YAML_STRING, depth, &node))
		return 0;
	if (tag && strcmp(tag, "tag:yaml.org,2002:str")) {
		int wanted = !strcmp(tag, "tag:yaml.org,2002:int")
			? HX_OS_CONFIG_INTEGER
			: !strcmp(tag, "tag:yaml.org,2002:float")
			? HX_OS_CONFIG_REAL
			: !strcmp(tag, "tag:yaml.org,2002:bool")
			? HX_OS_CONFIG_BOOL
			: HX_OS_CONFIG_NULL;
		if (wanted == HX_OS_CONFIG_REAL &&
			node->kind == HX_OS_CONFIG_INTEGER)
			node->kind = HX_OS_CONFIG_REAL;
		if (node->kind != wanted)
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"YAML tagged scalar does not match the schema");
	}

	*out = node;

	return 1;
}

static int hx_os_cfg_yaml_native_value(
	hx_os_cfg_yaml_native *state, size_t depth, hx_os_config_node **out)
{
	hx_os_cfg_parser *parser = state->parser;
	hx_os_config_node *node;
	int mapping;
	yaml_event_type_t closing;
	const yaml_char_t *anchor, *tag;

	if (state->event.type == YAML_SCALAR_EVENT)
		return hx_os_cfg_yaml_native_scalar(state, depth, 0, out);

	if (state->event.type != YAML_MAPPING_START_EVENT &&
		state->event.type != YAML_SEQUENCE_START_EVENT)
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"YAML aliases and complex keys are disabled");
	mapping = state->event.type == YAML_MAPPING_START_EVENT;
	closing = mapping ? YAML_MAPPING_END_EVENT : YAML_SEQUENCE_END_EVENT;
	anchor = mapping ? state->event.data.mapping_start.anchor
			 : state->event.data.sequence_start.anchor;
	tag = mapping ? state->event.data.mapping_start.tag
		      : state->event.data.sequence_start.tag;
	if (anchor ||
		(tag &&
			strcmp((const char *)tag,
				mapping ? "tag:yaml.org,2002:map"
					: "tag:yaml.org,2002:seq")))
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"YAML anchors or custom container tags are disabled");
	node = hx_os_cfg_node(parser,
		mapping ? HX_OS_CONFIG_OBJECT : HX_OS_CONFIG_ARRAY, depth);
	if (!node)
		return 0;

	for (;;) {
		hx_os_config_node *child;
		hx_os_config_slice name = {NULL, 0};

		if (!hx_os_cfg_yaml_event(state))
			return 0;

		if (state->event.type == closing)
			break;

		if (mapping) {
			hx_os_config_node *key;

			if (state->event.type != YAML_SCALAR_EVENT)
				return hx_os_cfg_reject(parser,
					HX_OS_NOT_SUPPORTED,
					"Only scalar YAML mapping keys are "
					"supported");

			if (!hx_os_cfg_yaml_native_scalar(
				    state, depth + 1, 1, &key))
				return 0;
			name = key->value;
			if (!hx_os_cfg_yaml_event(state))
				return 0;
		}

		if (!hx_os_cfg_yaml_native_value(state, depth + 1, &child))
			return 0;

		child->name = name;
		if (!hx_os_cfg_append(parser, node, child,
			    parser->document->options.duplicates))
			return 0;
	}

	*out = node;

	return 1;
}

int hx_os_cfg_libyaml(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	hx_os_cfg_yaml_native state;

	hx_os_cfg_parser *previous = hx_os_cfg_vendor_enter(parser);

	int initialized;

	memset(&state, 0, sizeof(state));
	state.parser = parser;
	state.base = parser->position;
	state.offset = state.base;
	initialized = yaml_parser_initialize(&state.native);
	if (!initialized)
		goto done;

	yaml_parser_set_input_string(&state.native,
		(const unsigned char *)parser->text + state.base,
		parser->length - state.base);
	if (!hx_os_cfg_yaml_event(&state) ||
		state.event.type != YAML_STREAM_START_EVENT ||
		!hx_os_cfg_yaml_event(&state))
		goto done;
	if (state.event.type == YAML_STREAM_END_EVENT) {
		hx_os_config_slice empty = {"", 0};

		(void)hx_os_cfg_scalar(
			parser, empty, HX_OS_CFG_YAML_STRING, 1, out);
		goto done;
	}

	if (state.event.type != YAML_DOCUMENT_START_EVENT ||
		!hx_os_cfg_yaml_event(&state) ||
		!hx_os_cfg_yaml_native_value(&state, 1, out) ||
		!hx_os_cfg_yaml_event(&state) ||
		state.event.type != YAML_DOCUMENT_END_EVENT ||
		!hx_os_cfg_yaml_event(&state))
		goto done;
	if (state.event.type != YAML_STREAM_END_EVENT)
		(void)hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"Only one YAML document per configuration is "
			"supported");

done:
	if (state.event_live)
		yaml_event_delete(&state.event);

	if (initialized)
		yaml_parser_delete(&state.native);

	if (!*out && parser->status == HX_OS_OK)
		(void)hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"libyaml did not produce a document");

	hx_os_cfg_vendor_leave(parser, previous);

	return parser->status == HX_OS_OK;
}
