#include "regex_private.h"

typedef struct hx_os_rx_parser {
	hx_os_regex *regex;
	hx_os_error *error;
	hx_os_status status;
	size_t position;
	uint32_t flags, depth, assertions;
	size_t prefix_end;
	int quoted;
} hx_os_rx_parser;

static void hx_os_rx_fail(
	hx_os_rx_parser *parser, hx_os_status status, int code)
{
	if (parser->status == HX_OS_OK)
		parser->status = hx_os_i_regex_error(
			parser->error, status, code, parser->position);
}

static unsigned char hx_os_rx_peek(const hx_os_rx_parser *parser)
{
	return parser->position < parser->regex->pattern.size
		? (unsigned char)parser->regex->pattern.data[parser->position]
		: 0;
}

static int hx_os_rx_more(const hx_os_rx_parser *parser)
{
	return parser->position < parser->regex->pattern.size;
}

static int hx_os_rx_take(hx_os_rx_parser *parser, char value)
{
	if (hx_os_rx_more(parser) &&
		hx_os_rx_peek(parser) == (unsigned char)value) {
		++parser->position;
		return 1;
	}
	return 0;
}

static void hx_os_rx_space(hx_os_rx_parser *parser)
{
	if (!(parser->flags & HX_OS_REGEX_EXTENDED) || parser->quoted)
		return;
	while (hx_os_rx_more(parser)) {
		unsigned char ch = hx_os_rx_peek(parser);

		if (ch == ' ' || (ch >= 9 && ch <= 13))
			++parser->position;
		else if (ch == '#')
			while (hx_os_rx_more(parser) &&
				hx_os_rx_peek(parser) != '\n')
				++parser->position;
		else
			break;
	}
}

static uint32_t hx_os_rx_node_add(hx_os_rx_parser *parser, uint32_t type,
	uint32_t left, uint32_t right, uint32_t value, uint32_t minimum,
	uint32_t maximum)
{
	hx_os_rx_node node = {type, left, right, value, parser->flags, minimum,
		maximum, (uint32_t)parser->position};
	uint32_t index = (uint32_t)(parser->regex->nodes.size / sizeof(node));
	hx_os_bytes_view bytes = {&node, sizeof(node)};

	if (parser->status != HX_OS_OK)
		return HX_OS_RX_NONE;
	parser->status = hx_os_i_regex_compile_work(
		parser->regex, 1, parser->position, parser->error);
	if (parser->status != HX_OS_OK)
		return HX_OS_RX_NONE;
	if (index > parser->regex->pattern.size * 3 + 8) {
		hx_os_rx_fail(parser, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT);
		return HX_OS_RX_NONE;
	}
	parser->status =
		hx_bytes_append(&parser->regex->nodes, bytes, parser->error);
	return parser->status == HX_OS_OK ? index : HX_OS_RX_NONE;
}

static hx_os_rx_node *hx_os_rx_node_at(hx_os_rx_parser *parser, uint32_t index)
{
	return (hx_os_rx_node *)parser->regex->nodes.data + index;
}

static uint32_t hx_os_rx_point(hx_os_rx_parser *parser)
{
	uint32_t point = 0;
	size_t used = 0;
	hx_os_status status = hx_os_i_utf_decode(HX_OS_UTF8,
		(const unsigned char *)parser->regex->pattern.data +
			parser->position,
		parser->regex->pattern.size - parser->position, &point, &used);

	if (status != HX_OS_OK)
		hx_os_rx_fail(parser, status, HX_OS_RX_BAD_ESCAPE);
	else
		parser->position += used;
	return point;
}

static int hx_os_rx_word(const char *text, size_t bytes, const char *expected)
{
	return bytes == strlen(expected) && !memcmp(text, expected, bytes);
}

static int hx_os_rx_name(hx_os_rx_parser *parser, char closing,
	uint32_t *offset, uint32_t *length)
{
	size_t start = parser->position;

	while (hx_os_rx_more(parser) &&
		hx_os_rx_peek(parser) != (unsigned char)closing) {
		unsigned char ch = hx_os_rx_peek(parser);

		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			    ch == '_' ||
			    (ch >= '0' && ch <= '9' &&
				    parser->position > start)))
			break;
		++parser->position;
	}
	*offset = (uint32_t)start;
	*length = (uint32_t)(parser->position - start);
	if (!*length || *length > 63 || !hx_os_rx_take(parser, closing)) {
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_REFERENCE);
		return 0;
	}
	return 1;
}

static uint32_t hx_os_rx_number(hx_os_rx_parser *parser)
{
	uint32_t number = 0;
	size_t start = parser->position;

	while (hx_os_rx_peek(parser) >= '0' && hx_os_rx_peek(parser) <= '9' &&
		hx_os_rx_more(parser)) {
		unsigned digit = hx_os_rx_peek(parser) - '0';

		if (number > (UINT32_MAX - digit) / 10) {
			hx_os_rx_fail(parser, HX_OS_RANGE, HX_OS_RX_BAD_REPEAT);
			return 0;
		}
		number = number * 10 + digit;
		++parser->position;
	}
	if (parser->position == start)
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_REFERENCE);
	return number;
}

static uint32_t hx_os_rx_reference(
	hx_os_rx_parser *parser, uint32_t type, char closing, int named)
{
	uint32_t offset = 0, length = 0, number = 0, node;
	int negative = hx_os_rx_take(parser, '-');
	int positive = !negative && hx_os_rx_take(parser, '+');

	if (!named && hx_os_rx_peek(parser) >= '0' &&
		hx_os_rx_peek(parser) <= '9') {
		number = hx_os_rx_number(parser);
		if (negative)
			if (!number || number > parser->regex->group_count)
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REFERENCE);
			else
				number =
					parser->regex->group_count - number + 1;
		else if (positive)
			number += parser->regex->group_count;
		if (closing && !hx_os_rx_take(parser, closing))
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_REFERENCE);
	}
	else {
		named = 1;
		if (negative || positive)
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_REFERENCE);
		(void)hx_os_rx_name(parser, closing, &offset, &length);
	}
	node = hx_os_rx_node_add(parser, type, HX_OS_RX_NONE, HX_OS_RX_NONE,
		named ? offset : number, length, 0);
	if (node != HX_OS_RX_NONE && named)
		hx_os_rx_node_at(parser, node)->flags |= HX_OS_RX_NAMED;
	return node;
}

static uint32_t hx_os_rx_property(hx_os_rx_parser *parser)
{
	static const char *const categories[] = {"", "Lu", "Ll", "Lt", "Lm",
		"Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps",
		"Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl",
		"Zp", "Cc", "Cf", "Cs", "Co", "Cn"};
	const char *name;
	size_t start = parser->position, bytes, i;
	int braced = hx_os_rx_take(parser, '{');

	if (braced) {
		start = parser->position;
		while (hx_os_rx_more(parser) && hx_os_rx_peek(parser) != '}')
			++parser->position;
	}
	else if (hx_os_rx_more(parser))
		++parser->position;
	bytes = parser->position - start;
	name = parser->regex->pattern.data + start;
	if (braced && !hx_os_rx_take(parser, '}'))
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_CLASS);
	if (!HX_OS_HAVE_UNICODE)
		hx_os_rx_fail(
			parser, HX_OS_NOT_SUPPORTED, HX_OS_RX_UNSUPPORTED);
	for (i = 1; i < sizeof(categories) / sizeof(categories[0]); ++i)
		if (hx_os_rx_word(name, bytes, categories[i]))
			return HX_OS_RX_CATEGORY + (uint32_t)i;
	if (bytes == 1) {
		const char *groups = "LMNPSZC";
		const char *found = strchr(groups, *name);

		if (found)
			return HX_OS_RX_CATEGORY + 64 +
				(uint32_t)(found - groups);
	}
	if (hx_os_rx_word(name, bytes, "Lc") ||
		hx_os_rx_word(name, bytes, "L&"))
		return HX_OS_RX_CASED;
	if (hx_os_rx_word(name, bytes, "Any"))
		return HX_OS_RX_ANY;
	if (hx_os_rx_word(name, bytes, "ASCII"))
		return HX_OS_RX_ASCII;
	if (hx_os_rx_word(name, bytes, "Xan"))
		return HX_OS_RX_ALNUM;
	if (hx_os_rx_word(name, bytes, "Xwd"))
		return HX_OS_RX_WORD;
	if (hx_os_rx_word(name, bytes, "Xps") ||
		hx_os_rx_word(name, bytes, "Xsp"))
		return HX_OS_RX_SPACE;
	hx_os_rx_fail(parser, HX_OS_NOT_SUPPORTED, HX_OS_RX_UNSUPPORTED);
	return 0;
}

static int hx_os_rx_hex(unsigned char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

static uint32_t hx_os_rx_encoded(
	hx_os_rx_parser *parser, unsigned base, unsigned fixed)
{
	uint32_t point = 0;
	unsigned digits = 0;
	int braced = hx_os_rx_take(parser, '{');

	while (hx_os_rx_more(parser) && (braced || digits < fixed)) {
		int value = hx_os_rx_hex(hx_os_rx_peek(parser));

		if (value < 0 || (unsigned)value >= base)
			break;
		if (point > (UINT32_C(0x10ffff) - (unsigned)value) / base) {
			hx_os_rx_fail(parser, HX_OS_RANGE, HX_OS_RX_BAD_ESCAPE);
			break;
		}
		point = point * base + (unsigned)value;
		++parser->position;
		++digits;
	}
	if (!digits || (braced && !hx_os_rx_take(parser, '}')) ||
		(!braced && digits != fixed) ||
		(point >= 0xd800 && point <= 0xdfff))
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
	return point;
}

static hx_os_rx_range hx_os_rx_escape_range(
	hx_os_rx_parser *parser, int in_class, uint32_t *type)
{
	hx_os_rx_range range = {0, 0, 0, 0};
	unsigned char ch;

	*type = HX_OS_RX_CHAR;
	if (!hx_os_rx_more(parser)) {
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		return range;
	}
	ch = hx_os_rx_peek(parser);
	++parser->position;
	switch (ch) {
	case 'd':
	case 'D':
		range.property = HX_OS_RX_DIGIT;
		range.negate = ch == 'D';
		break;
	case 'w':
	case 'W':
		range.property = HX_OS_RX_WORD;
		range.negate = ch == 'W';
		break;
	case 's':
	case 'S':
		range.property = HX_OS_RX_SPACE;
		range.negate = ch == 'S';
		break;
	case 'h':
	case 'H':
		range.property = HX_OS_RX_HSPACE;
		range.negate = ch == 'H';
		break;
	case 'v':
	case 'V':
		range.property = HX_OS_RX_VSPACE;
		range.negate = ch == 'V';
		break;
	case 'p':
	case 'P':
		range.property =
			hx_os_rx_property(parser) | HX_OS_RX_UNICODE_PROPERTY;
		range.negate = ch == 'P';
		break;
	case 'a':
		range.first = 7;
		break;
	case 'e':
		range.first = 27;
		break;
	case 'f':
		range.first = 12;
		break;
	case 'n':
		range.first = 10;
		break;
	case 'r':
		range.first = 13;
		break;
	case 't':
		range.first = 9;
		break;
	case 'b':
		if (in_class)
			range.first = 8;
		else {
			*type = HX_OS_RX_BOUNDARY;
			range.first = 0;
		}
		break;
	case 'B':
		if (in_class)
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		*type = HX_OS_RX_BOUNDARY;
		range.first = 1;
		break;
	case 'x':
		range.first = hx_os_rx_encoded(parser, 16, 2);
		break;
	case 'o':
		if (hx_os_rx_peek(parser) != '{')
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		range.first = hx_os_rx_encoded(parser, 8, 3);
		break;
	case 'c':
		if (!hx_os_rx_more(parser) || hx_os_rx_peek(parser) > 127)
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		else {
			ch = hx_os_rx_peek(parser);
			++parser->position;
			if (ch >= 'a' && ch <= 'z')
				ch -= 32;
			range.first = ch ^ 64u;
		}
		break;
	case '0': {
		unsigned digits = 0;

		while (digits < 2 && hx_os_rx_peek(parser) >= '0' &&
			hx_os_rx_peek(parser) <= '7' && hx_os_rx_more(parser)) {
			range.first =
				range.first * 8 + hx_os_rx_peek(parser) - '0';
			++parser->position;
			++digits;
		}
		break;
	}
	case 'A':
	case 'z':
	case 'Z':
	case 'G':
		*type = HX_OS_RX_ANCHOR;
		range.first = ch;
		break;
	case 'R':
		*type = HX_OS_RX_NEWLINE;
		break;
	case 'X':
		*type = HX_OS_RX_GRAPHEME;
		if (!HX_OS_HAVE_UNICODE)
			hx_os_rx_fail(parser, HX_OS_NOT_SUPPORTED,
				HX_OS_RX_UNSUPPORTED);
		break;
	case 'N':
		*type = HX_OS_RX_DOT;
		break;
	case 'K':
		if (parser->assertions)
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		*type = HX_OS_RX_RESET;
		break;
	default:
		if (ch >= 128) {
			--parser->position;
			range.first = hx_os_rx_point(parser);
			break;
		}
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
			(ch >= '1' && ch <= '9'))
			hx_os_rx_fail(
				parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
		range.first = ch;
		break;
	}
	range.last = range.first;
	if (range.property)
		*type = HX_OS_RX_CLASS;
	if (in_class && *type != HX_OS_RX_CHAR && *type != HX_OS_RX_CLASS)
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_ESCAPE);
	return range;
}

static void hx_os_rx_range_append(hx_os_rx_parser *parser, hx_os_rx_range range)
{
	hx_os_bytes_view view = {&range, sizeof(range)};

	if (parser->status == HX_OS_OK)
		parser->status = hx_os_i_regex_append(parser->regex,
			&parser->regex->ranges, view, parser->position,
			parser->error);
}

static void hx_os_rx_range_add(hx_os_rx_parser *parser, hx_os_rx_range range)
{
	hx_os_rx_range extra = {HX_OS_RX_NONE, 0, 0, 0};
	uint32_t point;

	hx_os_rx_range_append(parser, range);
	if (parser->status != HX_OS_OK || range.property ||
		!(parser->flags & HX_OS_REGEX_CASELESS))
		return;
	parser->status = hx_os_i_regex_compile_work(parser->regex,
		range.last - range.first + 1, parser->position, parser->error);
	/* Folding is not monotonic: retain the union, not folded endpoints. */
	for (point = range.first;
		point <= range.last && parser->status == HX_OS_OK; ++point) {
		uint32_t folded = hx_os_i_regex_fold(point);

		if (folded >= range.first && folded <= range.last)
			continue;
		if (extra.first != HX_OS_RX_NONE &&
			(folded < extra.first || folded > extra.last + 1)) {
			hx_os_rx_range_append(parser, extra);
			extra.first = HX_OS_RX_NONE;
		}
		if (extra.first == HX_OS_RX_NONE)
			extra.first = extra.last = folded;
		else if (folded > extra.last)
			extra.last = folded;
	}
	if (extra.first != HX_OS_RX_NONE)
		hx_os_rx_range_append(parser, extra);
}

static uint32_t hx_os_rx_class_add(
	hx_os_rx_parser *parser, hx_os_rx_class character_class)
{
	uint32_t index = (uint32_t)(parser->regex->classes.size /
		sizeof(character_class));
	hx_os_bytes_view bytes = {&character_class, sizeof(character_class)};

	if (parser->status == HX_OS_OK)
		parser->status = hx_os_i_regex_append(parser->regex,
			&parser->regex->classes, bytes, parser->position,
			parser->error);
	return hx_os_rx_node_add(parser, HX_OS_RX_CLASS, HX_OS_RX_NONE,
		HX_OS_RX_NONE, index, 1, 1);
}

static uint32_t hx_os_rx_posix_property(const char *name, size_t bytes)
{
	static const char *const names[] = {"digit", "word", "space", "hspace",
		"vspace", "alpha", "alnum", "ascii", "any", "lower", "upper",
		"xdigit", "blank", "cntrl", "graph", "print", "punct"};
	size_t i;

	for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
		if (hx_os_rx_word(name, bytes, names[i]))
			return (uint32_t)i + 1;
	return 0;
}

static hx_os_rx_range hx_os_rx_class_item(hx_os_rx_parser *parser)
{
	hx_os_rx_range range = {0, 0, 0, 0};
	uint32_t type;

	if (hx_os_rx_take(parser, '\\'))
		return hx_os_rx_escape_range(parser, 1, &type);
	if (hx_os_rx_take(parser, '[')) {
		if (hx_os_rx_take(parser, ':')) {
			size_t start;
			int negate = hx_os_rx_take(parser, '^');

			start = parser->position;
			while (hx_os_rx_more(parser) &&
				hx_os_rx_peek(parser) != ':')
				++parser->position;
			range.property = hx_os_rx_posix_property(
				parser->regex->pattern.data + start,
				parser->position - start);
			range.negate = (uint32_t)negate;
			if (!range.property || !hx_os_rx_take(parser, ':') ||
				!hx_os_rx_take(parser, ']'))
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_CLASS);
			return range;
		}
		range.first = '[';
	}
	else
		range.first = hx_os_rx_point(parser);
	range.last = range.first;
	return range;
}

static uint32_t hx_os_rx_class_parse(hx_os_rx_parser *parser)
{
	hx_os_rx_class character_class;
	int first = 1;

	character_class.first =
		(uint32_t)(parser->regex->ranges.size / sizeof(hx_os_rx_range));
	character_class.negate = (uint32_t)hx_os_rx_take(parser, '^');
	while (hx_os_rx_more(parser) && parser->status == HX_OS_OK) {
		hx_os_rx_range range;

		if (!first && hx_os_rx_take(parser, ']')) {
			character_class.count =
				(uint32_t)(parser->regex->ranges.size /
					sizeof(hx_os_rx_range)) -
				character_class.first;
			return hx_os_rx_class_add(parser, character_class);
		}
		range = hx_os_rx_class_item(parser);
		if (hx_os_rx_take(parser, '-')) {
			if (hx_os_rx_peek(parser) == ']') {
				hx_os_rx_range dash = {'-', '-', 0, 0};

				hx_os_rx_range_add(parser, range);
				hx_os_rx_range_add(parser, dash);
				first = 0;
				continue;
			}
			else {
				hx_os_rx_range end =
					hx_os_rx_class_item(parser);

				if (range.property || end.property ||
					range.first > end.first)
					hx_os_rx_fail(parser, HX_OS_SYNTAX,
						HX_OS_RX_BAD_CLASS);
				range.last = end.first;
			}
		}
		hx_os_rx_range_add(parser, range);
		first = 0;
	}
	hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_CLASS);
	return HX_OS_RX_NONE;
}

static uint32_t hx_os_rx_expression(hx_os_rx_parser *parser);
static uint32_t hx_os_rx_sequence(hx_os_rx_parser *parser);

static uint32_t hx_os_rx_verb(hx_os_rx_parser *parser)
{
	size_t start = parser->position, bytes;
	uint32_t type = HX_OS_RX_EMPTY, mark = HX_OS_RX_NONE;
	const char *name;

	while (hx_os_rx_more(parser) && hx_os_rx_peek(parser) != ')' &&
		hx_os_rx_peek(parser) != ':')
		++parser->position;
	bytes = parser->position - start;
	name = parser->regex->pattern.data + start;
	if (hx_os_rx_word(name, bytes, "FAIL") ||
		hx_os_rx_word(name, bytes, "F"))
		type = HX_OS_RX_FAIL;
	else if (hx_os_rx_word(name, bytes, "ACCEPT"))
		type = HX_OS_RX_ACCEPT;
	else if (hx_os_rx_word(name, bytes, "COMMIT"))
		type = HX_OS_RX_COMMIT;
	else if (hx_os_rx_word(name, bytes, "PRUNE"))
		type = HX_OS_RX_PRUNE;
	else if (hx_os_rx_word(name, bytes, "SKIP"))
		type = HX_OS_RX_SKIP;
	else if (hx_os_rx_word(name, bytes, "THEN"))
		type = HX_OS_RX_THEN;
	else if (hx_os_rx_word(name, bytes, "MARK") || !bytes)
		type = HX_OS_RX_MARK;
	else if (hx_os_rx_word(name, bytes, "UTF") ||
		hx_os_rx_word(name, bytes, "UTF8") ||
		hx_os_rx_word(name, bytes, "NO_JIT"))
		type = HX_OS_RX_EMPTY;
	else if (hx_os_rx_word(name, bytes, "UCP"))
		parser->flags |= HX_OS_REGEX_UCP;
	else if (hx_os_rx_word(name, bytes, "NOTEMPTY"))
		parser->regex->notempty = 1;
	else if (hx_os_rx_word(name, bytes, "NOTEMPTY_ATSTART"))
		parser->regex->notempty = 2;
	else if (bytes > 12 && !memcmp(name, "LIMIT_MATCH=", 12)) {
		uint64_t limit = 0;
		size_t i;

		for (i = 12; i < bytes; ++i) {
			if (name[i] < '0' || name[i] > '9' ||
				limit > (UINT64_MAX -
						(unsigned)(name[i] - '0')) /
						10) {
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
				break;
			}
			limit = limit * 10 + (unsigned)(name[i] - '0');
		}
		if (!limit)
			hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
		if (!parser->regex->step_limit ||
			limit < parser->regex->step_limit)
			parser->regex->step_limit = limit;
	}
	else
		hx_os_rx_fail(
			parser, HX_OS_NOT_SUPPORTED, HX_OS_RX_UNSUPPORTED);
	if (type == HX_OS_RX_EMPTY && start - 2 != parser->prefix_end)
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	if (hx_os_rx_take(parser, ':')) {
		uint32_t offset = (uint32_t)parser->position, length, i;

		while (hx_os_rx_more(parser) && hx_os_rx_peek(parser) != ')')
			++parser->position;
		length = (uint32_t)parser->position - offset;
		if (!length || length > 255 || !hx_os_rx_take(parser, ')'))
			hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
		if (parser->status == HX_OS_OK) {
			for (i = 0; i < parser->regex->mark_count; ++i)
				if (length == parser->regex->marks[i].length &&
					!memcmp(parser->regex->pattern.data +
							offset,
						parser->regex->pattern.data +
							parser->regex->marks[i]
								.offset,
						length))
					break;
			if (i == parser->regex->mark_count) {
				if (i == 64) {
					hx_os_rx_fail(parser, HX_OS_LIMIT,
						HX_OS_RX_MEMORY_LIMIT);
					return HX_OS_RX_NONE;
				}
				parser->regex->marks[i].offset = offset;
				parser->regex->marks[i].length = length;
				parser->regex->marks[i].slot = 0;
				++parser->regex->mark_count;
			}
			mark = i;
		}
	}
	else if (!hx_os_rx_take(parser, ')'))
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	if (type == HX_OS_RX_EMPTY)
		parser->prefix_end = parser->position;
	if (type == HX_OS_RX_MARK && mark == HX_OS_RX_NONE)
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	return hx_os_rx_node_add(
		parser, type, HX_OS_RX_NONE, HX_OS_RX_NONE, mark, 0, 0);
}

static uint32_t hx_os_rx_condition(hx_os_rx_parser *parser)
{
	uint32_t test = HX_OS_RX_NONE, value = 0, flags = 0;
	uint32_t yes, no;
	size_t name_start = parser->position;

	if (hx_os_rx_take(parser, '?')) {
		int negative = hx_os_rx_take(parser, '!');
		int behind = !negative && hx_os_rx_take(parser, '<');

		if (behind)
			negative = hx_os_rx_take(parser, '!');
		if (!negative && !hx_os_rx_take(parser, '='))
			hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
		++parser->assertions;
		test = hx_os_rx_expression(parser);
		--parser->assertions;
		test = hx_os_rx_node_add(
			parser, HX_OS_RX_ASSERT, test, HX_OS_RX_NONE, 0, 0, 0);
		if (test != HX_OS_RX_NONE)
			hx_os_rx_node_at(parser, test)->flags |=
				(negative ? HX_OS_RX_NEGATE : 0) |
				(behind ? HX_OS_RX_BEHIND : 0);
		if (!hx_os_rx_take(parser, ')'))
			hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	}
	else if (hx_os_rx_peek(parser) == 'R' &&
		parser->position + 1 < parser->regex->pattern.size &&
		parser->regex->pattern.data[parser->position + 1] == '&') {
		uint32_t length;

		parser->position += 2;
		name_start = parser->position;
		(void)hx_os_rx_name(parser, ')', &value, &length);
		flags = HX_OS_RX_NAMED | HX_OS_RX_RECURSION;
	}
	else if (hx_os_rx_peek(parser) >= '0' && hx_os_rx_peek(parser) <= '9') {
		value = hx_os_rx_number(parser);
		if (!hx_os_rx_take(parser, ')'))
			hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	}
	else {
		char closing = ')';
		uint32_t offset, length;

		if (hx_os_rx_take(parser, '<'))
			closing = '>';
		else if (hx_os_rx_take(parser, '\''))
			closing = '\'';
		if (hx_os_rx_name(parser, closing, &offset, &length)) {
			const char *name = parser->regex->pattern.data + offset;

			if (hx_os_rx_word(name, length, "DEFINE"))
				value = UINT32_MAX;
			else {
				value = offset;
				flags = HX_OS_RX_NAMED;
			}
			if (closing != ')' && !hx_os_rx_take(parser, ')'))
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
		}
		(void)name_start;
		if (flags)
			name_start = offset;
	}
	yes = hx_os_rx_sequence(parser);
	no = hx_os_rx_take(parser, '|')
		? hx_os_rx_sequence(parser)
		: hx_os_rx_node_add(parser, HX_OS_RX_EMPTY, HX_OS_RX_NONE,
			  HX_OS_RX_NONE, 0, 0, 0);
	if (!hx_os_rx_take(parser, ')'))
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	if (test != HX_OS_RX_NONE) {
		uint32_t branches = hx_os_rx_node_add(
			parser, HX_OS_RX_ALT, yes, no, 1, 0, 0);

		hx_os_rx_node_at(parser, test)->right = branches;
		return test;
	}
	{
		uint32_t result = hx_os_rx_node_add(
			parser, HX_OS_RX_CONDITION, yes, no, value, 0, 0);

		if (result != HX_OS_RX_NONE && flags) {
			hx_os_rx_node *node = hx_os_rx_node_at(parser, result);
			size_t length = 0;

			while (name_start + length <
				parser->regex->pattern.size) {
				char ch = parser->regex->pattern
						  .data[name_start + length];

				if (!(ch == '_' || (ch >= 'a' && ch <= 'z') ||
					    (ch >= 'A' && ch <= 'Z') ||
					    (ch >= '0' && ch <= '9')))
					break;
				++length;
			}
			node->flags |= flags;
			node->minimum = (uint32_t)length;
		}
		return result;
	}
}

static uint32_t hx_os_rx_group_parse(hx_os_rx_parser *parser)
{
	uint32_t type = HX_OS_RX_GROUP, group = 0, flags = 0;
	uint32_t old_flags = parser->flags, node;
	uint32_t name_offset = 0, name_length = 0;
	int capture = !(parser->flags & HX_OS_REGEX_NO_AUTO_CAPTURE);

	if (hx_os_rx_take(parser, '*'))
		return hx_os_rx_verb(parser);
	if (hx_os_rx_take(parser, '?')) {
		if (hx_os_rx_take(parser, '#')) {
			int closed = 0;

			while (hx_os_rx_more(parser) &&
				!(closed = hx_os_rx_take(parser, ')')))
				++parser->position;
			if (!closed)
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
			return hx_os_rx_node_add(parser, HX_OS_RX_EMPTY,
				HX_OS_RX_NONE, HX_OS_RX_NONE, 0, 0, 0);
		}
		if (hx_os_rx_take(parser, '(')) {
			node = hx_os_rx_condition(parser);
			parser->flags = old_flags;
			return node;
		}
		if (hx_os_rx_take(parser, 'R')) {
			if (!hx_os_rx_take(parser, ')'))
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
			return hx_os_rx_node_add(parser, HX_OS_RX_CALL,
				HX_OS_RX_NONE, HX_OS_RX_NONE, 0, 0, 0);
		}
		if ((hx_os_rx_peek(parser) >= '0' &&
			    hx_os_rx_peek(parser) <= '9') ||
			hx_os_rx_peek(parser) == '+' ||
			(hx_os_rx_peek(parser) == '-' &&
				parser->position + 1 <
					parser->regex->pattern.size &&
				parser->regex->pattern.data[parser->position +
					1] >= '0' &&
				parser->regex->pattern.data[parser->position +
					1] <= '9'))
			return hx_os_rx_reference(
				parser, HX_OS_RX_CALL, ')', 0);
		if (hx_os_rx_take(parser, '&'))
			return hx_os_rx_reference(
				parser, HX_OS_RX_CALL, ')', 1);
		if (hx_os_rx_take(parser, 'P')) {
			if (hx_os_rx_take(parser, '='))
				return hx_os_rx_reference(
					parser, HX_OS_RX_BACKREF, ')', 1);
			if (hx_os_rx_take(parser, '>'))
				return hx_os_rx_reference(
					parser, HX_OS_RX_CALL, ')', 1);
			if (!hx_os_rx_take(parser, '<'))
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
			capture = hx_os_rx_name(
				parser, '>', &name_offset, &name_length);
		}
		else if (hx_os_rx_take(parser, '<')) {
			if (hx_os_rx_take(parser, '=') ||
				hx_os_rx_take(parser, '!')) {
				flags = HX_OS_RX_BEHIND;
				if (parser->regex->pattern
						.data[parser->position - 1] ==
					'!')
					flags |= HX_OS_RX_NEGATE;
				type = HX_OS_RX_ASSERT;
				capture = 0;
			}
			else
				capture = hx_os_rx_name(parser, '>',
					&name_offset, &name_length);
		}
		else if (hx_os_rx_take(parser, '\''))
			capture = hx_os_rx_name(
				parser, '\'', &name_offset, &name_length);
		else if (hx_os_rx_take(parser, '=') ||
			hx_os_rx_take(parser, '!')) {
			type = HX_OS_RX_ASSERT;
			capture = 0;
			if (parser->regex->pattern.data[parser->position - 1] ==
				'!')
				flags = HX_OS_RX_NEGATE;
		}
		else if (hx_os_rx_take(parser, '>')) {
			type = HX_OS_RX_ATOMIC;
			capture = 0;
		}
		else if (hx_os_rx_take(parser, ':'))
			capture = 0;
		else {
			int unset = 0, any = 0;

			while (hx_os_rx_more(parser)) {
				uint32_t flag = 0;
				unsigned char ch = hx_os_rx_peek(parser);

				switch (ch) {
				case 'i':
					flag = HX_OS_REGEX_CASELESS;
					break;
				case 'm':
					flag = HX_OS_REGEX_MULTILINE;
					break;
				case 's':
					flag = HX_OS_REGEX_DOTALL;
					break;
				case 'x':
					flag = HX_OS_REGEX_EXTENDED;
					break;
				case 'U':
					flag = HX_OS_REGEX_UNGREEDY;
					break;
				case 'n':
					flag = HX_OS_REGEX_NO_AUTO_CAPTURE;
					break;
				case '-':
					unset = 1;
					++parser->position;
					continue;
				default:
					break;
				}
				if (!flag)
					break;
				++parser->position;
				any = 1;
				if (unset)
					parser->flags &= ~flag;
				else
					parser->flags |= flag;
			}
			if (!any)
				hx_os_rx_fail(parser, HX_OS_NOT_SUPPORTED,
					HX_OS_RX_UNSUPPORTED);
			if (hx_os_rx_take(parser, ')'))
				return hx_os_rx_node_add(parser, HX_OS_RX_EMPTY,
					HX_OS_RX_NONE, HX_OS_RX_NONE, 0, 0, 0);
			if (!hx_os_rx_take(parser, ':'))
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_GROUP);
			capture = 0;
		}
	}
	if (capture) {
		if (parser->regex->group_count == HX_OS_REGEX_MAX_CAPTURES) {
			hx_os_rx_fail(
				parser, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT);
			return HX_OS_RX_NONE;
		}
		group = ++parser->regex->group_count;
		parser->regex->groups[group].name_offset = name_offset;
		parser->regex->groups[group].name_length = name_length;
	}
	if (type == HX_OS_RX_ASSERT)
		++parser->assertions;
	node = hx_os_rx_expression(parser);
	if (type == HX_OS_RX_ASSERT)
		--parser->assertions;
	if (!hx_os_rx_take(parser, ')'))
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	if (type != HX_OS_RX_GROUP || capture) {
		node = hx_os_rx_node_add(
			parser, type, node, HX_OS_RX_NONE, group, 0, 0);
		if (node != HX_OS_RX_NONE) {
			hx_os_rx_node_at(parser, node)->flags |= flags;
			if (capture)
				parser->regex->groups[group].node = node;
		}
	}
	parser->flags = old_flags;
	return node;
}

static uint32_t hx_os_rx_atom(hx_os_rx_parser *parser)
{
	unsigned char ch;
	uint32_t type, point, node;
	hx_os_rx_range range;

	hx_os_rx_space(parser);
	if (!hx_os_rx_more(parser))
		return hx_os_rx_node_add(parser, HX_OS_RX_EMPTY, HX_OS_RX_NONE,
			HX_OS_RX_NONE, 0, 0, 0);
	if (parser->quoted) {
		point = hx_os_rx_point(parser);
		if (parser->position + 1 < parser->regex->pattern.size &&
			parser->regex->pattern.data[parser->position] == '\\' &&
			parser->regex->pattern.data[parser->position + 1] ==
				'E') {
			parser->position += 2;
			parser->quoted = 0;
		}
		return hx_os_rx_node_add(parser, HX_OS_RX_CHAR, HX_OS_RX_NONE,
			HX_OS_RX_NONE, point, 1, 1);
	}
	ch = hx_os_rx_peek(parser);
	++parser->position;
	switch (ch) {
	case '(':
		if (++parser->depth > parser->regex->options.max_nesting) {
			hx_os_rx_fail(
				parser, HX_OS_LIMIT, HX_OS_RX_DEPTH_LIMIT);
			return HX_OS_RX_NONE;
		}
		node = hx_os_rx_group_parse(parser);
		--parser->depth;
		return node;
	case '[':
		return hx_os_rx_class_parse(parser);
	case '.':
		return hx_os_rx_node_add(parser, HX_OS_RX_DOT, HX_OS_RX_NONE,
			HX_OS_RX_NONE, 0, 1, 1);
	case '^':
	case '$':
		return hx_os_rx_node_add(parser, HX_OS_RX_ANCHOR, HX_OS_RX_NONE,
			HX_OS_RX_NONE, ch, 0, 0);
	case '*':
	case '+':
	case '?':
		hx_os_rx_fail(parser, HX_OS_SYNTAX, HX_OS_RX_BAD_REPEAT);
		return HX_OS_RX_NONE;
	case '\\':
		if (hx_os_rx_peek(parser) >= '1' &&
			hx_os_rx_peek(parser) <= '9')
			return hx_os_rx_reference(
				parser, HX_OS_RX_BACKREF, 0, 0);
		if (hx_os_rx_take(parser, 'k')) {
			char closing = 0;

			if (hx_os_rx_take(parser, '<'))
				closing = '>';
			else if (hx_os_rx_take(parser, '\''))
				closing = '\'';
			else if (hx_os_rx_take(parser, '{'))
				closing = '}';
			if (!closing) {
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REFERENCE);
				return HX_OS_RX_NONE;
			}
			return hx_os_rx_reference(
				parser, HX_OS_RX_BACKREF, closing, 1);
		}
		if (hx_os_rx_take(parser, 'g')) {
			char closing = 0;

			if (hx_os_rx_take(parser, '{'))
				closing = '}';
			else if (hx_os_rx_take(parser, '<'))
				return hx_os_rx_reference(
					parser, HX_OS_RX_CALL, '>', 0);
			else if (hx_os_rx_take(parser, '\''))
				return hx_os_rx_reference(
					parser, HX_OS_RX_CALL, '\'', 0);
			if (!closing) {
				hx_os_rx_fail(parser, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REFERENCE);
				return HX_OS_RX_NONE;
			}
			return hx_os_rx_reference(
				parser, HX_OS_RX_BACKREF, closing, 0);
		}
		if (hx_os_rx_take(parser, 'Q')) {
			if (parser->position + 1 <
					parser->regex->pattern.size &&
				parser->regex->pattern.data[parser->position] ==
					'\\' &&
				parser->regex->pattern.data[parser->position +
					1] == 'E') {
				parser->position += 2;
				return hx_os_rx_node_add(parser, HX_OS_RX_EMPTY,
					HX_OS_RX_NONE, HX_OS_RX_NONE, 0, 0, 0);
			}
			parser->quoted = 1;
			return hx_os_rx_atom(parser);
		}
		range = hx_os_rx_escape_range(parser, 0, &type);
		if (type == HX_OS_RX_CLASS) {
			hx_os_rx_class c;

			c.first = (uint32_t)(parser->regex->ranges.size /
				sizeof(range));
			c.count = 1;
			c.negate = 0;
			hx_os_rx_range_add(parser, range);
			return hx_os_rx_class_add(parser, c);
		}
		node = hx_os_rx_node_add(parser, type, HX_OS_RX_NONE,
			HX_OS_RX_NONE, range.first, 0, 0);
		if (node != HX_OS_RX_NONE && type == HX_OS_RX_DOT)
			hx_os_rx_node_at(parser, node)->flags &=
				~HX_OS_REGEX_DOTALL;
		return node;
	default:
		--parser->position;
		point = hx_os_rx_point(parser);
		return hx_os_rx_node_add(parser, HX_OS_RX_CHAR, HX_OS_RX_NONE,
			HX_OS_RX_NONE, point, 1, 1);
	}
}

static uint32_t hx_os_rx_quantified(hx_os_rx_parser *parser)
{
	uint32_t node = hx_os_rx_atom(parser), minimum = 0, maximum = 0;
	int repeat = 1, lazy, possessive;
	size_t before;

	hx_os_rx_space(parser);
	if (parser->quoted || parser->status != HX_OS_OK)
		return node;
	before = parser->position;
	if (hx_os_rx_take(parser, '*'))
		maximum = HX_OS_RX_INFINITY;
	else if (hx_os_rx_take(parser, '+')) {
		minimum = 1;
		maximum = HX_OS_RX_INFINITY;
	}
	else if (hx_os_rx_take(parser, '?'))
		maximum = 1;
	else if (hx_os_rx_take(parser, '{')) {
		if (hx_os_rx_peek(parser) < '0' || hx_os_rx_peek(parser) > '9')
			repeat = 0;
		else {
			minimum = hx_os_rx_number(parser);
			maximum = minimum;
			if (hx_os_rx_take(parser, ','))
				maximum = hx_os_rx_peek(parser) >= '0' &&
						hx_os_rx_peek(parser) <= '9'
					? hx_os_rx_number(parser)
					: HX_OS_RX_INFINITY;
			if (!hx_os_rx_take(parser, '}'))
				repeat = 0;
		}
		if (!repeat)
			parser->position = before;
	}
	else
		repeat = 0;
	if (!repeat)
		return node;
	if (minimum > parser->regex->options.max_repeat ||
		(maximum != HX_OS_RX_INFINITY &&
			(maximum < minimum ||
				maximum > parser->regex->options.max_repeat))) {
		hx_os_rx_fail(parser, HX_OS_LIMIT, HX_OS_RX_BAD_REPEAT);
		return HX_OS_RX_NONE;
	}
	lazy = !!(parser->flags & HX_OS_REGEX_UNGREEDY);
	if (hx_os_rx_take(parser, '?'))
		lazy = !lazy;
	possessive = hx_os_rx_take(parser, '+');
	node = hx_os_rx_node_add(parser, HX_OS_RX_REPEAT, node, HX_OS_RX_NONE,
		0, minimum, maximum);
	if (node != HX_OS_RX_NONE && lazy)
		hx_os_rx_node_at(parser, node)->flags |= HX_OS_RX_LAZY;
	if (possessive)
		node = hx_os_rx_node_add(
			parser, HX_OS_RX_ATOMIC, node, HX_OS_RX_NONE, 0, 0, 0);
	return node;
}

static void hx_os_rx_join(hx_os_rx_parser *parser, uint32_t roots[32],
	uint32_t type, uint32_t node)
{
	unsigned level;

	for (level = 0; level < 32 && roots[level] != HX_OS_RX_NONE; ++level) {
		node = hx_os_rx_node_add(
			parser, type, roots[level], node, 0, 0, 0);
		roots[level] = HX_OS_RX_NONE;
	}
	if (level < 32)
		roots[level] = node;
	else
		hx_os_rx_fail(parser, HX_OS_LIMIT, HX_OS_RX_MEMORY_LIMIT);
}

static uint32_t hx_os_rx_join_finish(
	hx_os_rx_parser *parser, const uint32_t roots[32], uint32_t type)
{
	uint32_t node = HX_OS_RX_NONE;
	unsigned level = 32;

	while (level--)
		if (roots[level] != HX_OS_RX_NONE)
			node = node == HX_OS_RX_NONE
				? roots[level]
				: hx_os_rx_node_add(parser, type, node,
					  roots[level], 0, 0, 0);
	if (node == HX_OS_RX_NONE && parser->status == HX_OS_OK)
		node = hx_os_rx_node_add(parser, HX_OS_RX_EMPTY, HX_OS_RX_NONE,
			HX_OS_RX_NONE, 0, 0, 0);
	return node;
}

static uint32_t hx_os_rx_sequence(hx_os_rx_parser *parser)
{
	uint32_t roots[32];

	memset(roots, 0xff, sizeof(roots));
	while (parser->status == HX_OS_OK) {
		uint32_t atom;

		hx_os_rx_space(parser);
		if (!hx_os_rx_more(parser) ||
			(!parser->quoted &&
				(hx_os_rx_peek(parser) == ')' ||
					hx_os_rx_peek(parser) == '|')))
			break;
		atom = hx_os_rx_quantified(parser);
		hx_os_rx_join(parser, roots, HX_OS_RX_CONCAT, atom);
	}
	return hx_os_rx_join_finish(parser, roots, HX_OS_RX_CONCAT);
}

static uint32_t hx_os_rx_expression(hx_os_rx_parser *parser)
{
	uint32_t roots[32];

	memset(roots, 0xff, sizeof(roots));
	do {
		uint32_t node = hx_os_rx_sequence(parser);

		hx_os_rx_join(parser, roots, HX_OS_RX_ALT, node);
	} while (parser->status == HX_OS_OK && hx_os_rx_take(parser, '|'));
	return hx_os_rx_join_finish(parser, roots, HX_OS_RX_ALT);
}

hx_os_status hx_os_i_regex_parse(
	hx_os_regex *regex, uint32_t *root, hx_os_error *error)
{
	hx_os_rx_parser parser;
	uint32_t i;
	hx_os_rx_node *nodes;

	memset(&parser, 0, sizeof(parser));
	parser.regex = regex;
	parser.error = error;
	parser.flags = regex->options.flags;
	*root = hx_os_rx_expression(&parser);
	if (parser.status == HX_OS_OK && hx_os_rx_more(&parser))
		hx_os_rx_fail(&parser, HX_OS_SYNTAX, HX_OS_RX_BAD_GROUP);
	if (parser.status != HX_OS_OK)
		return parser.status;
	nodes = (hx_os_rx_node *)regex->nodes.data;
	for (i = 0; i < regex->nodes.size / sizeof(*nodes); ++i) {
		hx_os_rx_node *node = nodes + i;
		uint32_t group;

		if (!HX_OS_HAVE_UNICODE &&
			(node->flags &
				(HX_OS_REGEX_CASELESS | HX_OS_REGEX_UCP)))
			return hx_os_i_regex_error(error, HX_OS_NOT_SUPPORTED,
				HX_OS_RX_UNSUPPORTED, 0);
		if (node->type != HX_OS_RX_BACKREF &&
			node->type != HX_OS_RX_CALL &&
			node->type != HX_OS_RX_CONDITION)
			continue;
		if (node->flags & HX_OS_RX_NAMED) {
			for (group = 1; group <= regex->group_count; ++group)
				if (regex->groups[group].name_length ==
						node->minimum &&
					!memcmp(regex->pattern.data +
							node->value,
						regex->pattern.data +
							regex->groups[group]
								.name_offset,
						node->minimum))
					break;
			if (group > regex->group_count &&
				node->type == HX_OS_RX_CONDITION &&
				!(node->flags & HX_OS_RX_RECURSION) &&
				node->minimum &&
				regex->pattern.data[node->value] == 'R') {
				uint32_t digit, recursive = 0;

				for (digit = 1; digit < node->minimum;
					++digit) {
					unsigned char ch =
						(unsigned char)regex->pattern
							.data[node->value +
								digit];

					if (ch < '0' || ch > '9' ||
						recursive > 64)
						break;
					recursive = recursive * 10 + ch - '0';
				}
				if (digit == node->minimum) {
					group = node->minimum == 1
						? UINT32_MAX - 1
						: recursive;
					node->flags |= HX_OS_RX_RECURSION;
				}
			}
			if (group > regex->group_count &&
				group != UINT32_MAX - 1)
				return hx_os_i_regex_error(error, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REFERENCE, node->value);
			node->value = group;
			node->flags &= ~HX_OS_RX_NAMED;
		}
		if (node->type == HX_OS_RX_BACKREF && !node->value)
			return hx_os_i_regex_error(error, HX_OS_SYNTAX,
				HX_OS_RX_BAD_REFERENCE, node->offset);
		if (node->value > regex->group_count &&
			!(node->type == HX_OS_RX_CONDITION &&
				node->value >= UINT32_MAX - 1))
			return hx_os_i_regex_error(error, HX_OS_SYNTAX,
				HX_OS_RX_BAD_REFERENCE, node->offset);
	}
	for (i = 1; i <= regex->group_count; ++i) {
		uint32_t j;

		if (!regex->groups[i].name_length)
			continue;
		for (j = 1; j < i; ++j)
			if (regex->groups[j].name_length ==
					regex->groups[i].name_length &&
				!memcmp(regex->pattern.data +
						regex->groups[i].name_offset,
					regex->pattern.data +
						regex->groups[j].name_offset,
					regex->groups[i].name_length))
				return hx_os_i_regex_error(error, HX_OS_SYNTAX,
					HX_OS_RX_BAD_REFERENCE,
					regex->groups[i].name_offset);
	}
	return HX_OS_OK;
}
