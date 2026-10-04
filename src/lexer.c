#include "internal.h"
#include "hx_os_lexer.h"

struct hx_os_lexer {
	hx_string input;
	hx_os_lexer_options options;
	size_t position, line, column, tokens;
	hx_os_status failure;
};

hx_os_status hx_os_lexer_options_init(
	hx_os_lexer_options *options, size_t capacity)
{
	const hx_os_lexer_options defaults = HX_OS_LEXER_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;

	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;

	*options = defaults;

	return HX_OS_OK;
}
#if HX_OS_ENABLE_LEXER
static int hx_os_lex_digit(unsigned char c)
{
	return c >= '0' && c <= '9';
}

static int hx_os_lex_name(unsigned char c)
{
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		c >= 0x80;
}

static int hx_os_lex_hex(unsigned char c)
{
	if (hx_os_lex_digit(c))
		return c - '0';

	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;

	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;

	return -1;
}

static int hx_os_lex_hex4(
	const char *text, size_t bytes, size_t *i, uint32_t *point)
{
	unsigned n;
	uint32_t value = 0;

	if (bytes - *i < 4)
		return 0;

	for (n = 0; n < 4; ++n) {
		int digit = hx_os_lex_hex((unsigned char)text[(*i)++]);
		if (digit < 0)
			return 0;

		value = (value << 4) | (unsigned)digit;
	}

	*point = value;

	return 1;
}

/* Shared validation/decoding pass. No allocation when output is NULL. */
static hx_os_status hx_os_lex_unquote(
	hx_string_view source, char *output, size_t *written)
{
	size_t i = 1, used = 0, end;
	char quote;

	if (!source.data || source.size < 2)
		return HX_OS_SYNTAX;

	quote = source.data[0];
	if ((quote != '"' && quote != '\'') ||
		source.data[source.size - 1] != quote)
		return HX_OS_SYNTAX;
	end = source.size - 1;

	while (i < end) {
		unsigned char c = (unsigned char)source.data[i++];

		if (c < 0x20 || c == (unsigned char)quote)
			return HX_OS_SYNTAX;

		if (c == '\\') {
			uint32_t point;
			unsigned char encoded[4];
			size_t bytes;

			if (i == end)
				return HX_OS_SYNTAX;

			c = (unsigned char)source.data[i++];

			switch (c) {
			case '"':
			case '\'':
			case '\\':
			case '/':
				break;
			case 'n':
				c = '\n';
				break;
			case 'r':
				c = '\r';
				break;
			case 't':
				c = '\t';
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'u':
				if (!hx_os_lex_hex4(
					    source.data, end, &i, &point))
					return HX_OS_SYNTAX;
				if (point >= 0xd800 && point <= 0xdbff) {
					uint32_t low;

					if (end - i < 6 ||
						source.data[i] != '\\' ||
						source.data[i + 1] != 'u')
						return HX_OS_SYNTAX;
					i += 2;
					if (!hx_os_lex_hex4(source.data, end,
						    &i, &low) ||
						low < 0xdc00 || low > 0xdfff)
						return HX_OS_SYNTAX;
					point = 0x10000 +
						((point - 0xd800) << 10) + low -
						0xdc00;
				}

				if (hx_os_i_utf_encode(HX_OS_UTF8, point,
					    encoded, sizeof(encoded),
					    &bytes) != HX_OS_OK)
					return HX_OS_SYNTAX;
				if (output)
					memcpy(output + used, encoded, bytes);

				used += bytes;
				continue;

			default:
				return HX_OS_SYNTAX;
			}
		}

		if (output)
			output[used] = (char)c;

		++used;
	}

	if (output)
		output[used] = 0;

	*written = used;

	return HX_OS_OK;
}

static void hx_os_lex_advance(hx_os_lexer *lexer, size_t end)
{
	const char *text = hx_string_c_str(&lexer->input);

	while (lexer->position < end) {
		unsigned char c = (unsigned char)text[lexer->position++];

		if (c == '\r' || c == '\n') {
			if (c != '\n' || lexer->position < 2 ||
				text[lexer->position - 2] != '\r')
				++lexer->line;
			lexer->column = 1;
		}
		else if ((c & 0xc0) != 0x80)
			++lexer->column;
	}
}

static hx_os_status hx_os_lex_one(hx_os_lexer *lexer, hx_os_token *token)
{
	const char *text = hx_string_c_str(&lexer->input);

	size_t i = lexer->position, begin = i, size = lexer->input.size;
	unsigned char c;

	token->offset = i;
	token->line = lexer->line;
	token->column = lexer->column;
	token->text.data = text + i;
	token->text.size = 0;
	if (i == size) {
		token->kind = HX_OS_TOKEN_END;

		return HX_OS_EOF;
	}

	c = (unsigned char)text[i++];
	if (c == '\r' || c == '\n') {
		token->kind = HX_OS_TOKEN_NEWLINE;
		if (c == '\r' && i < size && text[i] == '\n')
			++i;
	}
	else if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
		token->kind = HX_OS_TOKEN_SPACE;

		while (i < size &&
			(text[i] == ' ' || text[i] == '\t' || text[i] == '\v' ||
				text[i] == '\f'))
			++i;
	}
	else if (((lexer->options.flags & HX_OS_LEXER_HASH_COMMENTS) &&
			 c == '#') ||
		((lexer->options.flags & HX_OS_LEXER_C_COMMENTS) && c == '/' &&
			i < size && (text[i] == '/' || text[i] == '*'))) {
		int block = c == '/' && text[i] == '*';

		token->kind = HX_OS_TOKEN_COMMENT;
		if (block) {
			++i;

			while (i + 1 < size &&
				!(text[i] == '*' && text[i + 1] == '/'))
				++i;
			if (i + 1 >= size)
				return HX_OS_SYNTAX;

			i += 2;
		}
		else
			while (i < size && text[i] != '\r' && text[i] != '\n')
				++i;
	}
	else if (c == '"' || c == '\'') {
		size_t decoded;
		int closed = 0;

		token->kind = HX_OS_TOKEN_STRING;

		while (i < size) {
			unsigned char next = (unsigned char)text[i++];

			if (next == c) {
				closed = 1;
				break;
			}

			if (next < 0x20)
				return HX_OS_SYNTAX;

			if (next == '\\' && i < size)
				++i;
		}

		if (!closed)
			return HX_OS_SYNTAX;

		token->text.size = i - begin;
		if (hx_os_lex_unquote(token->text, NULL, &decoded) != HX_OS_OK)
			return HX_OS_SYNTAX;
	}
	else if (hx_os_lex_digit(c) ||
		(c == '.' && i < size &&
			hx_os_lex_digit((unsigned char)text[i]))) {
		token->kind = HX_OS_TOKEN_NUMBER;
		if (c == '0' && i < size &&
			(text[i] == 'x' || text[i] == 'X')) {
			size_t digits = ++i;

			while (i < size &&
				hx_os_lex_hex((unsigned char)text[i]) >= 0)
				++i;
			if (i == digits)
				return HX_OS_SYNTAX;
		}
		else {
			while (i < size &&
				hx_os_lex_digit((unsigned char)text[i]))
				++i;
			if (c != '.' && i < size && text[i] == '.') {
				++i;

				while (i < size &&
					hx_os_lex_digit((unsigned char)text[i]))
					++i;
			}

			if (i < size && (text[i] == 'e' || text[i] == 'E')) {
				size_t digits;

				++i;
				if (i < size &&
					(text[i] == '+' || text[i] == '-'))
					++i;
				digits = i;

				while (i < size &&
					hx_os_lex_digit((unsigned char)text[i]))
					++i;
				if (digits == i)
					return HX_OS_SYNTAX;
			}
		}
	}
	else if (hx_os_lex_name(c)) {
		token->kind = HX_OS_TOKEN_IDENTIFIER;

		while (i < size &&
			(hx_os_lex_name((unsigned char)text[i]) ||
				hx_os_lex_digit((unsigned char)text[i])))
			++i;
	}
	else {
		if (c < 0x20 || c == 0x7f)
			return HX_OS_SYNTAX;

		token->kind = HX_OS_TOKEN_PUNCTUATION;
	}

	if (i - begin > lexer->options.max_token_bytes ||
		lexer->tokens == lexer->options.max_tokens)
		return HX_OS_LIMIT;
	token->text.size = i - begin;
	hx_os_lex_advance(lexer, i);
	++lexer->tokens;

	return HX_OS_OK;
}
#endif
hx_os_status hx_os_lexer_create(hx_os_context *context, hx_string_view input,
	hx_os_lexer **out, hx_os_error *error)
{
	return hx_os_lexer_create_with_options(
		context, input, NULL, out, error);
}

hx_os_status hx_os_lexer_create_with_options(hx_os_context *context,
	hx_string_view input, const hx_os_lexer_options *options,
	hx_os_lexer **out, hx_os_error *error)
{
#if HX_OS_ENABLE_LEXER
	const hx_os_lexer_options defaults = HX_OS_LEXER_OPTIONS_INIT;
	hx_os_lexer *lexer;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!options)
		options = &defaults;

	if (options->size < sizeof(*options) || options->version != 1 ||
		!options->max_input_bytes ||
		options->max_input_bytes > 64u * 1024u * 1024u ||
		!options->max_token_bytes ||
		options->max_token_bytes > 64u * 1024u * 1024u ||
		!options->max_tokens ||
		options->max_tokens > 16u * 1024u * 1024u ||
		(options->flags & ~31u) || (!input.data && input.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (input.size > options->max_input_bytes)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (input.size && memchr(input.data, 0, input.size))
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	lexer = hx_os_i_zero(context, sizeof(*lexer));
	if (!lexer)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	hx_string_init(&lexer->input, context);
	lexer->options = *options;

	status = hx_string_assign(&lexer->input, input, error);
	if (status != HX_OS_OK) {
		hx_os_lexer_destroy(&lexer);

		return status;
	}

	hx_os_lexer_reset(lexer);
	*out = lexer;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)input;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_lexer_next(
	hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error)
{
#if HX_OS_ENABLE_LEXER
	hx_os_token token;
	hx_os_status status;

	if (!lexer || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (lexer->failure)
		return hx_os_i_error(error, lexer->failure, 0, 0);

	for (;;) {
		status = hx_os_lex_one(lexer, &token);
		if (status != HX_OS_OK && status != HX_OS_EOF) {
			lexer->failure = status;

			return hx_os_i_error(error, status, 0, 0);
		}

		if ((token.kind == HX_OS_TOKEN_SPACE &&
			    !(lexer->options.flags & HX_OS_LEXER_KEEP_SPACE)) ||
			(token.kind == HX_OS_TOKEN_COMMENT &&
				!(lexer->options.flags &
					HX_OS_LEXER_KEEP_COMMENTS)) ||
			(token.kind == HX_OS_TOKEN_NEWLINE &&
				(lexer->options.flags &
					HX_OS_LEXER_SKIP_NEWLINES)))
			continue;
		*out = token;

		return hx_os_i_error(error, status, 0, 0);
	}
#else
	(void)lexer;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_lexer_peek(
	const hx_os_lexer *lexer, hx_os_token *out, hx_os_error *error)
{
#if HX_OS_ENABLE_LEXER
	hx_os_lexer copy;

	if (!lexer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	copy = *lexer;

	return hx_os_lexer_next(&copy, out, error);
#else
	(void)lexer;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_lexer_reset(hx_os_lexer *lexer)
{
	if (!lexer)
		return;

	lexer->position = lexer->tokens = 0;
	lexer->line = lexer->column = 1;
	lexer->failure = HX_OS_OK;
	if (lexer->input.size >= 3 &&
		!memcmp(hx_string_c_str(&lexer->input), "\xef\xbb\xbf", 3))
		lexer->position = 3;
}

void hx_os_lexer_destroy(hx_os_lexer **lexer)
{
	if (!lexer || !*lexer)
		return;

	hx_string_destroy(&(*lexer)->input);
	hx_os_memory_free(*lexer);
	*lexer = NULL;
}

hx_os_status hx_os_lexer_position(
	const hx_os_lexer *lexer, size_t *offset, size_t *line, size_t *column)
{
	if (!lexer)
		return HX_OS_INVALID;

	if (offset)
		*offset = lexer->position;

	if (line)
		*line = lexer->line;

	if (column)
		*column = lexer->column;

	return HX_OS_OK;
}

hx_os_status hx_os_token_string(
	const hx_os_token *token, hx_string *out, hx_os_error *error)
{
#if HX_OS_ENABLE_LEXER
	hx_string temporary;
	size_t bytes, invalid;
	hx_os_status status;

	if (!token || !out || (!token->text.data && token->text.size))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (token->kind != HX_OS_TOKEN_STRING)
		return hx_string_assign(out, token->text, error);

	status = hx_os_text_validate_utf8(
		token->text.data, token->text.size, &invalid);
	if (status == HX_OS_OK)
		status = hx_os_lex_unquote(token->text, NULL, &bytes);

	if (status != HX_OS_OK)
		return hx_os_i_error(error, status, 0, 0);

	hx_string_init(&temporary, out->context);

	status = hx_string_reserve(&temporary, bytes, error);
	if (status == HX_OS_OK)
		status = hx_os_lex_unquote(
			token->text, temporary.data, &temporary.size);

	if (status == HX_OS_OK)
		hx_string_move(out, &temporary);

	hx_string_destroy(&temporary);

	return hx_os_i_error(error, status, 0, 0);
#else
	(void)token;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
