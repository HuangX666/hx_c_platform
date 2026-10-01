#include "utility_test.h"
static void test_invalid(const char *input, hx_os_status expected)
{
	hx_os_lexer *lexer = NULL;
	hx_os_token token;
	size_t offset = 999;
	CHECK(hx_os_lexer_create(NULL, hx_string_view_from_cstr(input), &lexer,
		      NULL) == HX_OS_OK);
	memset(&token, 0, sizeof(token));
	token.kind = 99;
	CHECK(hx_os_lexer_next(lexer, &token, NULL) == expected);
	CHECK(token.kind == 99);
	CHECK(hx_os_lexer_next(lexer, &token, NULL) == expected);
	CHECK(hx_os_lexer_position(lexer, &offset, NULL, NULL) == HX_OS_OK);
	CHECK(offset == 0);
	hx_os_lexer_reset(lexer);
	CHECK(hx_os_lexer_peek(lexer, &token, NULL) == expected);
	hx_os_lexer_destroy(&lexer);
}
int main(void)
{
	const char *input = "\xef\xbb\xbf"
			    "中文 = 0x12 + .5e-2\r\n"
			    "# ignored\n\"hi\\n\\uD83D\\uDE00\" /* x */";
	hx_os_lexer *lexer = NULL;
	hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;
	hx_os_token token, peek;
	hx_string decoded = HX_STRING_INIT;
	int kinds[] = {HX_OS_TOKEN_IDENTIFIER, HX_OS_TOKEN_PUNCTUATION,
		HX_OS_TOKEN_NUMBER, HX_OS_TOKEN_PUNCTUATION, HX_OS_TOKEN_NUMBER,
		HX_OS_TOKEN_NEWLINE, HX_OS_TOKEN_NEWLINE, HX_OS_TOKEN_STRING};
	size_t i, line, column;
	if (!(hx_os_capabilities() & HX_OS_CAP_LEXER)) {
		CHECK(hx_os_lexer_create(NULL, hx_string_view_from_cstr("x"),
			      &lexer, NULL) == HX_OS_NOT_SUPPORTED &&
			!lexer);
		return 0;
	}
	CHECK(hx_os_lexer_create(NULL, hx_string_view_from_cstr(input), &lexer,
		      NULL) == HX_OS_OK);
	for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
		CHECK(hx_os_lexer_peek(lexer, &peek, NULL) == HX_OS_OK);
		CHECK(hx_os_lexer_next(lexer, &token, NULL) == HX_OS_OK);
		CHECK(token.kind == kinds[i] && token.offset == peek.offset);
		if (!i)
			CHECK(token.column == 1 && token.offset == 3);
		if (i == 1)
			CHECK(token.column == 4);
	}
	CHECK(token.line == 3);
	CHECK(hx_os_token_string(&token, &decoded, NULL) == HX_OS_OK);
	CHECK(!strcmp(decoded.data, "hi\n😀"));
	CHECK(hx_os_lexer_next(lexer, &token, NULL) == HX_OS_EOF);
	CHECK(token.kind == HX_OS_TOKEN_END);
	hx_os_lexer_reset(lexer);
	CHECK(hx_os_lexer_position(lexer, NULL, &line, &column) == HX_OS_OK);
	CHECK(line == 1 && column == 1);
	hx_os_lexer_destroy(&lexer);
	test_invalid("/*/", HX_OS_SYNTAX);
	test_invalid("\"\\uD800\"", HX_OS_SYNTAX);
	test_invalid("\"\\uDC00\"", HX_OS_SYNTAX);
	test_invalid("'unclosed", HX_OS_SYNTAX);
	test_invalid("\"\\q\"", HX_OS_SYNTAX);
	test_invalid("0x", HX_OS_SYNTAX);
	test_invalid("1e+", HX_OS_SYNTAX);
	CHECK(hx_os_lexer_create(NULL, hx_string_view_from_cstr("\xff"), &lexer,
		      NULL) == HX_OS_INVALID_ENCODING &&
		!lexer);
	options.max_token_bytes = 2;
	CHECK(hx_os_lexer_create_with_options(NULL,
		      hx_string_view_from_cstr("abc"), &options, &lexer,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_next(lexer, &token, NULL) == HX_OS_LIMIT);
	hx_os_lexer_destroy(&lexer);
	options.max_token_bytes = 8;
	options.max_tokens = 1;
	CHECK(hx_os_lexer_create_with_options(NULL,
		      hx_string_view_from_cstr("  x"), &options, &lexer,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_next(lexer, &token, NULL) == HX_OS_LIMIT);
	hx_os_lexer_destroy(&lexer);
	{
		test_memory memory = {0, 0, 0};
		hx_os_context *context = test_context(&memory);
		size_t base = memory.live, fail;
		for (fail = 1; fail <= 2; ++fail) {
			memory.fail = memory.calls + fail;
			CHECK(hx_os_lexer_create(context,
				      hx_string_view_from_cstr("abcdef"),
				      &lexer, NULL) == HX_OS_NO_MEMORY);
			CHECK(!lexer && memory.live == base);
		}
		memory.fail = 0;
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(!memory.live);
	}
	hx_string_destroy(&decoded);
	return 0;
}
