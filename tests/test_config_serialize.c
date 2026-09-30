#include <hx_os.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static void test_numbers(void)
{
	static const double values[] = {
		0.0, -0.0, 1.5, DBL_MAX, DBL_MIN, 1.2345678901234567, 1e-200};
	char buffer[128], small[2];
	double result = 123;
	size_t required, i;
	if (!(hx_os_capabilities() & HX_OS_CAP_TEXT_NUMBER)) {
		CHECK(hx_os_text_parse_double("1", 1, &result, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_text_format_double(1.0, buffer, sizeof(buffer),
			      &required) == HX_OS_NOT_SUPPORTED);
		return;
	}
	for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
		CHECK(hx_os_text_format_double(values[i], buffer,
			      sizeof(buffer), &required) == HX_OS_OK);
		CHECK(required == strlen(buffer) + 1);
		CHECK(hx_os_text_parse_double(
			      buffer, required - 1, &result, NULL) == HX_OS_OK);
		CHECK(result == values[i]);
		CHECK(!!signbit(result) == !!signbit(values[i]));
	}
	CHECK(hx_os_text_format_double(123.5, small, sizeof(small),
		      &required) == HX_OS_OUTPUT_FULL &&
		required == 6 && !small[0]);
	CHECK(hx_os_text_parse_double("0x10", 4, &result, NULL) == HX_OS_OK &&
		result == 16);
	CHECK(hx_os_text_parse_double("+1_000.5", 8, &result, NULL) ==
			HX_OS_OK &&
		result == 1000.5);
	CHECK(hx_os_text_parse_double("1e9999", 6, &result, NULL) ==
			HX_OS_RANGE &&
		result == 1000.5);
	CHECK(hx_os_text_parse_double(" 1", 2, &result, NULL) ==
		HX_OS_TYPE_MISMATCH);
	CHECK(hx_os_text_parse_double("1\0x", 3, &result, NULL) ==
		HX_OS_TYPE_MISMATCH);
	CHECK(hx_os_text_parse_double("-inf", 4, &result, NULL) == HX_OS_OK &&
		result < -DBL_MAX);
	CHECK(hx_os_text_format_double(
		      result, buffer, sizeof(buffer), &required) == HX_OS_OK);
	CHECK(!strcmp(buffer, "-inf"));
	CHECK(hx_os_text_parse_double("nan", 3, &result, NULL) == HX_OS_OK &&
		result != result);
	CHECK(hx_os_text_format_double(
		      result, buffer, sizeof(buffer), &required) == HX_OS_OK);
	CHECK(!strcmp(buffer, "nan"));
	{
		static const char *locales[] = {
			"de_DE.UTF-8", "fr_FR.UTF-8", "German_Germany.1252"};
		char previous_locale[256], previous_decimal[64];
		int changed = 0;
		for (i = 0; i < sizeof(locales) / sizeof(locales[0]); ++i)
			if (setlocale(LC_NUMERIC, locales[i])) {
				changed = 1;
				break;
			}
		(void)snprintf(previous_locale, sizeof(previous_locale), "%s",
			setlocale(LC_NUMERIC, NULL));
		(void)snprintf(previous_decimal, sizeof(previous_decimal), "%s",
			localeconv()->decimal_point);
		CHECK(hx_os_text_format_double(1.5, buffer, sizeof(buffer),
			      &required) == HX_OS_OK);
		CHECK(!strcmp(buffer, "1.5"));
		CHECK(hx_os_text_parse_double("1.5", 3, &result, NULL) ==
				HX_OS_OK &&
			result == 1.5);
		CHECK(!strcmp(previous_locale, setlocale(LC_NUMERIC, NULL)));
		CHECK(!strcmp(previous_decimal, localeconv()->decimal_point));
		if (!changed)
			puts("non-C numeric locale unavailable; "
			     "alternate-locale run pending");
		CHECK(setlocale(LC_NUMERIC, "C") != NULL);
	}
}

static hx_os_config_document *parse(const char *text, int format)
{
	hx_os_config_document *document = NULL;
	hx_os_config_error error;
	hx_os_status status = hx_os_config_parse(
		NULL, format, text, strlen(text), NULL, &document, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "parse status=%d offset=%zu message=%s\n",
			(int)status, error.byte_offset, error.message);
	CHECK(status == HX_OS_OK);
	return document;
}

static void compare_nodes(
	const hx_os_config_node *a, const hx_os_config_node *b)
{
	int kind = hx_os_config_kind(a);
	const hx_os_config_node *child;
	size_t index = 0;
	CHECK(kind == hx_os_config_kind(b));
	CHECK(hx_os_config_count(a) == hx_os_config_count(b));
	if (kind == HX_OS_CONFIG_OBJECT || kind == HX_OS_CONFIG_ARRAY) {
		for (child = hx_os_config_first(a); child;
			child = hx_os_config_next(child)) {
			const hx_os_config_node *other;
			if (kind == HX_OS_CONFIG_OBJECT) {
				hx_os_config_slice name =
					hx_os_config_name(child);
				CHECK(hx_os_config_member(b, name.data,
					      name.bytes, &other) == HX_OS_OK);
			}
			else
				other = hx_os_config_at(b, index++);
			compare_nodes(child, other);
		}
	}
	else {
		hx_os_config_slice first, second;
		CHECK(hx_os_config_scalar_text(a, &first) == HX_OS_OK);
		CHECK(hx_os_config_scalar_text(b, &second) == HX_OS_OK);
		CHECK(first.bytes == second.bytes &&
			!memcmp(first.data, second.data, first.bytes));
	}
}

static void round_trip(const char *source, int from, int to)
{
	hx_os_config_document *a = parse(source, from), *b = NULL;
	hx_string output = HX_STRING_INIT;
	CHECK(hx_os_config_serialize(a, to, &output, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_config_parse(NULL, to, output.data, output.size, NULL, &b,
		      NULL) == HX_OS_OK);
	compare_nodes(hx_os_config_root(a), hx_os_config_root(b));
	hx_os_config_release(b);
	hx_os_config_release(a);
	hx_string_destroy(&output);
}

static void expect_unrepresentable(const char *source, int format)
{
	hx_os_config_document *document = parse(source, HX_OS_CONFIG_JSON);
	hx_string output = HX_STRING_INIT;
	CHECK(hx_string_assign_cstr(&output, "kept", NULL) == HX_OS_OK);
	CHECK(hx_os_config_serialize(document, format, &output, NULL, NULL) ==
		HX_OS_UNREPRESENTABLE);
	CHECK(!strcmp(output.data, "kept"));
	hx_string_destroy(&output);
	hx_os_config_release(document);
}

static void *fail_allocate(size_t bytes, void *user)
{
	return *(int *)user ? NULL : malloc(bytes);
}
static void fail_free(void *memory, void *user)
{
	(void)user;
	free(memory);
}

static void *serialize_worker(void *user)
{
	hx_string text = HX_STRING_INIT;
	int i;
	for (i = 0; i < 100; ++i)
		CHECK(hx_os_config_serialize(user, HX_OS_CONFIG_JSON, &text,
			      NULL, NULL) == HX_OS_OK);
	hx_string_destroy(&text);
	return NULL;
}

static void test_serialization(void)
{
	const char *json = "{\"n\":9007199254740993,\"real\":1.2300e+09,"
			   "\"array\":[true,false,null,\"中文\\u0000text\"]}";
	hx_os_config_document *document, *loaded = NULL;
	hx_os_config_options options;
	hx_os_context_options context_options;
	hx_os_context *context = NULL;
	hx_string output = HX_STRING_INIT;
	hx_os_thread *threads[2];
	int fail = 0, i;
	if (!(hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE)) {
		CHECK(hx_os_config_serialize(NULL, HX_OS_CONFIG_JSON, &output,
			      NULL, NULL) == HX_OS_NOT_SUPPORTED);
		return;
	}
	round_trip(json, HX_OS_CONFIG_JSON, HX_OS_CONFIG_JSON);
	round_trip("global=x\n[s]\nname=中文\nempty=\n", HX_OS_CONFIG_INI,
		HX_OS_CONFIG_INI);
	round_trip("{\"section\":{\"x\":\"y\"},\"global\":\"z\"}",
		HX_OS_CONFIG_JSON, HX_OS_CONFIG_INI);
	round_trip("{\" #:=!\":\" \\n\\r\\t\\u0000\\\\中文 \"}",
		HX_OS_CONFIG_JSON, HX_OS_CONFIG_PROPERTIES);
	round_trip("{}", HX_OS_CONFIG_JSON, HX_OS_CONFIG_PROPERTIES);
	expect_unrepresentable("{\"value\":123}", HX_OS_CONFIG_INI);
	expect_unrepresentable("{\"value\":[]}", HX_OS_CONFIG_PROPERTIES);
	expect_unrepresentable("{\"value\":\"a ;comment\"}", HX_OS_CONFIG_INI);
	expect_unrepresentable("{\" value\":\"x\"}", HX_OS_CONFIG_INI);
	expect_unrepresentable(
		"{\"section\":{\"nested\":{}}}", HX_OS_CONFIG_INI);
	document = parse("a=0x10\nb=+1_234.50\n", HX_OS_CONFIG_TOML);
	CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_JSON, &output, NULL,
		      NULL) == HX_OS_OK);
	CHECK(!strcmp(output.data, "{\"a\":16,\"b\":1234.50}"));
	hx_os_config_release(document);
	document = parse("a=nan\n", HX_OS_CONFIG_TOML);
	CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_JSON, &output, NULL,
		      NULL) == HX_OS_UNREPRESENTABLE);
	hx_os_config_release(document);
	document = parse(json, HX_OS_CONFIG_JSON);
	CHECK(hx_string_assign_cstr(&output, "kept", NULL) == HX_OS_OK);
	for (i = 0; i < 5; ++i) {
		hx_os_config_options_init(&options);
		if (i == 0)
			options.max_input_bytes = 1;
		if (i == 1)
			options.max_memory_bytes = 1;
		if (i == 2)
			options.max_nodes = 1;
		if (i == 3)
			options.max_depth = 1;
		if (i == 4)
			options.max_work = 1;
		CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_JSON,
			      &output, &options, NULL) == HX_OS_LIMIT);
		CHECK(!strcmp(output.data, "kept"));
	}
	CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_YAML, &output, NULL,
		      NULL) == HX_OS_NOT_SUPPORTED);
	hx_string_destroy(&output);
	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = fail_allocate;
	context_options.allocator.deallocate = fail_free;
	context_options.allocator.user = &fail;
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	hx_string_init(&output, context);
	CHECK(hx_string_assign_cstr(&output, "kept", NULL) == HX_OS_OK);
	fail = 1;
	CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_JSON, &output, NULL,
		      NULL) == HX_OS_NO_MEMORY);
	CHECK(!strcmp(output.data, "kept"));
	fail = 0;
	hx_string_destroy(&output);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	for (i = 0; i < 2; ++i)
		CHECK(hx_os_thread_create(NULL, serialize_worker, document,
			      &threads[i], NULL) == HX_OS_OK);
	for (i = 0; i < 2; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(3000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
	if (hx_os_capabilities() & HX_OS_CAP_FILE_UTILS) {
		char path[96];
		(void)snprintf(path, sizeof(path), "hx-config-save-%llx.json",
			(unsigned long long)hx_os_monotonic_ns());
		CHECK(hx_os_config_save(document, HX_OS_CONFIG_JSON, path, NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_config_load(NULL, HX_OS_CONFIG_JSON, path, NULL,
			      &loaded, NULL) == HX_OS_OK);
		compare_nodes(
			hx_os_config_root(document), hx_os_config_root(loaded));
		hx_os_config_release(loaded);
		CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	}
	else
		CHECK(hx_os_config_save(document, HX_OS_CONFIG_JSON,
			      "unused.json", NULL,
			      NULL) == HX_OS_NOT_SUPPORTED);
	hx_os_config_release(document);
}

int main(void)
{
	test_numbers();
	test_serialization();
	puts("configuration serialization and numeric text contracts passed");
	return 0;
}
