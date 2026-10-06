#include "utility_test.h"

static hx_os_lexer *create(const char *input)
{
	hx_os_lexer *lexer = NULL;

	CHECK(hx_os_lexer_create(NULL, hx_string_view_from_cstr(input), &lexer,
		      NULL) == HX_OS_OK);

	return lexer;
}

static size_t position(hx_os_lexer *lexer)
{
	size_t offset = SIZE_MAX;

	CHECK(hx_os_lexer_position(lexer, &offset, NULL, NULL) == HX_OS_OK);

	return offset;
}

static void test_read(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_lexer *lexer = create(
		"name = 'hi\\n😀'; ignored = {[1, (\"}\")] /* ] */} tail");
	hx_string text;
	hx_os_token token;
	size_t offset;
	int matched = 99;

	hx_string_init(&text, context);
	CHECK(hx_string_assign_cstr(&text, "unchanged", NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_expect(lexer, "name", &token, NULL) == HX_OS_OK);
	CHECK(hx_os_token_is(&token, "name"));
	offset = position(lexer);
	CHECK(hx_os_lexer_accept(lexer, "+", &matched, NULL) == HX_OS_OK &&
		!matched);
	CHECK(position(lexer) == offset);
	token.kind = 99;
	CHECK(hx_os_lexer_expect_kind(
		      lexer, HX_OS_TOKEN_NUMBER, &token, NULL) == HX_OS_SYNTAX);
	CHECK(token.kind == 99 && position(lexer) == offset);
	CHECK(hx_os_lexer_read_string(lexer, &text, NULL) == HX_OS_SYNTAX);
	CHECK(!strcmp(text.data, "unchanged") && position(lexer) == offset);
	CHECK(hx_os_lexer_expect(lexer, "=", NULL, NULL) == HX_OS_OK);
	offset = position(lexer);
	memory.fail = memory.calls + 1;
	CHECK(hx_os_lexer_read_string(lexer, &text, NULL) == HX_OS_NO_MEMORY);
	CHECK(position(lexer) == offset && !strcmp(text.data, "unchanged"));
	memory.fail = 0;
	CHECK(hx_os_lexer_read_string(lexer, &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, "hi\n😀"));
	CHECK(hx_os_lexer_skip_until(lexer, "ignored", NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_accept(lexer, "=", &matched, NULL) == HX_OS_OK &&
		matched);
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_expect(lexer, "tail", &token, NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_accept(lexer, "tail", &matched, NULL) == HX_OS_OK &&
		!matched);
	CHECK(hx_os_lexer_expect(lexer, "tail", NULL, NULL) ==
		HX_OS_TRUNCATED_INPUT);
	CHECK(hx_os_lexer_read_string(lexer, &text, NULL) ==
		HX_OS_TRUNCATED_INPUT);
	CHECK(hx_os_lexer_expect_kind(lexer, HX_OS_TOKEN_END, &token, NULL) ==
		HX_OS_OK);
	CHECK(token.kind == HX_OS_TOKEN_END);
	CHECK(hx_os_lexer_skip_until(lexer, "missing", NULL) == HX_OS_EOF);

	hx_os_lexer_destroy(&lexer);
	hx_string_destroy(&text);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static void test_groups(void)
{
	char nested[161];
	hx_os_lexer *lexer;
	hx_os_token token;
	hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;
	int matched = 17;

	memset(nested, '(', 80);
	memset(nested + 80, ')', 80);
	nested[160] = 0;
	lexer = create(nested);
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_LIMIT);
	CHECK(position(lexer) == 64);
	hx_os_lexer_reset(lexer);
	CHECK(hx_os_lexer_skip_group_with_limit(lexer, 0, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_lexer_skip_group_with_limit(lexer, 80, NULL) == HX_OS_OK);
	CHECK(position(lexer) == 160);
	hx_os_lexer_destroy(&lexer);

	lexer = create("([)]");
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_SYNTAX);
	CHECK(position(lexer) == 2);
	CHECK(hx_os_lexer_peek(lexer, &token, NULL) == HX_OS_OK &&
		hx_os_token_is(&token, ")"));
	hx_os_lexer_destroy(&lexer);
	lexer = create("not_a_group");
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_SYNTAX &&
		position(lexer) == 0);
	CHECK(hx_os_lexer_expect_kind(lexer, -1, NULL, NULL) == HX_OS_INVALID);
	CHECK(hx_os_lexer_accept(lexer, "", &matched, NULL) == HX_OS_INVALID &&
		matched == 17);
	hx_os_lexer_destroy(&lexer);
	lexer = create("{ [");
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_TRUNCATED_INPUT);
	hx_os_lexer_destroy(&lexer);
	lexer = create("{ '\\q' }");
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_SYNTAX);
	hx_os_lexer_destroy(&lexer);

	options.max_tokens = 3;
	CHECK(hx_os_lexer_create_with_options(NULL,
		      hx_string_view_from_cstr("(((x)))"), &options, &lexer,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_LIMIT);
	CHECK(position(lexer) == 3);
	hx_os_lexer_destroy(&lexer);

	options = (hx_os_lexer_options)HX_OS_LEXER_OPTIONS_INIT;
	options.flags |= HX_OS_LEXER_KEEP_SPACE | HX_OS_LEXER_KEEP_COMMENTS;
	CHECK(hx_os_lexer_create_with_options(NULL,
		      hx_string_view_from_cstr(" /* x */ { ')'; }"), &options,
		      &lexer, NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_expect_kind(lexer, HX_OS_TOKEN_SPACE, NULL, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_lexer_expect_kind(lexer, HX_OS_TOKEN_COMMENT, NULL, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_lexer_expect_kind(lexer, HX_OS_TOKEN_SPACE, NULL, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_lexer_skip_group(lexer, NULL) == HX_OS_OK);
	hx_os_lexer_destroy(&lexer);
}

int main(void)
{
	int matched = 0;

	CHECK(!hx_os_token_is(NULL, "x"));
	if (!(hx_os_capabilities() & HX_OS_CAP_LEXER)) {
		CHECK(hx_os_lexer_accept(NULL, "x", &matched, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_lexer_skip_group(NULL, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return 0;
	}
	test_read();
	test_groups();

	return 0;
}
