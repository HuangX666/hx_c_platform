#include "utility_test.h"

int main(void)
{
	const uint64_t needed = HX_OS_CAP_CONFIG | HX_OS_CAP_CONFIG_SERIALIZE;
	test_memory memory = {0, 0, 0};
	hx_os_context *context;
	hx_os_config_document *document = NULL, *old, *roundtrip = NULL;
	hx_os_config_options options;
	hx_os_config_error error;
	hx_string value = HX_STRING_INIT, text = HX_STRING_INIT;
	size_t index;

	if ((hx_os_capabilities() & needed) != needed) {
		CHECK(hx_os_ini_set(&document, "network", "port", "8080",
			      &error) == HX_OS_NOT_SUPPORTED);
		return 0;
	}
	context = test_context(&memory);
	CHECK(hx_os_ini_create(context, &document, &error) == HX_OS_OK);
	CHECK(hx_os_ini_set(&document, "network", "host", "localhost",
		      &error) == HX_OS_OK);
	old = document;
	hx_os_config_retain(old);
	CHECK(hx_os_ini_set(&document, "network", "host", "中文😀", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_get(old, "network", "host", &value, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&value), "localhost"));
	CHECK(hx_os_ini_get(document, "network", "host", &value, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&value), "中文😀"));
	CHECK(hx_os_ini_set(&document, NULL, "title", "example", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_set(&document, "a/b~c", "x.y/z", "literal", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_get(document, "a/b~c", "x.y/z", &value, NULL) ==
		HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&value), "literal"));
	CHECK(hx_os_ini_set(&document, "network", "port", "8080", &error) ==
		HX_OS_OK);
	CHECK(hx_os_config_serialize(document, HX_OS_CONFIG_INI, &text, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_INI, hx_string_c_str(&text),
		      text.size, NULL, &roundtrip, &error) == HX_OS_OK);
	CHECK(hx_os_ini_get(roundtrip, "", "title", &value, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&value), "example"));
	hx_os_config_release(roundtrip);
	roundtrip = NULL;

	CHECK(hx_os_ini_remove_key(&document, "network", "host", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_get(document, "network", "host", &value, NULL) ==
		HX_OS_NOT_FOUND);
	CHECK(!strcmp(hx_string_c_str(&value), "example"));
	CHECK(hx_os_ini_remove_key(&document, "network", "port", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_remove_section(&document, "network", &error) ==
		HX_OS_OK);
	CHECK(hx_os_ini_remove_section(&document, "network", &error) ==
		HX_OS_NOT_FOUND);
	CHECK(hx_os_ini_remove_key(&document, NULL, "title", &error) ==
		HX_OS_OK);

	for (index = 1; index < 64; ++index) {
		hx_os_config_document *before = document;
		size_t live = memory.live;
		hx_os_status status;

		memory.fail = memory.calls + index;
		status =
			hx_os_ini_set(&document, "new", "key", "value", &error);
		memory.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status == HX_OS_NO_MEMORY)
			CHECK(document == before && memory.live == live);
		else
			break;
	}
	CHECK(index < 64);
	{
		hx_os_config_document *before = document;

		CHECK(hx_os_ini_set(&document, "new", "key", "line\nvalue",
			      &error) == HX_OS_UNREPRESENTABLE);
		CHECK(hx_os_ini_set(&document, "new", "key", " trailing ",
			      &error) == HX_OS_UNREPRESENTABLE);
		CHECK(hx_os_ini_set(&document, "new", "key", "\xff", &error) ==
			HX_OS_INVALID_ENCODING);
		CHECK(hx_os_ini_remove_key(&document, "new", "\xff", &error) ==
			HX_OS_INVALID_ENCODING);
		CHECK(hx_os_ini_set(&document, NULL, "new", "collision",
			      &error) == HX_OS_TYPE_MISMATCH);
		CHECK(document == before);
	}
	if (hx_os_capabilities() & HX_OS_CAP_FILE_UTILS) {
		CHECK(hx_os_ini_save(document, "hx-edit.ini", NULL) ==
			HX_OS_OK);
		CHECK(hx_os_ini_load("hx-edit.ini", &roundtrip, &error) ==
			HX_OS_OK);
		hx_os_config_release(roundtrip);
		CHECK(hx_os_file_remove("hx-edit.ini", NULL) == HX_OS_OK);
	}
	hx_os_config_release(document);
	document = NULL;
	hx_os_config_options_init(&options);
	options.max_nodes = 3;
	CHECK(hx_os_config_parse(context, HX_OS_CONFIG_INI, "[x]\na=b\n", 8,
		      &options, &document, &error) == HX_OS_OK);
	CHECK(hx_os_ini_set(&document, "x", "second", "2", &error) ==
		HX_OS_LIMIT);
	CHECK(hx_os_ini_get(document, "x", "a", &value, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&value), "b"));

	hx_os_config_release(old);
	hx_os_config_release(document);
	hx_string_destroy(&text);
	hx_string_destroy(&value);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!memory.live);
	puts("INI section/key editing contracts passed");

	return 0;
}
