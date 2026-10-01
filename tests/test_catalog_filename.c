#include "utility_test.h"
static const char catalog[] = "\xef\xbb\xbf"
			      "# base\r\nhello = \"你好\\n\\uD83D\\uDE00\"\r\n"
			      "app.title = First\napp.title= Last  # overlay\n"
			      "\"hash#key\" = \"value;#\" # comment\n"
			      "empty=\nurl=https://example.invalid/a\n";
static void expect(hx_os_text_lookup *table, const char *key, const char *value)
{
	hx_string text = HX_STRING_INIT;
	CHECK(hx_os_text_lookup_get_string(table, key, &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), value));
	hx_string_destroy(&text);
}
static void test_catalog(void)
{
	hx_os_text_lookup *table = NULL, *loaded = NULL;
	const char *file = "hx_catalog_test_中文.ini";
	const uint64_t caps = hx_os_capabilities();
	test_memory memory = {0, 0, 0};
	hx_os_context *context;
	size_t fail, live;
	hx_os_status status;
	const char *broken[] = {"hello=new\nbroken", "hello=new\nx=\"\\q\"",
		"[section]\nx=y", "x=\"a\" junk", "x=\"\\u0000\"",
		"\"key\" other=x", "x=\xff"};
	if (!(caps & HX_OS_CAP_TEXT_LOOKUP) || !(caps & HX_OS_CAP_LEXER)) {
		CHECK(hx_os_text_lookup_add_text(NULL,
			      hx_string_view_from_cstr(catalog),
			      NULL) == HX_OS_NOT_SUPPORTED);
		return;
	}
	context = test_context(&memory);
	CHECK(hx_os_text_lookup_create(context, &table, NULL) == HX_OS_OK);
	CHECK(hx_os_text_lookup_add_text(table,
		      hx_string_view_from_cstr(catalog), NULL) == HX_OS_OK);
	expect(table, "hello", "你好\n😀");
	expect(table, "app.title", "Last");
	expect(table, "hash#key", "value;#");
	expect(table, "empty", "");
	expect(table, "url", "https://example.invalid/a");
	CHECK(hx_os_text_lookup_add_text(table,
		      hx_string_view_from_cstr(
			      "raw=It's 1example, not a number\n"),
		      NULL) == HX_OS_OK);
	expect(table, "raw", "It's 1example, not a number");
	for (fail = 0; fail < sizeof(broken) / sizeof(broken[0]); ++fail) {
		status = hx_os_text_lookup_add_text(
			table, hx_string_view_from_cstr(broken[fail]), NULL);
		CHECK(status == HX_OS_SYNTAX ||
			status == HX_OS_INVALID_ENCODING);
		expect(table, "hello", "你好\n😀");
	}
	live = memory.live;
	for (fail = 1; fail < 100; ++fail) {
		memory.fail = memory.calls + fail;
		status = hx_os_text_lookup_add_text(table,
			hx_string_view_from_cstr("hello=changed\n"), NULL);
		memory.fail = 0;
		if (status == HX_OS_OK)
			break;
		CHECK(status == HX_OS_NO_MEMORY && memory.live == live);
		expect(table, "hello", "你好\n😀");
	}
	CHECK(fail < 100);
	expect(table, "hello", "changed");
	if (caps & HX_OS_CAP_FILE_UTILS) {
		CHECK(hx_os_file_save(file, catalog, sizeof(catalog) - 1, 0,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_text_lookup_load(NULL, file, &loaded, NULL) ==
			HX_OS_OK);
		expect(loaded, "hello", "你好\n😀");
		hx_os_text_lookup_destroy(&loaded);
		CHECK(hx_os_file_remove(file, NULL) == HX_OS_OK);
	}
	else
		CHECK(hx_os_text_lookup_add_file(table, file, NULL) ==
			HX_OS_NOT_SUPPORTED);
	hx_os_text_lookup_destroy(&table);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
}
static void test_filename(void)
{
	hx_string text = HX_STRING_INIT;
	char small[4] = "old";
	size_t required = 0;
	const uint64_t caps = hx_os_capabilities();
	if (!(caps & HX_OS_CAP_CALENDAR)) {
		CHECK(hx_os_filename_generate("txt", &text, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return;
	}
	CHECK(hx_os_filename_format_string(0, NULL, ".txt", &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(text.data, "1970-01-01_00-00-00.txt"));
	CHECK(hx_os_filename_format_string(0, "CON", "txt", &text, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(text.data, "_CON.txt"));
	CHECK(hx_os_filename_format_string(
		      0, "中文/%hh:%mm. ", NULL, &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(text.data, "中文_00_00__"));
	CHECK(hx_os_filename_format_string(0, NULL, "../bad", &text, NULL) ==
		HX_OS_INVALID);
	CHECK(!strcmp(text.data, "中文_00_00__"));
	CHECK(hx_os_filename_format(0, NULL, "txt", small, sizeof(small),
		      &required, NULL) == HX_OS_OUTPUT_FULL &&
		!strcmp(small, "old"));
	CHECK(required == 24);
	hx_string_destroy(&text);
}
static void test_encoded_file(void)
{
	const char *path = "hx_encoded_test_😀.txt";
	const unsigned char data[] = {0xff, 0xfe, 0x2d, 0x4e, 0x87, 0x65};
	hx_string text = HX_STRING_INIT;
	if (!(hx_os_capabilities() & HX_OS_CAP_FILE_UTILS))
		return;
	CHECK(hx_os_file_save(path, data, sizeof(data), 0, NULL) == HX_OS_OK);
	CHECK(hx_os_file_read_text_encoded(path, HX_OS_UTF16LE, &text, NULL) ==
		HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_BOM)
		CHECK(!strcmp(text.data, "中文"));
	else
		CHECK(!strcmp(text.data,
			"\xef\xbb\xbf"
			"中文"));
	CHECK(hx_os_file_read_text_encoded(path, HX_OS_UTF8, &text, NULL) ==
		HX_OS_INVALID_ENCODING);
	{
		const unsigned char nul_text[] = {0xff, 0xfe, 0, 0, 'A', 0};
		size_t prefix = hx_os_capabilities() & HX_OS_CAP_BOM ? 0 : 3;
		CHECK(hx_os_file_save(path, nul_text, sizeof(nul_text), 1,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_file_read_text_encoded(
			      path, HX_OS_UTF16LE, &text, NULL) == HX_OS_OK);
		CHECK(text.size == prefix + 2 && !text.data[prefix] &&
			text.data[prefix + 1] == 'A');
	}
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	hx_string_destroy(&text);
}
int main(void)
{
	test_catalog();
	test_filename();
	test_encoded_file();
	return 0;
}
