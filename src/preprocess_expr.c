#include "preprocess_private.h"

typedef struct hx_os_pp_expression {
	hx_os_pp_state *state;
	hx_os_lexer *lexer;
	size_t depth;
} hx_os_pp_expression;

static hx_os_status hx_os_pp_expr_or(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error);

static hx_os_status hx_os_pp_expr_error(hx_os_pp_expression *expression,
	hx_os_status status, const char *message, hx_os_error *error)
{
	return hx_os_i_pp_fail(expression->state, status, message, NULL, error);
}

static hx_os_status hx_os_pp_expr_number(hx_os_pp_expression *expression,
	const hx_os_token *token, int negative, int64_t *value,
	hx_os_error *error)
{
	uint64_t number = 0;
	uint64_t limit = (uint64_t)INT64_MAX + (negative ? 1u : 0u);
	size_t i = 0;
	unsigned base = 10;

	if (token->text.size > 2 && token->text.data[0] == '0' &&
		(token->text.data[1] == 'x' || token->text.data[1] == 'X')) {
		base = 16;
		i = 2;
	}
	for (; i < token->text.size; ++i) {
		unsigned char c = (unsigned char)token->text.data[i];
		unsigned digit;

		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (base == 16 && c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (base == 16 && c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return hx_os_pp_expr_error(expression, HX_OS_SYNTAX,
				"condition requires integer literals", error);
		if (number > (limit - digit) / base)
			return hx_os_pp_expr_error(expression, HX_OS_RANGE,
				"conditional integer is out of range", error);
		number = number * base + digit;
	}
	if (negative)
		*value = number == (uint64_t)INT64_MAX + 1 ? INT64_MIN
							   : -(int64_t)number;
	else
		*value = (int64_t)number;

	return HX_OS_OK;
}

static hx_os_status hx_os_pp_expr_defined(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_token token;
	hx_string_view replacement;
	char name[128];
	int parenthesis = 0;
	hx_os_status status =
		hx_os_lexer_accept(expression->lexer, "(", &parenthesis, error);

	if (status == HX_OS_OK)
		status = hx_os_lexer_expect_kind(expression->lexer,
			HX_OS_TOKEN_IDENTIFIER, &token, error);
	if (status != HX_OS_OK)
		return hx_os_pp_expr_error(expression, status,
			"defined requires a macro name", error);
	status = hx_os_i_pp_macro(
		expression->state, &token, name, &replacement, error);
	if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
		return status;
	*value = status == HX_OS_OK;
	if (parenthesis) {
		status =
			hx_os_lexer_expect(expression->lexer, ")", NULL, error);
		if (status != HX_OS_OK)
			return hx_os_pp_expr_error(expression, status,
				"defined is missing its closing parenthesis",
				error);
	}

	return HX_OS_OK;
}

static hx_os_status hx_os_pp_expr_unary(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_token token;
	hx_os_status status;
	char operation = 0;

	if (expression->depth == 65)
		return hx_os_pp_expr_error(expression, HX_OS_LIMIT,
			"condition expression is too deeply nested", error);
	++expression->depth;
	status = hx_os_lexer_next(expression->lexer, &token, error);
	if (status != HX_OS_OK) {
		status = hx_os_pp_expr_error(expression,
			status == HX_OS_EOF ? HX_OS_SYNTAX : status,
			"condition requires an operand", error);
		goto done;
	}

	if (token.kind == HX_OS_TOKEN_PUNCTUATION && token.text.size == 1)
		operation = token.text.data[0];
	if (operation == '+' || operation == '-' || operation == '!') {
		status = hx_os_lexer_peek(expression->lexer, &token, error);
		if (status == HX_OS_OK && operation != '!' &&
			token.kind == HX_OS_TOKEN_NUMBER) {
			(void)hx_os_lexer_next(expression->lexer, &token, NULL);
			status = hx_os_pp_expr_number(expression, &token,
				operation == '-', value, error);
			goto done;
		}
		if (status != HX_OS_OK) {
			status = hx_os_pp_expr_error(expression,
				status == HX_OS_EOF ? HX_OS_SYNTAX : status,
				"unary operator requires an operand", error);
			goto done;
		}
		status = hx_os_pp_expr_unary(expression, value, error);
		if (status == HX_OS_OK) {
			if (operation == '!')
				*value = !*value;
			else if (operation == '-') {
				if (*value == INT64_MIN)
					status = hx_os_pp_expr_error(expression,
						HX_OS_RANGE,
						"conditional negation overflow",
						error);
				else
					*value = -*value;
			}
		}
	}
	else if (operation == '(') {
		status = hx_os_pp_expr_or(expression, value, error);
		if (status == HX_OS_OK)
			status = hx_os_lexer_expect(
				expression->lexer, ")", NULL, error);
		if (status != HX_OS_OK)
			status = hx_os_pp_expr_error(expression, status,
				"invalid parenthesized condition", error);
	}
	else if (token.kind == HX_OS_TOKEN_NUMBER)
		status = hx_os_pp_expr_number(
			expression, &token, 0, value, error);
	else if (token.kind == HX_OS_TOKEN_IDENTIFIER) {
		if (hx_os_token_is(&token, "defined"))
			status =
				hx_os_pp_expr_defined(expression, value, error);
		else
			*value = hx_os_token_is(&token, "true") ? 1 : 0;
	}
	else
		status = hx_os_pp_expr_error(expression, HX_OS_SYNTAX,
			"unsupported conditional operand", error);

done:
	--expression->depth;

	return status;
}

// Every compound operator is adjacent in the expanded token stream.
static hx_os_status hx_os_pp_expr_operator(hx_os_pp_expression *expression,
	const char *allowed, int *operation, hx_os_error *error)
{
	hx_os_token first, second;
	hx_os_status status =
		hx_os_lexer_peek(expression->lexer, &first, error);
	char c;

	*operation = 0;
	if (status == HX_OS_EOF)
		return HX_OS_OK;
	if (status != HX_OS_OK)
		return status;
	if (first.kind != HX_OS_TOKEN_PUNCTUATION || first.text.size != 1 ||
		!strchr(allowed, first.text.data[0]))
		return HX_OS_OK;
	c = first.text.data[0];
	(void)hx_os_lexer_next(expression->lexer, &first, NULL);
	status = hx_os_lexer_peek(expression->lexer, &second, error);
	if (status != HX_OS_OK && status != HX_OS_EOF)
		return status;
	if (status == HX_OS_OK && second.kind == HX_OS_TOKEN_PUNCTUATION &&
		second.text.size == 1 && second.offset == first.offset + 1 &&
		second.text.data[0] == ((c == '&' || c == '|') ? c : '=')) {
		(void)hx_os_lexer_next(expression->lexer, &second, NULL);
		*operation = (unsigned char)c | 256;
	}
	else if (c == '<' || c == '>')
		*operation = c;
	else
		return hx_os_pp_expr_error(expression, HX_OS_SYNTAX,
			"invalid conditional operator", error);

	return HX_OS_OK;
}

static hx_os_status hx_os_pp_expr_relational(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_status status = hx_os_pp_expr_unary(expression, value, error);

	while (status == HX_OS_OK) {
		int operation;
		int64_t right;

		status = hx_os_pp_expr_operator(
			expression, "<>", &operation, error);
		if (status != HX_OS_OK || !operation)
			return status;
		status = hx_os_pp_expr_unary(expression, &right, error);
		if (status != HX_OS_OK)
			return status;
		switch (operation) {
		case '<':
			*value = *value < right;
			break;
		case '>':
			*value = *value > right;
			break;
		case '<' | 256:
			*value = *value <= right;
			break;
		case '>' | 256:
			*value = *value >= right;
			break;
		default:
			break;
		}
	}

	return status;
}

static hx_os_status hx_os_pp_expr_equal(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_status status =
		hx_os_pp_expr_relational(expression, value, error);

	while (status == HX_OS_OK) {
		int operation;
		int64_t right;

		status = hx_os_pp_expr_operator(
			expression, "=!", &operation, error);
		if (status != HX_OS_OK || !operation)
			return status;
		status = hx_os_pp_expr_relational(expression, &right, error);
		if (status == HX_OS_OK)
			*value = operation == ('=' | 256) ? *value == right
							  : *value != right;
	}

	return status;
}

static hx_os_status hx_os_pp_expr_and(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_status status = hx_os_pp_expr_equal(expression, value, error);

	while (status == HX_OS_OK) {
		int operation;
		int64_t right;

		status = hx_os_pp_expr_operator(
			expression, "&", &operation, error);
		if (status != HX_OS_OK || !operation)
			return status;
		status = hx_os_pp_expr_equal(expression, &right, error);
		if (status == HX_OS_OK)
			*value = *value && right;
	}

	return status;
}

static hx_os_status hx_os_pp_expr_or(
	hx_os_pp_expression *expression, int64_t *value, hx_os_error *error)
{
	hx_os_status status = hx_os_pp_expr_and(expression, value, error);

	while (status == HX_OS_OK) {
		int operation;
		int64_t right;

		status = hx_os_pp_expr_operator(
			expression, "|", &operation, error);
		if (status != HX_OS_OK || !operation)
			return status;
		status = hx_os_pp_expr_and(expression, &right, error);
		if (status == HX_OS_OK)
			*value = *value || right;
	}

	return status;
}

hx_os_status hx_os_i_pp_evaluate(hx_os_pp_state *state, hx_string_view input,
	int64_t *value, hx_os_error *error)
{
	hx_os_pp_expression expression;
	hx_os_status status;

	memset(&expression, 0, sizeof(expression));
	expression.state = state;
	status = hx_os_i_pp_lexer(state, input,
		HX_OS_LEXER_SKIP_NEWLINES | HX_OS_LEXER_C_COMMENTS,
		&expression.lexer, error);
	if (status == HX_OS_OK)
		status = hx_os_pp_expr_or(&expression, value, error);
	if (status == HX_OS_OK)
		status = hx_os_lexer_expect_kind(
			expression.lexer, HX_OS_TOKEN_END, NULL, error);
	if (status != HX_OS_OK)
		status = hx_os_pp_expr_error(&expression, status,
			"invalid or unsupported conditional expression", error);
	hx_os_lexer_destroy(&expression.lexer);

	return status;
}
