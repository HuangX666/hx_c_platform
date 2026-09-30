#include "config_private.h"
#include "number_private.h"

int hx_os_cfg_space(unsigned char character)
{
	return character == ' ' || character == '\t' || character == '\r' ||
		character == '\n';
}

void hx_os_cfg_skip(hx_os_cfg_parser *parser, int newlines, int comments)
{
	while (parser->position < parser->length) {
		char c = parser->text[parser->position];
		if (c == ' ' || c == '\t' ||
			(newlines && (c == '\n' || c == '\r')))
			++parser->position;
		else if (comments && c == '#')
			while (parser->position < parser->length &&
				parser->text[parser->position] != '\n' &&
				parser->text[parser->position] != '\r')
				++parser->position;
		else
			break;
	}
}

int hx_os_cfg_equal(hx_os_config_slice value, const char *literal)
{
	size_t bytes = strlen(literal);
	return value.bytes == bytes &&
		(!bytes || !memcmp(value.data, literal, bytes));
}

hx_os_config_slice hx_os_cfg_trim(hx_os_config_slice value)
{
	while (value.bytes && hx_os_cfg_space((unsigned char)*value.data)) {
		++value.data;
		--value.bytes;
	}
	while (value.bytes &&
		hx_os_cfg_space((unsigned char)value.data[value.bytes - 1]))
		--value.bytes;
	return value;
}

static int hx_os_cfg_hex(const char *text, size_t length, size_t *position,
	int digits, uint32_t *point)
{
	int i;
	*point = 0;
	if ((size_t)digits > length - *position)
		return 0;
	for (i = 0; i < digits; ++i) {
		int digit = hx_os_i_number_digit(
			(unsigned char)text[(*position)++]);
		if (digit < 0)
			return 0;
		*point = (*point << 4) | (unsigned)digit;
	}
	return 1;
}

int hx_os_cfg_unescape(hx_os_cfg_parser *parser, const char *text, size_t bytes,
	int mode, char quote, hx_os_config_slice *out)
{
	char *buffer;
	size_t i = 0, written = 0;
	if (!hx_os_cfg_work(parser, bytes))
		return 0;
	buffer = hx_os_cfg_alloc(parser, bytes + 1);
	if (!buffer)
		return 0;
	while (i < bytes) {
		unsigned char c = (unsigned char)text[i++];
		if (mode != HX_OS_CFG_PROPERTY_STRING &&
			(c < 0x20 ||
				(c == 0x7f && mode != HX_OS_CFG_JSON_STRING)) &&
			!(c == '\t' && mode != HX_OS_CFG_JSON_STRING))
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Control character in quoted string");
		if (quote == '\'' && mode != HX_OS_CFG_PROPERTY_STRING) {
			buffer[written++] = (char)c;
			if (mode == HX_OS_CFG_YAML_STRING && c == '\'' &&
				i < bytes && text[i] == '\'')
				++i;
			continue;
		}
		if (c != '\\') {
			buffer[written++] = (char)c;
			continue;
		}
		if (i == bytes)
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Incomplete string escape");
		c = (unsigned char)text[i++];
		switch (c) {
		case '"':
		case '\\':
			buffer[written++] = (char)c;
			break;
		case '/':
			if (mode == HX_OS_CFG_TOML_STRING)
				goto invalid;
			buffer[written++] = '/';
			break;
		case 'b':
			buffer[written++] =
				mode == HX_OS_CFG_PROPERTY_STRING ? 'b' : '\b';
			break;
		case 'f':
			buffer[written++] = '\f';
			break;
		case 'n':
			buffer[written++] = '\n';
			break;
		case 'r':
			buffer[written++] = '\r';
			break;
		case 't':
			buffer[written++] = '\t';
			break;
		case 'u':
		case 'U': {
			uint32_t point;
			size_t used = 0;
			if (c == 'U' && mode == HX_OS_CFG_PROPERTY_STRING) {
				buffer[written++] = 'U';
				break;
			}
			if (c == 'U' && mode != HX_OS_CFG_TOML_STRING &&
				mode != HX_OS_CFG_YAML_STRING)
				goto invalid;
			if (!hx_os_cfg_hex(
				    text, bytes, &i, c == 'u' ? 4 : 8, &point))
				goto invalid;
			if (point >= 0xd800 && point <= 0xdbff &&
				(mode == HX_OS_CFG_JSON_STRING ||
					mode == HX_OS_CFG_PROPERTY_STRING ||
					mode == HX_OS_CFG_ENV_STRING)) {
				uint32_t low;
				if (bytes - i < 6 || text[i++] != '\\' ||
					text[i++] != 'u' ||
					!hx_os_cfg_hex(
						text, bytes, &i, 4, &low) ||
					low < 0xdc00 || low > 0xdfff)
					goto invalid;
				point = 0x10000 + ((point - 0xd800) << 10) +
					low - 0xdc00;
			}
			if (hx_os_i_utf_encode(HX_OS_UTF8, point,
				    (unsigned char *)buffer + written,
				    bytes - written, &used) != HX_OS_OK)
				goto invalid;
			written += used;
			break;
		}
		default:
			if (mode == HX_OS_CFG_PROPERTY_STRING)
				buffer[written++] = (char)c;
			else if (mode == HX_OS_CFG_ENV_STRING &&
				(c == '$' || c == '\'' || c == '#'))
				buffer[written++] = (char)c;
			else
				goto invalid;
			break;
		}
	}
	buffer[written] = 0;
	out->data = buffer;
	out->bytes = written;
	return 1;
invalid:
	return hx_os_cfg_reject(parser, HX_OS_SYNTAX, "Invalid string escape");
}

int hx_os_cfg_quoted(
	hx_os_cfg_parser *parser, int mode, hx_os_config_slice *out)
{
	size_t start, end;
	char quote;
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "Quoted string expected");
	quote = parser->text[parser->position++];
	if (quote != '"' && (quote != '\'' || mode == HX_OS_CFG_JSON_STRING))
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "Quoted string expected");
	start = parser->position;
	while (parser->position < parser->length) {
		char c = parser->text[parser->position++];
		if (c == quote) {
			if (quote == '\'' && mode == HX_OS_CFG_YAML_STRING &&
				parser->position < parser->length &&
				parser->text[parser->position] == '\'') {
				++parser->position;
				continue;
			}
			end = parser->position - 1;
			return hx_os_cfg_unescape(parser, parser->text + start,
				end - start, mode, quote, out);
		}
		if (c == '\\' && quote == '"' &&
			parser->position < parser->length)
			++parser->position;
	}
	return hx_os_cfg_reject(
		parser, HX_OS_SYNTAX, "Unterminated quoted string");
}

int hx_os_cfg_number(const char *text, size_t bytes, int toml, int *kind)
{
	int real;
	int valid = hx_os_i_number_valid(text, bytes, toml, &real);
	*kind = real ? HX_OS_CONFIG_REAL : HX_OS_CONFIG_INTEGER;
	return valid;
}

hx_os_status hx_os_cfg_integer(
	hx_os_config_slice value, uint64_t *magnitude, int *negative)
{
	return hx_os_i_number_integer(
		value.data, value.bytes, magnitude, negative);
}

int hx_os_cfg_scalar(hx_os_cfg_parser *parser, hx_os_config_slice text,
	int mode, size_t depth, hx_os_config_node **out)
{
	int kind = HX_OS_CONFIG_STRING;
	hx_os_config_node *node;
	if (hx_os_cfg_equal(text, "true") || hx_os_cfg_equal(text, "false"))
		kind = HX_OS_CONFIG_BOOL;
	else if (hx_os_cfg_equal(text, "null") ||
		(mode == HX_OS_CFG_YAML_STRING &&
			(!text.bytes || hx_os_cfg_equal(text, "~"))))
		kind = HX_OS_CONFIG_NULL;
	else if (!hx_os_cfg_number(text.data, text.bytes,
			 mode == HX_OS_CFG_TOML_STRING, &kind)) {
		kind = HX_OS_CONFIG_STRING;
		if (mode != HX_OS_CFG_YAML_STRING)
			return hx_os_cfg_reject(
				parser, HX_OS_SYNTAX, "Invalid scalar value");
	}
	if (mode == HX_OS_CFG_TOML_STRING && kind == HX_OS_CONFIG_NULL)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"TOML does not have a null value");
	if (mode == HX_OS_CFG_TOML_STRING && kind == HX_OS_CONFIG_INTEGER) {
		uint64_t magnitude;
		int negative;
		hx_os_status status =
			hx_os_cfg_integer(text, &magnitude, &negative);
		if (status != HX_OS_OK ||
			magnitude > (uint64_t)INT64_MAX + (unsigned)negative)
			return hx_os_cfg_reject(parser, HX_OS_RANGE,
				"TOML integer is outside the signed 64-bit "
				"range");
	}
	node = hx_os_cfg_node(parser, kind, depth);
	if (!node)
		return 0;
	node->value = hx_os_cfg_copy(parser, text.data, text.bytes);
	*out = node;
	return node->value.data != NULL;
}
