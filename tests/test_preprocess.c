#include "utility_test.h"

static void check_tokens(const hx_string *actual, const char *expected)
{
	hx_os_lexer_options options = HX_OS_LEXER_OPTIONS_INIT;
	hx_os_lexer *left = NULL, *right = NULL;

	options.flags = HX_OS_LEXER_C_COMMENTS | HX_OS_LEXER_SKIP_NEWLINES;
	CHECK(hx_os_lexer_create_with_options(NULL, hx_string_view_of(actual),
		      &options, &left, NULL) == HX_OS_OK);
	CHECK(hx_os_lexer_create_with_options(NULL,
		      hx_string_view_from_cstr(expected), &options, &right,
		      NULL) == HX_OS_OK);
	for (;;) {
		hx_os_token a, b;
		hx_os_status first = hx_os_lexer_next(left, &a, NULL);
		hx_os_status second = hx_os_lexer_next(right, &b, NULL);

		CHECK(first == second);
		if (first == HX_OS_EOF)
			break;
		CHECK(first == HX_OS_OK && a.kind == b.kind &&
			a.text.size == b.text.size);
		CHECK(!memcmp(a.text.data, b.text.data, a.text.size));
	}
	hx_os_lexer_destroy(&left);
	hx_os_lexer_destroy(&right);
}

static void check_text(const char *input, const char *expected)
{
	hx_string output = HX_STRING_INIT;
	hx_os_preprocess_diagnostic diagnostic =
		HX_OS_PREPROCESS_DIAGNOSTIC_INIT;
	hx_os_error error;
	hx_os_status status =
		hx_os_preprocess_text_ex(hx_string_view_from_cstr(input),
			"case.cfg", NULL, &output, &diagnostic, &error);

	if (status != HX_OS_OK)
		fprintf(stderr, "%s: %s at %zu:%zu (%s)\n", input,
			hx_os_status_name(status), diagnostic.line,
			diagnostic.column,
			diagnostic.description ? diagnostic.description : "");
	CHECK(status == HX_OS_OK && error.status == HX_OS_OK);
	CHECK(!diagnostic.description && !diagnostic.source.size);
	check_tokens(&output, expected);
	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);
}

static void check_failure(const char *input, hx_os_status expected,
	const hx_os_preprocess_options *options)
{
	hx_string output = HX_STRING_INIT;
	hx_os_preprocess_diagnostic diagnostic =
		HX_OS_PREPROCESS_DIAGNOSTIC_INIT;
	hx_os_error error;
	hx_os_status status;

	CHECK(hx_string_assign_cstr(&output, "unchanged", NULL) == HX_OS_OK);
	status = hx_os_preprocess_text_ex(hx_string_view_from_cstr(input),
		"failure.cfg", options, &output, &diagnostic, &error);
	if (status != expected)
		fprintf(stderr, "input %s: expected %s, got %s (%s)\n", input,
			hx_os_status_name(expected), hx_os_status_name(status),
			diagnostic.description ? diagnostic.description : "");
	CHECK(status == expected && error.status == expected);
	CHECK(!strcmp(output.data, "unchanged"));
	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);
}

static void test_language(void)
{
	check_text(
		"#define A B\n#define B C\n#define C D\n#define D 17\nA", "17");
	check_text("#define D1 D2\n#define D2 defined\n#define X 1\n#if "
		   "D1(X)\nok\n#endif",
		"ok");
	check_text("", "");
	check_text("plain \"NAME\" /* NAME */", "plain \"NAME\"");
	check_text("\xef\xbb\xbf#define 名称 \"相机😀\"\nname = 名称;",
		"name = \"相机😀\";");
	check_text("#define A 12\n#define B A\nB \"B\" 'B' // B\n",
		"12 \"B\" 'B'");
	check_text("#define X 1\n#define X 2\nX\n#undef X\nX", "2 X");
	check_text("#undef missing\n#\n// empty", "");
	check_text("#define X\n#ifdef X\npresent\n#endif\n", "present");
	check_text("#ifndef NO_VALUE\ntrue_branch\n#else\nwrong\n#endif",
		"true_branch");
	check_text("#define LEVEL 3\n#if defined(LEVEL) && LEVEL >= "
		   "2\nok\n#else\nwrong\n#endif",
		"ok");
	check_text("#if defined missing\nwrong\n#elif true && "
		   "!false\nok\n#else\nwrong\n#endif",
		"ok");
	check_text(
		"#define X 1 || 0\n#if X && 0\nok\n#else\nwrong\n#endif", "ok");
	check_text("#if 2 == 1 < 1\nwrong\n#else\nok\n#endif", "ok");
	check_text("#if -9223372036854775808 < 0 && +0x7f == 127\nok\n#endif",
		"ok");
	check_text("#if 0\n#include \"not-read\"\n#unsupported ignored\n#if "
		   "nonsense ? text\nwrong\n#endif\n#else\nok\n#endif",
		"ok");
	check_text("/* leading */ #define VALUE 9\nVALUE", "9");
	check_text("text /* comment\nspans a line */ #define X 1\nX", "text 1");
	check_text("a #define X 1\nX", "a #define X 1 X");
	check_text("#define SLASH /\nSLASH* text */", "/ * text * /");
	check_text(
		"#define D defined\n#define X 1\n#if D(X)\nok\n#endif", "ok");
	check_failure("#define A A\nA", HX_OS_SYNTAX, NULL);
	check_failure("#define A B\n#define B A\nA", HX_OS_SYNTAX, NULL);
	check_failure("#define F(x) x\n", HX_OS_NOT_SUPPORTED, NULL);
	check_failure("#define X a ## b\n", HX_OS_NOT_SUPPORTED, NULL);
	check_failure("#define X a \\\nb\n", HX_OS_NOT_SUPPORTED, NULL);
	check_failure("#if 1 + 2\nx\n#endif", HX_OS_SYNTAX, NULL);
	check_failure("#if 1.5\nx\n#endif", HX_OS_SYNTAX, NULL);
	check_failure("#if 1 & & 1\nx\n#endif", HX_OS_SYNTAX, NULL);
	check_failure("#if 9223372036854775808\nx\n#endif", HX_OS_RANGE, NULL);
	check_failure(
		"#if -(-9223372036854775808)\nx\n#endif", HX_OS_RANGE, NULL);
	check_failure("#if defined()\nx\n#endif", HX_OS_SYNTAX, NULL);
	check_failure("#else\n", HX_OS_SYNTAX, NULL);
	check_failure("#if 1\nx", HX_OS_TRUNCATED_INPUT, NULL);
	check_failure("#if 0\n#else\n#else\n#endif\n", HX_OS_SYNTAX, NULL);
	check_failure("#endif extra\n", HX_OS_SYNTAX, NULL);
	check_failure("#define true 0\n", HX_OS_SYNTAX, NULL);
	check_failure("#\xef\xbb\xbf"
		      "define X 1\n",
		HX_OS_SYNTAX, NULL);
}

typedef struct include_state {
	size_t calls;
	int mode;
} include_state;

static hx_os_status HX_OS_CALL resolve(const char *parent,
	const char *requested, size_t maximum, hx_string *text,
	hx_string *resolved, void *user, hx_os_error *error)
{
	include_state *state = user;
	const char *content = "#define VALUE 7\nVALUE";
	char nested[64];

	CHECK(parent && requested);
	++state->calls;
	if (state->mode == 1) {
		error->status = HX_OS_ACCESS_DENIED;
		error->domain = HX_OS_ERROR_ERRNO;
		error->native_code = 13;
		error->cause_code = 99;

		return HX_OS_ACCESS_DENIED;
	}
	if (state->mode == 2)
		content = "#include \"part\"";
	if (state->mode == 3)
		content = "#if 1\nunclosed";
	if (state->mode == 4)
		content = "";
	if (state->mode == 6) {
		int level = requested[4] - '0';

		if (level < 5)
			(void)snprintf(nested, sizeof(nested),
				"#include \"part%d\"\n", level + 1);
		else
			(void)snprintf(nested, sizeof(nested), "deep");
		content = nested;
	}
	if (strlen(content) > maximum)
		return HX_OS_LIMIT;
	if (state->mode == 5) {
		hx_string_view bad = {"bad\0id", 6};

		CHECK(hx_string_assign(resolved, bad, NULL) == HX_OS_OK);
	}

	return hx_string_assign_cstr(text, content, error);
}

static void test_includes(void)
{
	hx_os_preprocess_options options = HX_OS_PREPROCESS_OPTIONS_INIT;
	hx_os_preprocess_diagnostic diagnostic =
		HX_OS_PREPROCESS_DIAGNOSTIC_INIT;
	include_state state = {0};
	hx_string output = HX_STRING_INIT;
	hx_os_error error;
	const char *input = "#include \"part\"\nVALUE";

	options.include = resolve;
	options.user = &state;
	CHECK(hx_os_preprocess_text_ex(hx_string_view_from_cstr(input), "root",
		      &options, &output, &diagnostic, &error) == HX_OS_OK);
	check_tokens(&output, "7 7");
	CHECK(state.calls == 1);
	CHECK(hx_os_preprocess_text("#include \"part\"", &output, &error) ==
		HX_OS_NOT_SUPPORTED);
	options.flags = HX_OS_PREPROCESS_NO_INCLUDES;
	check_failure(input, HX_OS_NOT_SUPPORTED, &options);
	CHECK(state.calls == 1);
	options.flags = 0;
	state.mode = 1;
	CHECK(hx_os_preprocess_text_ex(hx_string_view_from_cstr(input), "root",
		      &options, &output, &diagnostic,
		      &error) == HX_OS_ACCESS_DENIED);
	CHECK(error.domain == HX_OS_ERROR_ERRNO && error.native_code == 13 &&
		error.cause_code == 99);
	CHECK(!strcmp(diagnostic.source.data, "root") && diagnostic.line == 1);
	state.mode = 2;
	check_failure(input, HX_OS_SYNTAX, &options);
	state.mode = 3;
	check_failure(input, HX_OS_TRUNCATED_INPUT, &options);
	state.mode = 5;
	check_failure(input, HX_OS_INVALID, &options);
	state.mode = 0;
	options.max_include_depth = 1;
	check_failure(input, HX_OS_LIMIT, &options);
	options.max_include_depth = 16;
	options.max_total_input_bytes = strlen(input);
	state.mode = 4;
	CHECK(hx_os_preprocess_text_ex(hx_string_view_from_cstr(input), "root",
		      &options, &output, &diagnostic, &error) == HX_OS_OK);
	check_tokens(&output, "VALUE");
	options.max_total_input_bytes = 64u * 1024u * 1024u;
	state.mode = 6;
	CHECK(hx_os_preprocess_text_ex(
		      hx_string_view_from_cstr("#include \"part1\"\n"), "root",
		      &options, &output, &diagnostic, &error) == HX_OS_OK);
	check_tokens(&output, "deep");
	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);
}

static void test_budgets(void)
{
	hx_os_preprocess_options options = HX_OS_PREPROCESS_OPTIONS_INIT;
	hx_os_preprocess_define define = {"ANSWER", {"42", 2}};
	hx_string output = HX_STRING_INIT;
	char expression[256];

	options.defines = &define;
	options.define_count = 1;
	CHECK(hx_os_preprocess_text_ex(hx_string_view_from_cstr("ANSWER"), NULL,
		      &options, &output, NULL, NULL) == HX_OS_OK);
	check_tokens(&output, "42");
	options.max_output_bytes = 1;
	check_failure("ANSWER", HX_OS_LIMIT, &options);
	options.max_output_bytes = 1024;
	options.max_expansions = 1;
	check_failure("ANSWER ANSWER", HX_OS_LIMIT, &options);
	options.max_expansions = 100;
	options.max_expanded_bytes = 1;
	check_failure("ANSWER", HX_OS_LIMIT, &options);
	options.max_expanded_bytes = 1024;
	options.max_defines = 1;
	check_failure("#define SECOND 2\n", HX_OS_LIMIT, &options);
	options.max_directives = 1;
	check_failure("#undef missing\n#undef other", HX_OS_LIMIT, &options);
	options.max_directives = 100;
	options.max_condition_depth = 1;
	check_failure("#if 1\n#if 1\n#endif\n#endif", HX_OS_LIMIT, &options);
	options.max_condition_depth = 64;
	options.max_expansion_depth = 1;
	options.define_count = 0;
	options.max_defines = 10;
	check_failure("#define A B\n#define B 1\nA", HX_OS_LIMIT, &options);
	options.max_input_bytes = 1;
	check_failure("too long", HX_OS_LIMIT, &options);
	memset(expression, 0, sizeof(expression));
	memcpy(expression, "#if ", 4);
	memset(expression + 4, '!', 66);
	memcpy(expression + 70, "1\nx\n#endif", 11);
	check_failure(expression, HX_OS_LIMIT, NULL);
	hx_string_destroy(&output);
}

static void test_allocation(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_string output;
	hx_os_preprocess_diagnostic diagnostic;
	const char *input = "#define A 7\n#if A > 2\nA A\n#endif";
	size_t fail, base;
	int succeeded = 0;

	hx_string_init(&output, context);
	hx_os_preprocess_diagnostic_init(&diagnostic, context);
	CHECK(hx_string_assign_cstr(&output, "keep", NULL) == HX_OS_OK);
	base = memory.live;
	for (fail = 1; fail < 128; ++fail) {
		hx_os_status status;

		memory.fail = memory.calls + fail;
		status = hx_os_preprocess_text_ex(
			hx_string_view_from_cstr(input), "oom.cfg", NULL,
			&output, &diagnostic, NULL);
		memory.fail = 0;
		if (status == HX_OS_OK) {
			succeeded = 1;
			break;
		}
		CHECK(status == HX_OS_NO_MEMORY &&
			!strcmp(output.data, "keep"));
		hx_os_preprocess_diagnostic_destroy(&diagnostic);
		hx_os_preprocess_diagnostic_init(&diagnostic, context);
		CHECK(memory.live == base);
	}
	CHECK(succeeded);
	check_tokens(&output, "7 7");
	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}

static void test_files(void)
{
	char directory[96], subdirectory[128], root[128], part[160];
	const char *input = "#include \"目录/片段.cfg\"\nVALUE";
	const char *included = "\xef\xbb\xbf#define VALUE 11\nVALUE";
	hx_string output = HX_STRING_INIT;
	hx_os_preprocess_options options = HX_OS_PREPROCESS_OPTIONS_INIT;
	hx_os_preprocess_diagnostic diagnostic =
		HX_OS_PREPROCESS_DIAGNOSTIC_INIT;

	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_UTILS)) {
		CHECK(hx_os_preprocess_file("unused.cfg", &output, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return;
	}
	(void)snprintf(directory, sizeof(directory), "hx-preprocess-%llx",
		(unsigned long long)hx_os_monotonic_ns());
	(void)snprintf(
		subdirectory, sizeof(subdirectory), "%s/目录", directory);
	(void)snprintf(root, sizeof(root), "%s/main.cfg", directory);
	(void)snprintf(part, sizeof(part), "%s/片段.cfg", subdirectory);
	CHECK(hx_os_directory_create(directory, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_create(subdirectory, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_save(root, input, strlen(input), 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_save(part, included, strlen(included), 0, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_preprocess_file(root, &output, NULL) == HX_OS_OK);
	check_tokens(&output, "11 11");
	options.flags = HX_OS_PREPROCESS_NO_INCLUDES;
	CHECK(hx_os_preprocess_file_ex(root, &options, &output, &diagnostic,
		      NULL) == HX_OS_NOT_SUPPORTED);
	check_tokens(&output, "11 11");
	input = "#include \"../escape.cfg\"";
	CHECK(hx_os_file_save(root, input, strlen(input), 1, NULL) == HX_OS_OK);
	CHECK(hx_os_preprocess_file_ex(
		      root, NULL, &output, &diagnostic, NULL) == HX_OS_INVALID);
	CHECK(diagnostic.line == 1 && diagnostic.description);
	input = "#include \"main.cfg\"";
	CHECK(hx_os_file_save(root, input, strlen(input), 1, NULL) == HX_OS_OK);
	CHECK(hx_os_preprocess_file_ex(
		      root, NULL, &output, &diagnostic, NULL) == HX_OS_SYNTAX);
	CHECK(hx_os_file_remove(root, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(part, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(subdirectory, NULL) == HX_OS_OK);
	CHECK(hx_os_directory_remove(directory, NULL) == HX_OS_OK);
	hx_string_destroy(&output);
	hx_os_preprocess_diagnostic_destroy(&diagnostic);
}

static void test_expressions(void)
{
	char input[256];
	int a, b, c;

	for (a = -2; a <= 2; ++a)
		for (b = -2; b <= 2; ++b)
			for (c = -2; c <= 2; ++c) {
				int expected = (a == (b < c)) || (a && b);

				(void)snprintf(input, sizeof(input),
					"#if %d == %d < %d || (%d && %d)\n"
					"yes\n#else\nno\n#endif",
					a, b, c, a, b);
				check_text(input, expected ? "yes" : "no");
			}
}

static void *process_thread(void *user)
{
	const char *value = user;
	char input[128];
	size_t i;

	(void)snprintf(input, sizeof(input), "#define VALUE %s\nVALUE", value);
	for (i = 0; i < 50; ++i)
		check_text(input, value);

	return NULL;
}

static void test_threads(void)
{
	const char *values[] = {"101", "202", "303", "404"};
	hx_os_thread *threads[4] = {NULL};
	size_t i;

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, process_thread,
			      (void *)values[i], &threads[i],
			      NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
}

int main(void)
{
	if (!hx_os_preprocess_available()) {
		hx_string output = HX_STRING_INIT;

		CHECK(hx_os_preprocess_text("input", &output, NULL) ==
			HX_OS_NOT_SUPPORTED);
		hx_string_destroy(&output);

		return 0;
	}
	test_language();
	test_includes();
	test_budgets();
	test_allocation();
	test_files();
	test_expressions();
	test_threads();

	return 0;
}
