#include <hx_os.h>
#include <stdio.h>

static hx_os_status read_title(hx_os_lexer *lexer, hx_string *title)
{
	hx_os_status status = hx_os_lexer_expect(lexer, "title", NULL, NULL);

	if (status == HX_OS_OK)
		status = hx_os_lexer_expect(lexer, "=", NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_lexer_read_string(lexer, title, NULL);
	if (status == HX_OS_OK)
		status = hx_os_lexer_expect(lexer, ";", NULL, NULL);

	return status;
}

int main(void)
{
	const char *input =
		"title = \"Camera\";\n"
		"extensions { nested [call(\"}\")]; /* ignored */ }";
	hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;
	hx_os_lexer *lexer = NULL;
	hx_string title = HX_STRING_INIT;
	hx_os_status status;

	options.flags |= HX_OS_LEXER_SKIP_NEWLINES;
	status = hx_os_lexer_create_with_options(
		NULL, hx_string_view_from_cstr(input), &options, &lexer, NULL);
	if (status == HX_OS_OK)
		status = read_title(lexer, &title);

	// Skip an extension block without interpreting its contents.
	if (status == HX_OS_OK)
		status = hx_os_lexer_expect(lexer, "extensions", NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_lexer_skip_group(lexer, NULL);
	if (status == HX_OS_OK)
		status = hx_os_lexer_expect_kind(
			lexer, HX_OS_TOKEN_END, NULL, NULL);

	if (status == HX_OS_OK)
		printf("Title: %s\n", hx_string_c_str(&title));
	else
		fprintf(stderr, "Parsing failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&title);
	hx_os_lexer_destroy(&lexer);

	return status == HX_OS_OK ? 0 : 1;
}
