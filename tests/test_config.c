#include "hx_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at line %d: %s\n",       \
				__LINE__, #value);                             \
			exit(1);                                               \
		}                                                              \
	} while (0)

static hx_os_config_document *test_parse(
	hx_os_context *context, int format, int backend, const char *text)
{
	hx_os_config_options options;
	hx_os_config_error error;
	hx_os_config_document *doc = NULL;
	hx_os_status status;
	hx_os_config_options_init(&options);
	options.backend = backend;
	status = hx_os_config_parse(
		context, format, text, strlen(text), &options, &doc, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s: %s at %zu:%zu\n",
			hx_os_config_format_name(format), error.message,
			error.line, error.column);
	CHECK(status == HX_OS_OK && doc);
	return doc;
}

static const hx_os_config_node *test_pointer(
	const hx_os_config_document *doc, const char *pointer)
{
	const hx_os_config_node *node;
	CHECK(hx_os_config_pointer(hx_os_config_root(doc), pointer, &node) ==
		HX_OS_OK);
	return node;
}

static void test_text(const hx_os_config_node *node, const char *text)
{
	hx_os_config_slice value;
	CHECK(hx_os_config_get_string(node, &value) == HX_OS_OK);
	CHECK(value.bytes == strlen(text));
	CHECK(!memcmp(value.data, text, value.bytes));
}

static void test_number(const hx_os_config_node *node, int64_t expected)
{
	int64_t value = 0;
	CHECK(hx_os_config_get_i64(node, &value) == HX_OS_OK);
	CHECK(value == expected);
}

static void test_json(hx_os_context *context, int backend)
{
	hx_os_config_document *doc =
		test_parse(context, HX_OS_CONFIG_JSON, backend,
			"{\"中文\":[\"\\uD83D\\uDE00\",true,null],"
			"\"n\":9007199254740993,\"min\":-9223372036854775808,"
			"\"u\":18446744073709551615,\"real\":1.25e2,"
			"\"a/b\":{\"~\":\"x\\u0000y\"}}");
	const hx_os_config_node *node;
	hx_os_config_slice value;
	uint64_t number;
	int64_t signed_number;
	double real;
	int boolean;
	test_text(test_pointer(doc, "/中文/0"), "😀");
	CHECK(hx_os_config_get_bool(test_pointer(doc, "/中文/1"), &boolean) ==
			HX_OS_OK &&
		boolean);
	CHECK(hx_os_config_kind(test_pointer(doc, "/中文/2")) ==
		HX_OS_CONFIG_NULL);
	test_number(test_pointer(doc, "/n"), INT64_C(9007199254740993));
	test_number(test_pointer(doc, "/min"), INT64_MIN);
	node = test_pointer(doc, "/u");
	CHECK(hx_os_config_get_u64(node, &number) == HX_OS_OK &&
		number == UINT64_MAX);
	CHECK(hx_os_config_get_i64(node, &signed_number) == HX_OS_RANGE);
	if (hx_os_capabilities() & HX_OS_CAP_CONFIG_DOUBLE)
		CHECK(hx_os_config_get_double(
			      test_pointer(doc, "/real"), &real) == HX_OS_OK &&
			real == 125.0);
	else
		CHECK(hx_os_config_get_double(test_pointer(doc, "/real"),
			      &real) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_config_get_i64(test_pointer(doc, "/real"),
		      &signed_number) == HX_OS_TYPE_MISMATCH);
	CHECK(hx_os_config_get_string(test_pointer(doc, "/a~1b/~0"), &value) ==
			HX_OS_OK &&
		value.bytes == 3 && !memcmp(value.data, "x\0y", 3));
	CHECK(hx_os_config_pointer(hx_os_config_root(doc), "/中文/01", &node) ==
		HX_OS_INVALID);
	CHECK(hx_os_config_pointer(hx_os_config_root(doc), "/~2", &node) ==
		HX_OS_INVALID);
	CHECK(hx_os_config_pointer(hx_os_config_root(doc), "/missing", &node) ==
			HX_OS_NOT_FOUND &&
		!node);
	hx_os_config_retain(doc);
	hx_os_config_release(doc);
	CHECK(hx_os_config_root(doc));
	hx_os_config_release(doc);
}

static void test_formats(hx_os_context *context, int format, int backend)
{
	hx_os_config_document *doc;
	const hx_os_config_node *node;
	switch (format) {
	case HX_OS_CONFIG_JSON:
		test_json(context, backend);
		break;
	case HX_OS_CONFIG_YAML:
		doc = test_parse(context, format, backend,
			"---\nserver:\n  port: 8080\n  name: 中文\n"
			"items:\n  - name: first\n    value: 7\n"
			"  - name: second\n    value: 8\n"
			"enabled: true\nquoted: 'it''s ok'\n"
			"plain: it's ok # removed\n"
			"hash: 'keep # hash'\n...\n");
		test_number(test_pointer(doc, "/server/port"), 8080);
		test_text(test_pointer(doc, "/server/name"), "中文");
		test_number(test_pointer(doc, "/items/1/value"), 8);
		test_text(test_pointer(doc, "/quoted"), "it's ok");
		test_text(test_pointer(doc, "/plain"), "it's ok");
		test_text(test_pointer(doc, "/hash"), "keep # hash");
		hx_os_config_release(doc);
		break;
	case HX_OS_CONFIG_XML:
		doc = test_parse(context, format, backend,
			"<?xml version='1.0' encoding='UTF-8'?>"
			"<根 count='42' text='a&#10;b'>"
			"hello&lt;<子/><![CDATA[x&y]]></根>");
		node = hx_os_config_root(doc);
		CHECK(hx_os_config_attribute(node, "count", &node) == HX_OS_OK);
		test_number(node, 42);
		CHECK(hx_os_config_attribute(hx_os_config_root(doc), "text",
			      &node) == HX_OS_OK);
		test_text(node, "a\nb");
		CHECK(hx_os_config_count(hx_os_config_root(doc)) == 3);
		test_text(hx_os_config_at(hx_os_config_root(doc), 0), "hello<");
		CHECK(hx_os_config_kind(hx_os_config_at(hx_os_config_root(doc),
			      1)) == HX_OS_CONFIG_ELEMENT);
		test_text(hx_os_config_at(hx_os_config_root(doc), 2), "x&y");
		hx_os_config_release(doc);
		break;
	case HX_OS_CONFIG_INI:
		doc = test_parse(context, format, backend,
			"; header\n[server]\nport=8080 ; comment\nname=中文\n");
		test_number(test_pointer(doc, "/server/port"), 8080);
		test_text(test_pointer(doc, "/server/name"), "中文");
		hx_os_config_release(doc);
		break;
	case HX_OS_CONFIG_TOML:
		doc = test_parse(context, format, backend,
			"title = '中文'\n[server]\nport=8_080\n"
			"hex=0xff\nlimits={threads=4, queue=16}\n"
			"hosts=['one', # comment\n 'two',]\n");
		test_text(test_pointer(doc, "/title"), "中文");
		test_number(test_pointer(doc, "/server/port"), 8080);
		test_number(test_pointer(doc, "/server/hex"), 255);
		test_number(test_pointer(doc, "/server/limits/threads"), 4);
		test_text(test_pointer(doc, "/server/hosts/1"), "two");
		hx_os_config_release(doc);
		break;
	case HX_OS_CONFIG_ENV:
		doc = test_parse(context, format, backend,
			"export APP_NAME=\"中文\\nready\"\nPORT=8080 # "
			"comment\n"
			"LITERAL='$HOME'\nEMPTY=\n");
		test_text(test_pointer(doc, "/APP_NAME"), "中文\nready");
		test_number(test_pointer(doc, "/PORT"), 8080);
		test_text(test_pointer(doc, "/LITERAL"), "$HOME");
		test_text(test_pointer(doc, "/EMPTY"), "");
		hx_os_config_release(doc);
		break;
	case HX_OS_CONFIG_PROPERTIES:
		doc = test_parse(context, format, backend,
			"! comment\\\nname=\\u4e2d\\u6587\n"
			"a\\ key : first\\\r\n   second\n"
			"emoji=\\uD83D\\uDE00\nempty\nspace=value \n"
			"unknown=\\Uabcd\n");
		test_text(test_pointer(doc, "/name"), "中文");
		test_text(test_pointer(doc, "/a key"), "firstsecond");
		test_text(test_pointer(doc, "/emoji"), "😀");
		test_text(test_pointer(doc, "/empty"), "");
		test_text(test_pointer(doc, "/space"), "value ");
		test_text(test_pointer(doc, "/unknown"), "Uabcd");
		hx_os_config_release(doc);
		break;
	default:
		CHECK(0);
	}
}

static void test_bad(hx_os_context *context, int format, const char *text,
	hx_os_status expected)
{
	hx_os_config_document *doc = NULL;
	hx_os_config_error error;
	CHECK(hx_os_config_parse(context, format, text, strlen(text), NULL,
		      &doc, &error) == expected);
	CHECK(!doc && error.detail.status == expected);
	CHECK(error.line >= 1 && error.column >= 1);
	CHECK(error.byte_offset <= strlen(text));
}

static void test_failures(hx_os_context *context)
{
	hx_os_config_document *doc;
	hx_os_config_error error;
	hx_os_config_options options;
	const char *duplicate = "{\"a\":1,\"a\":2}";
	const char *bad_json[] = {"[1,]", "{\"a\":}", "true false", "01", "1.",
		"1e+", "\"\\uD800\"", "\"\\x20\"", "\"line\nbreak\""};
	size_t i;
	for (i = 0; i < sizeof(bad_json) / sizeof(bad_json[0]); ++i)
		test_bad(context, HX_OS_CONFIG_JSON, bad_json[i], HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_JSON, duplicate, HX_OS_EXISTS);
	test_bad(
		context, HX_OS_CONFIG_JSON, "\xc0\x80", HX_OS_INVALID_ENCODING);
	test_bad(context, HX_OS_CONFIG_XML, "<a><b></a>", HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_XML, "<a x='1' x='2'/>", HX_OS_EXISTS);
	test_bad(context, HX_OS_CONFIG_XML, "<!DOCTYPE a><a/>",
		HX_OS_NOT_SUPPORTED);
	test_bad(context, HX_OS_CONFIG_XML, "<a>&#0;</a>", HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_XML, "<?xml version='&#49;.0'?><a/>",
		HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_TOML, "a=1\rb=2", HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_TOML, "[[item]]\na=1",
		HX_OS_NOT_SUPPORTED);
	test_bad(context, HX_OS_CONFIG_TOML, "a=1\na=2", HX_OS_EXISTS);
	test_bad(context, HX_OS_CONFIG_TOML, "a=9223372036854775808",
		HX_OS_RANGE);
	test_bad(context, HX_OS_CONFIG_YAML, "x: &anchor value",
		HX_OS_NOT_SUPPORTED);
	test_bad(context, HX_OS_CONFIG_YAML, "x:\n\tbad: 1", HX_OS_SYNTAX);
	test_bad(context, HX_OS_CONFIG_ENV, "BAD-NAME=1", HX_OS_SYNTAX);
	hx_os_config_options_init(&options);
	options.source_name = "配置.json";
	options.duplicates = HX_OS_CONFIG_DUPLICATE_LAST;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, duplicate,
		      strlen(duplicate), &options, &doc, &error) == HX_OS_OK);
	test_number(test_pointer(doc, "/a"), 2);
	hx_os_config_release(doc);
	options.duplicates = HX_OS_CONFIG_DUPLICATE_FIRST;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, duplicate,
		      strlen(duplicate), &options, &doc, &error) == HX_OS_OK);
	test_number(test_pointer(doc, "/a"), 1);
	hx_os_config_release(doc);
	options.max_depth = 2;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "[[[]]]", 6,
		      &options, &doc, &error) == HX_OS_LIMIT &&
		!doc);
	CHECK(!strcmp(error.source, "配置.json"));
	options.max_depth = 64;
	options.max_memory_bytes = 1;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "{}", 2, &options,
		      &doc, &error) == HX_OS_LIMIT &&
		!doc);
	options.max_memory_bytes = 65536;
	options.max_nodes = 1;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "[1]", 3, &options,
		      &doc, &error) == HX_OS_LIMIT &&
		!doc);
	options.max_nodes = 64;
	options.max_work = 1;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "{}", 2, &options,
		      &doc, &error) == HX_OS_LIMIT &&
		!doc);
}

typedef struct test_allocator {
	size_t attempts;
	size_t fail_at;
	size_t live;
	size_t bytes;
	size_t peak;
	int reenter;
} test_allocator;

typedef union test_allocation {
	size_t bytes;
	long double alignment;
	void *pointer;
} test_allocation;

static void *test_allocate(size_t bytes, void *user)
{
	test_allocator *allocator = user;
	test_allocation *memory;
	if (++allocator->attempts == allocator->fail_at)
		return NULL;
	if (allocator->reenter) {
		hx_os_config_document *doc = test_parse(NULL, HX_OS_CONFIG_JSON,
			HX_OS_CONFIG_CJSON, "{\"nested\":true}");
		hx_os_config_release(doc);
	}
	memory = malloc(bytes + sizeof(*memory));
	if (!memory)
		return NULL;
	memory->bytes = bytes;
	++allocator->live;
	allocator->bytes += bytes;
	if (allocator->peak < allocator->bytes)
		allocator->peak = allocator->bytes;
	return memory + 1;
}

static void test_deallocate(void *memory, void *user)
{
	test_allocator *allocator = user;
	test_allocation *allocation = (test_allocation *)memory - 1;
	CHECK(allocator->live > 0);
	--allocator->live;
	allocator->bytes -= allocation->bytes;
	free(allocation);
}

static void test_allocation_failures(int backend)
{
	static const char *const inputs[] = {"{\"a\":[1,\"中文\",true]}",
		"a:\n  - name: value\n    n: 1\n", "<a x='b'><c/>text</a>",
		"[s]\na=b\n", "[s]\na=[1,2]\n", "A=value\n", "a\\ key=value\n"};
	int format;
	for (format = 0; format <= HX_OS_CONFIG_PROPERTIES; ++format) {
		size_t fail;
		if (!hx_os_config_backend_available(format, backend))
			continue;
		for (fail = 2; fail < 512; ++fail) {
			test_allocator allocator = {0};
			hx_os_context_options options;
			hx_os_config_options config;
			hx_os_context *context;
			hx_os_config_document *doc;
			hx_os_status status;
			allocator.fail_at = fail;
			hx_os_config_options_init(&config);
			config.backend = backend;
			hx_os_context_options_init(&options);
			options.allocator.allocate = test_allocate;
			options.allocator.deallocate = test_deallocate;
			options.allocator.user = &allocator;
			CHECK(hx_os_context_create(&options, &context, NULL) ==
				HX_OS_OK);
			status = hx_os_config_parse(context, format,
				inputs[format], strlen(inputs[format]), &config,
				&doc, NULL);
			CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
			hx_os_config_release(doc);
			CHECK(hx_os_context_destroy(&context, NULL) ==
				HX_OS_OK);
			CHECK(allocator.live == 0);
			if (status == HX_OS_OK)
				break;
		}
		CHECK(fail < 512);
	}
}

static void test_backend_features(hx_os_context *context)
{
	hx_os_config_document *doc;
	if (hx_os_config_backend_available(
		    HX_OS_CONFIG_YAML, HX_OS_CONFIG_LIBYAML)) {
		doc = test_parse(context, HX_OS_CONFIG_YAML,
			HX_OS_CONFIG_LIBYAML,
			"flow: {list: [1, true, \"中文\"]}\n"
			"text: |\n  first\n  second\n");
		test_text(test_pointer(doc, "/text"), "first\nsecond\n");
		test_text(test_pointer(doc, "/flow/list/2"), "中文");
		hx_os_config_release(doc);
	}
	if (hx_os_config_backend_available(
		    HX_OS_CONFIG_TOML, HX_OS_CONFIG_TOMLC)) {
		doc = test_parse(context, HX_OS_CONFIG_TOML, HX_OS_CONFIG_TOMLC,
			"text=\"\"\"\nfirst\nsecond\"\"\"\n"
			"date=2026-09-28T10:11:12.123456789+08:00\n"
			"[[items]]\nname='one'\n[[items]]\nname='two'\n");
		test_text(test_pointer(doc, "/text"), "first\nsecond");
		test_text(test_pointer(doc, "/date"),
			"2026-09-28T10:11:12.123456789+08:00");
		test_text(test_pointer(doc, "/items/1/name"), "two");
		hx_os_config_release(doc);
	}
}

static void test_memory_limit(int backend, int format, const char *input)
{
	test_allocator allocator = {0};
	hx_os_context_options options;
	hx_os_config_options config;
	hx_os_context *context;
	hx_os_config_document *doc;
	size_t baseline, limit;
	hx_os_context_options_init(&options);
	options.allocator.allocate = test_allocate;
	options.allocator.deallocate = test_deallocate;
	options.allocator.user = &allocator;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	baseline = allocator.bytes;
	hx_os_config_options_init(&config);
	config.backend = backend;
	for (limit = 512; limit <= 262144; limit *= 2) {
		hx_os_status status;
		config.max_memory_bytes = limit;
		allocator.peak = baseline;
		status = hx_os_config_parse(context, format, input,
			strlen(input), &config, &doc, NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_LIMIT);
		CHECK(allocator.peak <= baseline + limit);
		hx_os_config_release(doc);
		CHECK(allocator.bytes == baseline);
	}
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(allocator.bytes == 0);
}

static void test_reentry(void)
{
	test_allocator allocator = {0};
	hx_os_context_options options;
	hx_os_context *context;
	hx_os_config_document *doc;
	if (!hx_os_config_backend_available(
		    HX_OS_CONFIG_JSON, HX_OS_CONFIG_CJSON))
		return;
	hx_os_context_options_init(&options);
	options.allocator.allocate = test_allocate;
	options.allocator.deallocate = test_deallocate;
	options.allocator.user = &allocator;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	allocator.reenter = 1;
	doc = test_parse(context, HX_OS_CONFIG_JSON, HX_OS_CONFIG_CJSON,
		"{\"outer\":[1,2,3]}");
	hx_os_config_release(doc);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(allocator.live == 0);
}

static void *test_parse_thread(void *user)
{
	hx_os_context *context = user;
	int iteration, format, backend;
	for (iteration = 0; iteration < 12; ++iteration) {
		for (format = 0; format <= HX_OS_CONFIG_PROPERTIES; ++format) {
			for (backend = 0; backend <= HX_OS_CONFIG_TOMLC;
				++backend) {
				if (hx_os_config_backend_available(
					    format, backend))
					test_formats(context, format, backend);
			}
		}
	}
	return NULL;
}

static void test_concurrency(hx_os_context *context)
{
	hx_os_thread *threads[4];
	int i;
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(context, test_parse_thread, context,
			      &threads[i], NULL) == HX_OS_OK);
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      HX_OS_DEADLINE_INFINITE, NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
}

static void test_numeric_locale(hx_os_context *context)
{
	char saved[256];
	const char *locale = setlocale(LC_NUMERIC, NULL);
	int backend;
	CHECK(locale && strlen(locale) < sizeof(saved));
	strcpy(saved, locale);
	if (!setlocale(LC_NUMERIC, "German_Germany.1252") &&
		!setlocale(LC_NUMERIC, "de_DE.UTF-8"))
		return;
	for (backend = 0; backend <= HX_OS_CONFIG_TOMLC; ++backend) {
		int format;
		for (format = 0; format <= HX_OS_CONFIG_PROPERTIES; ++format)
			if (hx_os_config_backend_available(format, backend))
				test_formats(context, format, backend);
	}
	CHECK(setlocale(LC_NUMERIC, saved));
}

static void test_load(hx_os_context *context)
{
	const char *path = "hx_config_中文_😀.json";
	hx_os_file *file;
	hx_os_config_document *doc;
	hx_os_config_options options;
	hx_os_config_error error;
	size_t done;
	CHECK(hx_os_file_open(context, path,
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_TRUNCATE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(file, "{\"a\":1}", 7, &done, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_config_load(context, HX_OS_CONFIG_JSON, path, NULL, &doc,
		      &error) == HX_OS_OK);
	test_number(test_pointer(doc, "/a"), 1);
	hx_os_config_release(doc);
	hx_os_config_options_init(&options);
	options.max_input_bytes = 7;
	CHECK(hx_os_config_load(context, HX_OS_CONFIG_JSON, path, &options,
		      &doc, &error) == HX_OS_OK);
	hx_os_config_release(doc);
	options.max_input_bytes = 6;
	CHECK(hx_os_config_load(context, HX_OS_CONFIG_JSON, path, &options,
		      &doc, &error) == HX_OS_LIMIT &&
		!doc);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(hx_os_config_load(context, HX_OS_CONFIG_JSON, path, NULL, &doc,
		      &error) == HX_OS_NOT_FOUND &&
		!doc);
	CHECK(error.detail.domain != HX_OS_ERROR_NONE);
}

int main(void)
{
	hx_os_context *context;
	int format, backend;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	if (!(hx_os_capabilities() & HX_OS_CAP_CONFIG)) {
		hx_os_config_document *doc;
		CHECK(hx_os_config_parse(context, HX_OS_CONFIG_JSON, "{}", 2,
			      NULL, &doc, NULL) == HX_OS_NOT_SUPPORTED &&
			!doc);
	}
	else {
		for (format = 0; format <= HX_OS_CONFIG_PROPERTIES; ++format) {
			for (backend = 0; backend <= HX_OS_CONFIG_TOMLC;
				++backend) {
				if (hx_os_config_backend_available(
					    format, backend)) {
					printf("Checking %s backend %d\n",
						hx_os_config_format_name(
							format),
						backend);
					fflush(stdout);
					test_formats(context, format, backend);
				}
			}
		}
		test_failures(context);
		for (backend = 0; backend <= HX_OS_CONFIG_TOMLC; ++backend) {
			printf("Checking allocation failures for backend %d\n",
				backend);
			fflush(stdout);
			test_allocation_failures(backend);
			for (format = 0; format <= HX_OS_CONFIG_PROPERTIES;
				++format) {
				const char *input = format == HX_OS_CONFIG_JSON
					? "{}"
					: format == HX_OS_CONFIG_XML  ? "<a/>"
					: format == HX_OS_CONFIG_YAML ? "a: b\n"
								      : "a=1\n";
				if (hx_os_config_backend_available(
					    format, backend))
					test_memory_limit(
						backend, format, input);
			}
		}
		test_backend_features(context);
		test_reentry();
		test_numeric_locale(context);
		test_concurrency(context);
		test_load(context);
	}
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("Configuration contracts passed");
	return 0;
}
