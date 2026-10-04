#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(v)                                                               \
	do {                                                                   \
		if (!(v)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #v);        \
			exit(1);                                               \
		}                                                              \
	} while (0)

static const char mixed[] =
	"{\"items\":[1,\"文字\",null,{\"children\":[true,2.5,{},[],"
	"\"quote\\\" slash\\\\ newline\\n zero\\u0000 \\u0085 \\u2028 \\ufffe "
	"\\uffff "
	"\\uD83D\\uDE80\"]}],"
	"\"literal\":\"{\\\"x\\\":[1]}\",\"integer\":9007199254740993}";

static hx_os_config_document *parse(const char *s, size_t bytes, int format,
	const hx_os_config_options *options)
{
	hx_os_config_document *doc = NULL;
	hx_os_config_error error;

	hx_os_status status = hx_os_config_parse(
		NULL, format, s, bytes, options, &doc, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "parse %s: %s %s\n",
			hx_os_config_format_name(format),
			hx_os_status_name(status), error.message);

	CHECK(status == HX_OS_OK);

	return doc;
}

static void same(const hx_os_config_node *a, const hx_os_config_node *b)
{
	const hx_os_config_node *x, *y;
	hx_os_config_slice u, v;

	CHECK(hx_os_config_kind(a) == hx_os_config_kind(b));
	CHECK(hx_os_config_count(a) == hx_os_config_count(b));

	u = hx_os_config_name(a);
	v = hx_os_config_name(b);
	CHECK(u.bytes == v.bytes && !memcmp(u.data, v.data, u.bytes));
	if (hx_os_config_kind(a) <= HX_OS_CONFIG_STRING) {
		CHECK(hx_os_config_scalar_text(a, &u) == HX_OS_OK);
		CHECK(hx_os_config_scalar_text(b, &v) == HX_OS_OK);
		CHECK(u.bytes == v.bytes && !memcmp(u.data, v.data, u.bytes));
	}

	for (x = hx_os_config_first(a), y = hx_os_config_first(b); x;
		x = hx_os_config_next(x), y = hx_os_config_next(y))
		same(x, y);
}

static void roundtrip(hx_os_config_document *source, int format,
	const hx_os_config_options *options, int backend)
{
	hx_os_config_document *result;
	hx_os_config_options input;
	hx_string text = HX_STRING_INIT;

	hx_os_config_options_init(&input);
	if (options)
		input = *options;

	CHECK(hx_os_config_serialize(source, format, &text, options, NULL) ==
		HX_OS_OK);

	input.backend = backend;

	result = parse(text.data, text.size, format, &input);
	same(hx_os_config_root(source), hx_os_config_root(result));

	hx_os_config_release(result);
	hx_string_destroy(&text);
}

static void test_mixed(void)
{
	hx_os_config_document *source =
		parse(mixed, strlen(mixed), HX_OS_CONFIG_JSON, NULL);
	hx_os_config_document *replacement = NULL, *embedded = NULL;
	const hx_os_config_node *target, *node;
	hx_os_config_value value = {
		{NULL, 0}, {"false", 5}, NULL, NULL, 0, 0, HX_OS_CONFIG_BOOL};
	hx_string text = HX_STRING_INIT;
	int64_t integer;

	roundtrip(source, HX_OS_CONFIG_JSON, NULL, HX_OS_CONFIG_BUILTIN);
	roundtrip(source, HX_OS_CONFIG_YAML, NULL, HX_OS_CONFIG_BUILTIN);
	if (hx_os_config_backend_available(
		    HX_OS_CONFIG_YAML, HX_OS_CONFIG_LIBYAML))
		roundtrip(
			source, HX_OS_CONFIG_YAML, NULL, HX_OS_CONFIG_LIBYAML);
	CHECK(hx_os_config_pointer(hx_os_config_root(source), "/integer",
		      &node) == HX_OS_OK);
	CHECK(hx_os_config_get_i64(node, &integer) == HX_OS_OK &&
		integer == INT64_C(9007199254740993));
	CHECK(hx_os_config_pointer(hx_os_config_root(source),
		      "/items/3/children/0", &target) == HX_OS_OK);
	CHECK(hx_os_config_replace(source, target, &value, NULL, &replacement,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_config_pointer(hx_os_config_root(replacement),
		      "/items/3/children/0", &node) == HX_OS_OK);
	CHECK(hx_os_config_get_bool(node, (int *)&value.kind) == HX_OS_OK &&
		value.kind == 0);
	CHECK(hx_os_config_get_bool(target, (int *)&value.kind) == HX_OS_OK &&
		value.kind == 1);
	CHECK(hx_os_config_pointer(hx_os_config_root(source), "/literal",
		      &node) == HX_OS_OK);
	CHECK(hx_os_config_kind(node) == HX_OS_CONFIG_STRING);
	CHECK(hx_os_config_parse_node(NULL, HX_OS_CONFIG_JSON, node, NULL,
		      &embedded, NULL) == HX_OS_OK);
	CHECK(hx_os_config_kind(hx_os_config_root(embedded)) ==
		HX_OS_CONFIG_OBJECT);
	CHECK(hx_string_assign_cstr(&text, "kept", NULL) == HX_OS_OK);
	CHECK(hx_os_config_convert(NULL, HX_OS_CONFIG_JSON, mixed,
		      strlen(mixed), HX_OS_CONFIG_TOML, &text, NULL,
		      NULL) == HX_OS_UNREPRESENTABLE);
	CHECK(!strcmp(text.data, "kept"));
	CHECK(hx_os_config_convert(NULL, HX_OS_CONFIG_JSON, mixed,
		      strlen(mixed), HX_OS_CONFIG_XML, &text, NULL,
		      NULL) == HX_OS_UNREPRESENTABLE);

	hx_os_config_release(embedded);
	hx_os_config_release(replacement);
	hx_os_config_release(source);
	hx_string_destroy(&text);
}

static void test_depth(size_t depth)
{
	hx_string input = HX_STRING_INIT;
	hx_os_config_options options;
	hx_os_config_document *doc, *rejected = NULL;
	size_t i;

	hx_os_config_options_init(&options);
	options.max_depth = depth;

	for (i = 1; i < depth; ++i)
		CHECK(hx_string_append_cstr(&input, i % 2 ? "{\"n\":" : "[",
			      NULL) == HX_OS_OK);

	CHECK(hx_string_append_cstr(&input, "42", NULL) == HX_OS_OK);

	for (i = depth - 1; i; --i)
		CHECK(hx_string_append_cstr(&input, i % 2 ? "}" : "]", NULL) ==
			HX_OS_OK);

	doc = parse(input.data, input.size, HX_OS_CONFIG_JSON, &options);
	roundtrip(doc, HX_OS_CONFIG_JSON, &options, HX_OS_CONFIG_BUILTIN);
	roundtrip(doc, HX_OS_CONFIG_YAML, &options, HX_OS_CONFIG_BUILTIN);
	roundtrip(doc, HX_OS_CONFIG_TOML, &options, HX_OS_CONFIG_BUILTIN);
	if (hx_os_config_backend_available(
		    HX_OS_CONFIG_JSON, HX_OS_CONFIG_CJSON))
		roundtrip(doc, HX_OS_CONFIG_JSON, &options, HX_OS_CONFIG_CJSON);
	options.max_depth--;
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, input.data,
		      input.size, &options, &rejected, NULL) == HX_OS_LIMIT &&
		!rejected);
	CHECK(hx_os_config_serialize(doc, HX_OS_CONFIG_JSON, &input, &options,
		      NULL) == HX_OS_LIMIT);

	hx_os_config_release(doc);
	hx_string_destroy(&input);
}

static void test_create(void)
{
	hx_os_config_value children[] = {
		{{"text", 4}, {"中文", sizeof("中文") - 1}, NULL, NULL, 0, 0,
			HX_OS_CONFIG_STRING},
		{{"empty", 5}, {NULL, 0}, NULL, NULL, 0, 0,
			HX_OS_CONFIG_OBJECT},
		{{"array", 5}, {NULL, 0}, NULL, NULL, 0, 0, HX_OS_CONFIG_ARRAY},
		{{"nil", 3}, {NULL, 0}, NULL, NULL, 0, 0, HX_OS_CONFIG_NULL}};
	hx_os_config_value root = {{NULL, 0}, {NULL, 0}, children, NULL, 4, 0,
		HX_OS_CONFIG_OBJECT};
	hx_os_config_document *doc = NULL;
	hx_os_config_options options;

	CHECK(hx_os_config_create(NULL, &root, NULL, &doc, NULL) == HX_OS_OK);

	roundtrip(doc, HX_OS_CONFIG_JSON, NULL, HX_OS_CONFIG_BUILTIN);
	roundtrip(doc, HX_OS_CONFIG_YAML, NULL, HX_OS_CONFIG_BUILTIN);

	hx_os_config_release(doc);
	doc = NULL;
	children[1].name = children[0].name;
	CHECK(hx_os_config_create(NULL, &root, NULL, &doc, NULL) ==
			HX_OS_EXISTS &&
		!doc);

	root.count = SIZE_MAX;
	CHECK(hx_os_config_create(NULL, &root, NULL, &doc, NULL) ==
			HX_OS_LIMIT &&
		!doc);

	root.kind = HX_OS_CONFIG_ARRAY;
	root.children = &root;
	root.count = 1;
	hx_os_config_options_init(&options);
	options.max_depth = 12;
	CHECK(hx_os_config_create(NULL, &root, &options, &doc, NULL) ==
			HX_OS_LIMIT &&
		!doc);

	root.kind = HX_OS_CONFIG_ELEMENT;
	root.name.data = "ok";
	root.name.bytes = 2;
	root.children = NULL;
	root.count = 0;
	CHECK(hx_os_config_create(NULL, &root, NULL, &doc, NULL) == HX_OS_OK);

	hx_os_config_release(doc);
	root.name.data = "1x";
	CHECK(hx_os_config_create(NULL, &root, NULL, &doc, NULL) ==
			HX_OS_INVALID &&
		!doc);
}

static void test_other_formats(void)
{
	const char *source = "<r "
			     "a=\"&#13;&#10;&#9;&quot;\">left<![CDATA[<b>]]><x>"
			     "1&amp;2</x>tail</r>";
	const char *flat = "{\"NAME\":\" line\\n\\\"\\\\ \\u0085\\u0000\"}";
	const char *toml =
		"{\"mixed\":[1,\"x\",true,{\"empty\":[],\"object\":{}}]}";

	hx_os_config_document *a = parse(source, strlen(source),
				      HX_OS_CONFIG_XML, NULL),
			      *b;

	hx_string one = HX_STRING_INIT, two = HX_STRING_INIT;

	CHECK(hx_os_config_serialize(a, HX_OS_CONFIG_XML, &one, NULL, NULL) ==
		HX_OS_OK);

	b = parse(one.data, one.size, HX_OS_CONFIG_XML, NULL);
	CHECK(hx_os_config_serialize(b, HX_OS_CONFIG_XML, &two, NULL, NULL) ==
		HX_OS_OK);
	CHECK(one.size == two.size && !memcmp(one.data, two.data, one.size));

	hx_os_config_release(a);
	hx_os_config_release(b);
	a = parse(flat, strlen(flat), HX_OS_CONFIG_JSON, NULL);
	roundtrip(a, HX_OS_CONFIG_ENV, NULL, HX_OS_CONFIG_BUILTIN);

	hx_os_config_release(a);
	a = parse(toml, strlen(toml), HX_OS_CONFIG_JSON, NULL);
	roundtrip(a, HX_OS_CONFIG_TOML, NULL, HX_OS_CONFIG_BUILTIN);
	if (hx_os_config_backend_available(
		    HX_OS_CONFIG_TOML, HX_OS_CONFIG_TOMLC))
		roundtrip(a, HX_OS_CONFIG_TOML, NULL, HX_OS_CONFIG_TOMLC);
	hx_os_config_release(a);
	hx_string_destroy(&one);
	hx_string_destroy(&two);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE))
		return 0;

	test_mixed();
	test_create();
	test_other_formats();
	test_depth(32);
	test_depth(64);
	test_depth(128);
	puts("deep mixed trees and all-format output passed");

	return 0;
}
