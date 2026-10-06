#include "internal.h"
#include "hx_os_lexer.h"

static int hx_os_parser_matches(
	const hx_os_token *token, const char *text, size_t bytes)
{
	return token->text.size == bytes &&
		(!token->text.size ||
			(token->text.data &&
				!memcmp(token->text.data, text,
					token->text.size)));
}

int hx_os_token_is(const hx_os_token *token, const char *text)
{
	return token && text && hx_os_parser_matches(token, text, strlen(text));
}

hx_os_status hx_os_lexer_accept(
	hx_os_lexer *lexer, const char *text, int *matched, hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status;

	if (!text || !text[0] || !matched)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_lexer_peek(lexer, &token, error);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;
	if (status == HX_OS_EOF || !hx_os_token_is(&token, text)) {
		*matched = 0;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	status = hx_os_lexer_next(lexer, &token, error);
	if (status == HX_OS_OK)
		*matched = 1;

	return status;
}

static hx_os_status hx_os_parser_expected(hx_os_lexer *lexer, const char *text,
	int kind, hx_os_token *out, hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status = hx_os_lexer_peek(lexer, &token, error);

	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;
	if (status == HX_OS_EOF && (text || kind != HX_OS_TOKEN_END))
		return hx_os_i_error(error, HX_OS_TRUNCATED_INPUT, 0, 0);
	if (text ? !hx_os_token_is(&token, text) : token.kind != kind)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
	status = hx_os_lexer_next(lexer, &token, error);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;
	if (out)
		*out = token;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_lexer_expect(hx_os_lexer *lexer, const char *text,
	hx_os_token *out, hx_os_error *error)
{
	if (!text || !text[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_parser_expected(lexer, text, 0, out, error);
}

hx_os_status hx_os_lexer_expect_kind(
	hx_os_lexer *lexer, int kind, hx_os_token *out, hx_os_error *error)
{
	if (kind < HX_OS_TOKEN_END || kind > HX_OS_TOKEN_COMMENT)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_parser_expected(lexer, NULL, kind, out, error);
}

hx_os_status hx_os_lexer_read_string(
	hx_os_lexer *lexer, hx_string *out, hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_lexer_peek(lexer, &token, error);
	if (status == HX_OS_EOF)
		return hx_os_i_error(error, HX_OS_TRUNCATED_INPUT, 0, 0);
	if (status != HX_OS_OK)
		return status;
	if (token.kind != HX_OS_TOKEN_STRING)
		return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);

	status = hx_os_token_string(&token, out, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_lexer_next(lexer, &token, error);
}

static char hx_os_parser_closer(char opening)
{
	switch (opening) {
	case '(':
		return ')';
	case '[':
		return ']';
	case '{':
		return '}';
	default:
		return 0;
	}
}

hx_os_status hx_os_lexer_skip_group_with_limit(
	hx_os_lexer *lexer, size_t max_depth, hx_os_error *error)
{
	char stack[256];
	size_t depth = 0;
	hx_os_token token;
	hx_os_status status;

	if (!max_depth || max_depth > sizeof(stack))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	for (;;) {
		char punctuation = 0, closing;

		status = hx_os_lexer_peek(lexer, &token, error);
		if (status == HX_OS_EOF)
			return hx_os_i_error(
				error, HX_OS_TRUNCATED_INPUT, 0, 0);
		if (status != HX_OS_OK)
			return status;
		if (token.kind == HX_OS_TOKEN_PUNCTUATION &&
			token.text.size == 1)
			punctuation = token.text.data[0];
		closing = hx_os_parser_closer(punctuation);
		if (closing) {
			if (depth == max_depth)
				return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			stack[depth++] = closing;
		}
		else if (!depth)
			return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
		else if (punctuation == ')' || punctuation == ']' ||
			punctuation == '}') {
			if (stack[depth - 1] != punctuation)
				return hx_os_i_error(error, HX_OS_SYNTAX, 0, 0);
			--depth;
		}

		status = hx_os_lexer_next(lexer, &token, error);
		if (status != HX_OS_OK || !depth)
			return status;
	}
}

hx_os_status hx_os_lexer_skip_group(hx_os_lexer *lexer, hx_os_error *error)
{
	return hx_os_lexer_skip_group_with_limit(lexer, 64, error);
}

hx_os_status hx_os_lexer_skip_until(
	hx_os_lexer *lexer, const char *text, hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status;
	size_t bytes;

	if (!text || !text[0])
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = strlen(text);
	do {
		status = hx_os_lexer_next(lexer, &token, error);
	} while (status == HX_OS_OK &&
		!hx_os_parser_matches(&token, text, bytes));

	return status;
}
