#include "config_private.h"

static int hx_os_cfg_xml_prefix(hx_os_cfg_parser *parser, const char *text)
{
	size_t bytes = strlen(text);
	return bytes <= parser->length - parser->position &&
		!memcmp(parser->text + parser->position, text, bytes);
}

static int hx_os_cfg_xml_char(uint32_t point)
{
	return point == 9 || point == 10 || point == 13 ||
		(point >= 0x20 && point <= 0xd7ff) ||
		(point >= 0xe000 && point <= 0xfffd) ||
		(point >= 0x10000 && point <= 0x10ffff);
}

static int hx_os_cfg_xml_name_start(uint32_t c)
{
	return c == ':' || c == '_' || (c >= 'A' && c <= 'Z') ||
		(c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0xd6) ||
		(c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0x2ff) ||
		(c >= 0x370 && c <= 0x37d) || (c >= 0x37f && c <= 0x1fff) ||
		(c >= 0x200c && c <= 0x200d) || (c >= 0x2070 && c <= 0x218f) ||
		(c >= 0x2c00 && c <= 0x2fef) || (c >= 0x3001 && c <= 0xd7ff) ||
		(c >= 0xf900 && c <= 0xfdcf) || (c >= 0xfdf0 && c <= 0xfffd) ||
		(c >= 0x10000 && c <= 0xeffff);
}

static int hx_os_cfg_xml_name(hx_os_cfg_parser *parser, hx_os_config_slice *out)
{
	size_t start = parser->position;
	while (parser->position < parser->length) {
		uint32_t point;
		size_t used;
		int first = parser->position == start;
		if (hx_os_i_utf_decode(HX_OS_UTF8,
			    (const unsigned char *)parser->text +
				    parser->position,
			    parser->length - parser->position, &point,
			    &used) != HX_OS_OK)
			break;
		if (!hx_os_cfg_xml_name_start(point) &&
			(first ||
				!(point == '-' || point == '.' ||
					(point >= '0' && point <= '9') ||
					point == 0xb7 ||
					(point >= 0x300 && point <= 0x36f) ||
					(point >= 0x203f && point <= 0x2040))))
			break;
		parser->position += used;
	}
	if (parser->position == start)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "XML name expected");
	*out = hx_os_cfg_copy(
		parser, parser->text + start, parser->position - start);
	return out->data != NULL;
}

static int hx_os_cfg_xml_entity(hx_os_cfg_parser *parser, const char *text,
	size_t bytes, uint32_t *point)
{
	hx_os_config_slice value = {text, bytes};
	if (hx_os_cfg_equal(value, "lt"))
		*point = '<';
	else if (hx_os_cfg_equal(value, "gt"))
		*point = '>';
	else if (hx_os_cfg_equal(value, "amp"))
		*point = '&';
	else if (hx_os_cfg_equal(value, "quot"))
		*point = '"';
	else if (hx_os_cfg_equal(value, "apos"))
		*point = '\'';
	else if (bytes > 1 && text[0] == '#') {
		size_t i = 1;
		unsigned base = 10;
		uint32_t result = 0;
		if (text[i] == 'x') {
			base = 16;
			++i;
		}
		if (i == bytes)
			goto invalid;
		for (; i < bytes; ++i) {
			unsigned c = (unsigned char)text[i];
			unsigned digit = c >= '0' && c <= '9' ? c - '0'
				: c >= 'a' && c <= 'f'	      ? c - 'a' + 10
				: c >= 'A' && c <= 'F'	      ? c - 'A' + 10
							      : 99;
			if (digit >= base || result > (0x10ffff - digit) / base)
				goto invalid;
			result = result * base + digit;
		}
		if (!hx_os_cfg_xml_char(result))
			goto invalid;
		*point = result;
	}
	else
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"XML named entity is not predefined");
	return 1;
invalid:
	return hx_os_cfg_reject(
		parser, HX_OS_SYNTAX, "Invalid XML character reference");
}

static int hx_os_cfg_xml_text(hx_os_cfg_parser *parser, const char *text,
	size_t bytes, int attribute, int cdata, hx_os_config_slice *out)
{
	char *buffer = hx_os_cfg_alloc(parser, bytes + 1);
	size_t i = 0, written = 0;
	if (!buffer || !hx_os_cfg_work(parser, bytes))
		return 0;
	while (i < bytes) {
		char c = text[i++];
		if (!cdata && c == '&') {
			size_t start = i, used;
			uint32_t point;
			while (i < bytes && text[i] != ';')
				++i;
			if (i == bytes)
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Unterminated XML entity reference");
			if (!hx_os_cfg_xml_entity(
				    parser, text + start, i - start, &point))
				return 0;
			++i;
			if (hx_os_i_utf_encode(HX_OS_UTF8, point,
				    (unsigned char *)buffer + written,
				    bytes - written, &used) != HX_OS_OK)
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Invalid XML character reference");
			written += used;
		}
		else {
			if (attribute && c == '<')
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Less-than sign in XML attribute "
					"value");
			if (!cdata && !attribute && c == ']' && i + 1 < bytes &&
				text[i] == ']' && text[i + 1] == '>')
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"CDATA terminator outside CDATA");
			if (c == '\r') {
				c = '\n';
				if (i < bytes && text[i] == '\n')
					++i;
			}
			if (attribute && (c == '\n' || c == '\t'))
				c = ' ';
			buffer[written++] = c;
		}
	}
	buffer[written] = 0;
	out->data = buffer;
	out->bytes = written;
	return 1;
}

static int hx_os_cfg_xml_attribute(hx_os_cfg_parser *parser,
	hx_os_config_slice *name, hx_os_config_slice *value, int declaration)
{
	size_t start;
	char quote;
	if (!hx_os_cfg_xml_name(parser, name))
		return 0;
	hx_os_cfg_skip(parser, 1, 0);
	if (parser->position == parser->length ||
		parser->text[parser->position++] != '=')
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"XML attribute equals sign expected");
	hx_os_cfg_skip(parser, 1, 0);
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "XML attribute quote expected");
	quote = parser->text[parser->position++];
	if (quote != '\'' && quote != '"')
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "XML attribute quote expected");
	start = parser->position;
	while (parser->position < parser->length &&
		parser->text[parser->position] != quote)
		++parser->position;
	if (parser->position == parser->length)
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "Unterminated XML attribute");
	if (declaration &&
		memchr(parser->text + start, '&', parser->position - start))
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"Entity reference inside XML declaration");
	if (!hx_os_cfg_xml_text(parser, parser->text + start,
		    parser->position - start, 1, 0, value))
		return 0;
	++parser->position;
	return 1;
}

static int hx_os_cfg_xml_declaration(hx_os_cfg_parser *parser)
{
	hx_os_config_slice name, value;
	int stage = 0;
	while (!hx_os_cfg_xml_prefix(parser, "?>")) {
		size_t before = parser->position;
		hx_os_cfg_skip(parser, 1, 0);
		if (hx_os_cfg_xml_prefix(parser, "?>"))
			break;
		if (before == parser->position ||
			!hx_os_cfg_xml_attribute(parser, &name, &value, 1))
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"Invalid XML declaration");
		if (!stage && hx_os_cfg_equal(name, "version") &&
			hx_os_cfg_equal(value, "1.0"))
			stage = 1;
		else if (stage == 1 && hx_os_cfg_equal(name, "encoding")) {
			size_t i;
			static const char utf8[] = "utf-8";
			if (value.bytes != 5)
				goto encoding;
			for (i = 0; i < 5; ++i) {
				unsigned c = (unsigned char)value.data[i];
				if (c >= 'A' && c <= 'Z')
					c += 'a' - 'A';
				if (c != (unsigned char)utf8[i])
					goto encoding;
			}
			stage = 2;
		}
		else if ((stage == 1 || stage == 2) &&
			hx_os_cfg_equal(name, "standalone") &&
			(hx_os_cfg_equal(value, "yes") ||
				hx_os_cfg_equal(value, "no")))
			stage = 3;
		else
			return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
				"Unsupported XML declaration");
	}
	if (!stage)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"XML version declaration expected");
	parser->position += 2;
	return 1;
encoding:
	return hx_os_cfg_reject(parser, HX_OS_INVALID_ENCODING,
		"XML declaration must specify UTF-8");
}

static int hx_os_cfg_xml_misc(hx_os_cfg_parser *parser, int declaration)
{
	if (hx_os_cfg_xml_prefix(parser, "<!--")) {
		parser->position += 4;
		while (parser->position < parser->length) {
			if (hx_os_cfg_xml_prefix(parser, "-->")) {
				parser->position += 3;
				return 1;
			}
			if (hx_os_cfg_xml_prefix(parser, "--"))
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Double hyphen inside XML comment");
			++parser->position;
		}
	}
	else if (hx_os_cfg_xml_prefix(parser, "<?")) {
		hx_os_config_slice target;
		parser->position += 2;
		if (!hx_os_cfg_xml_name(parser, &target))
			return 0;
		if (target.bytes == 3 &&
			(target.data[0] == 'x' || target.data[0] == 'X') &&
			(target.data[1] == 'm' || target.data[1] == 'M') &&
			(target.data[2] == 'l' || target.data[2] == 'L')) {
			if (!declaration || !hx_os_cfg_equal(target, "xml"))
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Misplaced XML declaration");
			return hx_os_cfg_xml_declaration(parser);
		}
		if (!hx_os_cfg_xml_prefix(parser, "?>") &&
			(parser->position == parser->length ||
				!hx_os_cfg_space(
					(unsigned char)parser
						->text[parser->position])))
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"XML processing instruction separator "
				"expected");
		while (parser->position < parser->length) {
			if (hx_os_cfg_xml_prefix(parser, "?>")) {
				parser->position += 2;
				return 1;
			}
			++parser->position;
		}
	}
	else
		return hx_os_cfg_reject(parser, HX_OS_NOT_SUPPORTED,
			"DTD and XML declarations are not supported here");
	return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
		"Unterminated XML comment or processing instruction");
}

static int hx_os_cfg_xml_element(
	hx_os_cfg_parser *parser, size_t depth, hx_os_config_node **out)
{
	hx_os_config_node *element;
	if (parser->position == parser->length ||
		parser->text[parser->position++] != '<')
		return hx_os_cfg_reject(
			parser, HX_OS_SYNTAX, "XML element expected");
	element = hx_os_cfg_node(parser, HX_OS_CONFIG_ELEMENT, depth);
	if (!element || !hx_os_cfg_xml_name(parser, &element->name))
		return 0;
	for (;;) {
		size_t before = parser->position;
		hx_os_config_node *attribute;
		hx_os_cfg_skip(parser, 1, 0);
		if (hx_os_cfg_xml_prefix(parser, "/>")) {
			parser->position += 2;
			*out = element;
			return 1;
		}
		if (hx_os_cfg_xml_prefix(parser, ">")) {
			++parser->position;
			break;
		}
		if (before == parser->position)
			return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
				"XML attribute separator expected");
		if (!element->attributes)
			element->attributes = hx_os_cfg_node(
				parser, HX_OS_CONFIG_OBJECT, depth);
		attribute = hx_os_cfg_node(
			parser, HX_OS_CONFIG_ATTRIBUTE, depth + 1);
		if (!element->attributes || !attribute ||
			!hx_os_cfg_xml_attribute(parser, &attribute->name,
				&attribute->value, 0) ||
			!hx_os_cfg_append(parser, element->attributes,
				attribute, HX_OS_CONFIG_DUPLICATE_ERROR))
			return 0;
	}
	while (parser->position < parser->length) {
		hx_os_config_node *child;
		if (hx_os_cfg_xml_prefix(parser, "</")) {
			hx_os_config_slice name;
			parser->position += 2;
			if (!hx_os_cfg_xml_name(parser, &name))
				return 0;
			hx_os_cfg_skip(parser, 1, 0);
			if (!hx_os_cfg_xml_prefix(parser, ">") ||
				name.bytes != element->name.bytes ||
				memcmp(name.data, element->name.data,
					name.bytes))
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"XML closing element does not match");
			++parser->position;
			*out = element;
			return 1;
		}
		if (hx_os_cfg_xml_prefix(parser, "<![CDATA[")) {
			size_t start;
			parser->position += 9;
			start = parser->position;
			while (parser->position < parser->length &&
				!hx_os_cfg_xml_prefix(parser, "]]>"))
				++parser->position;
			if (parser->position == parser->length)
				return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
					"Unterminated XML CDATA");
			child = hx_os_cfg_node(
				parser, HX_OS_CONFIG_TEXT, depth + 1);
			if (!child ||
				!hx_os_cfg_xml_text(parser,
					parser->text + start,
					parser->position - start, 0, 1,
					&child->value))
				return 0;
			parser->position += 3;
		}
		else if (hx_os_cfg_xml_prefix(parser, "<?") ||
			hx_os_cfg_xml_prefix(parser, "<!")) {
			if (!hx_os_cfg_xml_misc(parser, 0))
				return 0;
			continue;
		}
		else if (parser->text[parser->position] == '<') {
			if (!hx_os_cfg_xml_element(parser, depth + 1, &child))
				return 0;
		}
		else {
			size_t start = parser->position;
			while (parser->position < parser->length &&
				parser->text[parser->position] != '<')
				++parser->position;
			child = hx_os_cfg_node(
				parser, HX_OS_CONFIG_TEXT, depth + 1);
			if (!child ||
				!hx_os_cfg_xml_text(parser,
					parser->text + start,
					parser->position - start, 0, 0,
					&child->value))
				return 0;
		}
		if (!hx_os_cfg_append(parser, element, child,
			    HX_OS_CONFIG_DUPLICATE_ERROR))
			return 0;
	}
	return hx_os_cfg_reject(
		parser, HX_OS_SYNTAX, "Unterminated XML element");
}

int hx_os_cfg_xml(hx_os_cfg_parser *parser, hx_os_config_node **out)
{
	size_t position = parser->position, first = position;
	while (position < parser->length) {
		uint32_t point;
		size_t used;
		if (hx_os_i_utf_decode(HX_OS_UTF8,
			    (const unsigned char *)parser->text + position,
			    parser->length - position, &point,
			    &used) != HX_OS_OK ||
			!hx_os_cfg_xml_char(point)) {
			parser->position = position;
			return hx_os_cfg_reject(
				parser, HX_OS_SYNTAX, "Invalid XML character");
		}
		position += used;
	}
	hx_os_cfg_skip(parser, 1, 0);
	while (hx_os_cfg_xml_prefix(parser, "<?") ||
		hx_os_cfg_xml_prefix(parser, "<!")) {
		if (!hx_os_cfg_xml_misc(parser, parser->position == first))
			return 0;
		hx_os_cfg_skip(parser, 1, 0);
	}
	if (!hx_os_cfg_xml_element(parser, 1, out))
		return 0;
	hx_os_cfg_skip(parser, 1, 0);
	while (hx_os_cfg_xml_prefix(parser, "<?") ||
		hx_os_cfg_xml_prefix(parser, "<!")) {
		if (!hx_os_cfg_xml_misc(parser, 0))
			return 0;
		hx_os_cfg_skip(parser, 1, 0);
	}
	if (parser->position != parser->length)
		return hx_os_cfg_reject(parser, HX_OS_SYNTAX,
			"Trailing content after XML root element");
	return 1;
}
