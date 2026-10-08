#include <hx_print.h>
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void test_format(const char *pattern, const hx_format_arg *args,
	size_t count, const char *expected)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status = hx_format(
		&text, hx_string_view_from_cstr(pattern), args, count, &error);
	if (status != HX_OS_OK || strcmp(hx_string_c_str(&text), expected)) {
		fprintf(stderr, "%s: %s; got [%s], expected [%s]\n", pattern,
			hx_os_status_name(status), hx_string_c_str(&text),
			expected);
		exit(1);
	}

	hx_string_destroy(&text);
}

static hx_os_status test_custom(const void *object, hx_string_view spec,
	hx_string *out, size_t limit, hx_os_error *error)
{
	const int *point = object;

	hx_format_arg args[] = {hx_fmt_i64(point[0]), hx_fmt_i64(point[1])};

	hx_format_options options = HX_FORMAT_OPTIONS_INIT;

	if (spec.size)
		return HX_OS_SYNTAX;

	options.max_output_bytes = limit;

	return hx_format_with_options(out, hx_string_view_from_cstr("({}, {})"),
		args, 2, &options, error);
}

static void test_convenience(void)
{
	hx_string text = HX_STRING_INIT;
	int point[] = {10, 20};
	int evaluations = 0;
	char repeated[3 * 80 + 1];
	size_t i;

	CHECK(HX_FORMAT(&text, "literal {{}}") == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "literal {}"));
	CHECK(HX_FORMAT(&text, "{0}/{0}", hx_fmt_i64(++evaluations)) ==
		HX_OS_OK);
	CHECK(evaluations == 1 && !strcmp(hx_string_c_str(&text), "1/1"));
	CHECK(HX_FORMAT(&text, "{name} {value:04d}",
		      HX_NAMED("name", hx_fmt_str("中文")),
		      HX_NAMED("value", hx_fmt_i64(20))) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "中文 0020"));
	CHECK(HX_FORMAT(&text, "{}", hx_fmt_custom(point, test_custom)) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "(10, 20)"));
	CHECK(HX_FORMAT(&text, "{}!", hx_fmt_view(hx_string_view_of(&text))) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "(10, 20)!"));
	CHECK(HX_FORMAT(&text, "{} {}", hx_fmt_i64(1)) == HX_OS_RANGE);
	CHECK(!strcmp(hx_string_c_str(&text), "(10, 20)!"));

#define TEST_EIGHT_VALUES                                                      \
	hx_fmt_i64(7), hx_fmt_i64(7), hx_fmt_i64(7), hx_fmt_i64(7),            \
		hx_fmt_i64(7), hx_fmt_i64(7), hx_fmt_i64(7), hx_fmt_i64(7)
	CHECK(HX_FORMAT(&text, "{63}", TEST_EIGHT_VALUES, TEST_EIGHT_VALUES,
		      TEST_EIGHT_VALUES, TEST_EIGHT_VALUES, TEST_EIGHT_VALUES,
		      TEST_EIGHT_VALUES, TEST_EIGHT_VALUES,
		      TEST_EIGHT_VALUES) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "7"));
#undef TEST_EIGHT_VALUES

	for (i = 0; i < 80; ++i)
		memcpy(repeated + i * 3, "{0}", 3);
	repeated[sizeof(repeated) - 1] = 0;
	CHECK(HX_FORMAT(&text, repeated, hx_fmt_i64(7)) == HX_OS_OK);
	CHECK(text.size == 80 && strspn(hx_string_c_str(&text), "7") == 80);

	hx_string_destroy(&text);
}

typedef struct test_memory {
	size_t calls;
	size_t live;
	size_t fail;
} test_memory;

static void *test_allocate(size_t bytes, void *user)
{
	test_memory *memory = user;
	void *result;

	if (++memory->calls == memory->fail)
		return NULL;

	result = malloc(bytes);
	if (result)
		++memory->live;

	return result;
}

static void test_free(void *pointer, void *user)
{
	test_memory *memory = user;

	CHECK(memory->live);

	--memory->live;

	free(pointer);
}

static void test_failures(void)
{
	size_t fail;

	for (fail = 1; fail < 64; ++fail) {
		test_memory memory = {0};
		hx_os_context_options context_options;
		hx_os_context *context;
		hx_string text;

		hx_format_arg args[] = {hx_fmt_str("中文"), hx_fmt_f64(1.25)};

		hx_os_status status;

		hx_os_context_options_init(&context_options);
		context_options.allocator =
			(hx_os_allocator){test_allocate, test_free, &memory};
		CHECK(hx_os_context_create(&context_options, &context, NULL) ==
			HX_OS_OK);

		hx_string_init(&text, context);
		CHECK(hx_string_assign_cstr(&text, "original", NULL) ==
			HX_OS_OK);

		memory.calls = 0;
		memory.fail = fail;

		status = hx_format(&text,
			hx_string_view_from_cstr("{:^10} {:.3f}"), args, 2,
			NULL);
		if (status != HX_OS_OK) {
			CHECK(status == HX_OS_NO_MEMORY);
			CHECK(!strcmp(hx_string_c_str(&text), "original"));
		}

		hx_string_destroy(&text);
		CHECK(hx_os_context_allocations(context) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(!memory.live);
		if (status == HX_OS_OK)
			break;
	}

	CHECK(fail < 64);
}

int main(int argc, char **argv)
{
	hx_format_arg values[] = {hx_fmt_i64(-42), hx_fmt_u64(UINT64_MAX),
		hx_fmt_str("中文🙂"), hx_fmt_f64(3.14159), hx_fmt_bool(1)};

	hx_format_arg named[] = {hx_fmt_named("name", hx_fmt_str("张三")),
		hx_fmt_named("width", hx_fmt_i64(6)),
		hx_fmt_named("n", hx_fmt_i64(255))};

	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_format_options options = HX_FORMAT_OPTIONS_INIT;
	char buffer[8] = "keep";
	size_t required = 0;
	int point[] = {10, 20};

	hx_format_arg custom = hx_fmt_custom(point, test_custom);
	if (!(hx_os_capabilities() & HX_OS_CAP_FORMAT)) {
		CHECK(hx_format(&text, hx_string_view_from_cstr(""), NULL, 0,
			      NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	if (argc > 1 && !strcmp(argv[1], "--print"))
		return HX_PRINTLN("{} {:.2f}", hx_fmt_str("中文🙂"),
			       hx_fmt_f64(1.25)) != HX_OS_OK;

	test_convenience();

	test_format("{} {} {} {:.2f} {}", values, 5,
		"-42 18446744073709551615 中文🙂 3.14 true");
	test_format("{{{0:+08d}}} {1:#x}", values, 5,
		"{-0000042} 0xffffffffffffffff");
	test_format("{2:*^7.2s}", values, 5, "**中文***");
	test_format("{name:>{width}} {n:#06x}", named, 3, "    张三 0x00ff");
	test_format("{name!r}", named, 3, "\"张三\"");
	{
		const char raw[] = "prefix中文\0middle\"\\\x7f"
				   "tail";
		hx_string_view view = {raw, sizeof(raw) - 1};
		hx_format_arg quoted = hx_fmt_view(view);

		test_format("{:?}", &quoted, 1,
			"\"prefix中文\\u0000middle\\\"\\\\\\u007ftail\"");
	}
	test_format("{0}", &custom, 1, "(10, 20)");
	test_format("{4:s} {4:?}", values, 5, "true \"true\"");
	{
		const char bad_spec[] = "{0:\0}";
		hx_string_view pattern = {bad_spec, sizeof(bad_spec) - 1};

		CHECK(hx_format(&text, pattern, values, 5, NULL) ==
			HX_OS_SYNTAX);
	}

	{
		hx_format_arg args[] = {hx_fmt_i64(INT64_MIN),
			hx_fmt_i64(1234567), hx_fmt_f32(0.1f), hx_fmt_f64(0.1),
			hx_fmt_f64(-0.0), hx_fmt_i64(5)};
		test_format("{} {:,d} {} {} {} {:08b}", args, 6,
			"-9223372036854775808 1,234,567 0.1 0.1 -0 00000101");
	}

	CHECK(hx_string_assign_cstr(&text, "unchanged", NULL) == HX_OS_OK);
	CHECK(hx_format(&text, hx_string_view_from_cstr("{} {0}"), values, 5,
		      &error) == HX_OS_SYNTAX);
	CHECK(error.cause_code == 3);
	CHECK(!strcmp(hx_string_c_str(&text), "unchanged"));
	CHECK(hx_format(&text, hx_string_view_from_cstr("{2:d}"), values, 5,
		      NULL) == HX_OS_TYPE_MISMATCH);
	CHECK(hx_format(&text, hx_string_view_from_cstr("{"), values, 5,
		      NULL) == HX_OS_SYNTAX);
	CHECK(hx_format(&text, hx_string_view_from_cstr("{99}"), values, 5,
		      NULL) == HX_OS_RANGE);

	options.max_output_bytes = 4;
	CHECK(hx_format_with_options(&text, hx_string_view_from_cstr("{2}"),
		      values, 5, &options, NULL) == HX_OS_LIMIT);
	CHECK(hx_format_buffer(buffer, sizeof(buffer), &required,
		      hx_string_view_from_cstr("{2}"), values, 5,
		      NULL) == HX_OS_OUTPUT_FULL);
	CHECK(required == 11 && !strcmp(buffer, "keep"));

	values[0] = hx_fmt_view(hx_string_view_of(&text));
	CHECK(hx_format(&text, hx_string_view_from_cstr("{}!"), values, 1,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "unchanged!"));

	hx_string_destroy(&text);
	test_failures();
	puts("typed formatting, UTF-8 and failure retention passed");

	return 0;
}
