#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
static void import_json(
	hx_settings *settings, const char *text, hx_os_status expected)
{
	hx_os_config_document *document = NULL;

	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, text, strlen(text),
		      NULL, &document, NULL) == HX_OS_OK);
	CHECK(hx_settings_import(settings, document, NULL) == expected);

	hx_os_config_release(document);
}

int main(int argc, char **argv)
{
	hx_settings *settings = NULL, *loaded = NULL;
	hx_settings_value *value = NULL;
	hx_os_config_document *document = NULL, *source = NULL;
	const hx_os_config_node *node;
	hx_os_config_error detail;
	hx_string json = HX_STRING_INIT, copy = HX_STRING_INIT;
	int64_t integer;
	double real;
	char directory[96], path[256];
	const char input[] = "{\"n\":9223372036854775807,\"x\":0.1,"
			     "\"a\\u0000b\":[true,null,\"中文😀\"]}";

	uint64_t capabilities = hx_os_capabilities();
	if (argc > 1)
		CHECK(setlocale(LC_NUMERIC, argv[1]) != NULL);

	if (!(capabilities & HX_OS_CAP_SETTINGS))
		return 0;

	CHECK(hx_settings_create(NULL, &settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_set_default_i64(settings, "fallback", 9, NULL) ==
		HX_OS_OK);
	if (!(capabilities & HX_OS_CAP_CONFIG)) {
		CHECK(hx_settings_import(settings, NULL, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_settings_to_config(settings, HX_SETTINGS_EXPLICIT,
			      NULL, &document, NULL) == HX_OS_NOT_SUPPORTED &&
			!document);
		CHECK(hx_settings_load(settings, HX_OS_CONFIG_JSON, "unused",
			      NULL, &detail) == HX_OS_NOT_SUPPORTED &&
			detail.detail.status == HX_OS_NOT_SUPPORTED);
		CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);

		return 0;
	}

	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, input,
		      sizeof(input) - 1, NULL, &source, NULL) == HX_OS_OK);
	CHECK(hx_settings_import(settings, source, NULL) == HX_OS_OK);
	CHECK(hx_settings_get_i64(settings, "n", &integer, NULL) == HX_OS_OK &&
		integer == INT64_MAX);
	CHECK(hx_settings_get_double(settings, "x", &real, NULL) == HX_OS_OK &&
		real == 0.1);
	CHECK(hx_settings_to_config(settings, HX_SETTINGS_EXPLICIT, NULL,
		      &document, NULL) == HX_OS_OK);
	CHECK(hx_os_config_member(hx_os_config_root(document), "fallback", 8,
		      &node) == HX_OS_NOT_FOUND);

	hx_os_config_release(document);
	document = NULL;
	CHECK(hx_settings_to_config(settings, HX_SETTINGS_EFFECTIVE, NULL,
		      &document, NULL) == HX_OS_OK);
	CHECK(hx_os_config_member(hx_os_config_root(document), "fallback", 8,
		      &node) == HX_OS_OK &&
		hx_os_config_get_i64(node, &integer) == HX_OS_OK &&
		integer == 9);

	hx_os_config_release(document);
	document = NULL;
	CHECK(hx_settings_set_i64(settings, "n", 42, NULL) == HX_OS_OK);
	CHECK(hx_os_config_member(hx_os_config_root(source), "n", 1, &node) ==
			HX_OS_OK &&
		hx_os_config_get_i64(node, &integer) == HX_OS_OK &&
		integer == INT64_MAX);

	hx_os_config_release(source);
	source = NULL;
	import_json(settings, "[]", HX_OS_TYPE_MISMATCH);
	import_json(settings, "{\"bad\":18446744073709551615}", HX_OS_RANGE);
	CHECK(hx_settings_get_i64(settings, "n", &integer, NULL) == HX_OS_OK &&
		integer == 42);
	if (!(capabilities & HX_OS_CAP_CONFIG_SERIALIZE)) {
		CHECK(hx_settings_to_json(settings, &json, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);

		return 0;
	}

	CHECK(hx_settings_to_json(settings, &json, NULL) == HX_OS_OK);
	CHECK(!strstr(hx_string_c_str(&json), "fallback"));
	CHECK(strstr(hx_string_c_str(&json), "a\\u0000b") != NULL);
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_JSON,
		      hx_string_c_str(&json), json.size, NULL, &document,
		      NULL) == HX_OS_OK);
	CHECK(hx_settings_create(NULL, &loaded, NULL) == HX_OS_OK);
	CHECK(hx_settings_import(loaded, document, NULL) == HX_OS_OK);

	hx_os_config_release(document);
	document = NULL;
	CHECK(hx_settings_to_json(loaded, &copy, NULL) == HX_OS_OK &&
		!strcmp(hx_string_c_str(&json), hx_string_c_str(&copy)));
	CHECK(hx_settings_set_pointer(loaded, "pointer", &integer, NULL) ==
		HX_OS_OK);
	CHECK(hx_settings_to_json(loaded, &copy, NULL) ==
			HX_OS_UNREPRESENTABLE &&
		!strcmp(hx_string_c_str(&json), hx_string_c_str(&copy)));
	CHECK(hx_settings_destroy(&loaded, NULL) == HX_OS_OK);

	import_json(settings,
		"{\"section\":{\"name\":\"中文😀\"},\"global\":\"value\"}",
		HX_OS_OK);
	CHECK(hx_settings_serialize(settings, HX_OS_CONFIG_INI, &copy, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_INI, hx_string_c_str(&copy),
		      copy.size, NULL, &document, NULL) == HX_OS_OK);
	CHECK(hx_settings_import(settings, document, NULL) == HX_OS_OK);

	hx_os_config_release(document);
	document = NULL;
	import_json(settings, "{\"a\":\"中文😀\",\"b\":\"x=y\"}", HX_OS_OK);
	CHECK(hx_settings_serialize(settings, HX_OS_CONFIG_PROPERTIES, &copy,
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_config_parse(NULL, HX_OS_CONFIG_PROPERTIES,
		      hx_string_c_str(&copy), copy.size, NULL, &document,
		      NULL) == HX_OS_OK);
	CHECK(hx_settings_import(settings, document, NULL) == HX_OS_OK);

	hx_os_config_release(document);
	document = NULL;
	{
		const int formats[] = {
			HX_OS_CONFIG_YAML, HX_OS_CONFIG_TOML, HX_OS_CONFIG_ENV};
		size_t i;

		for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
			CHECK(hx_settings_serialize(settings, formats[i], &copy,
				      NULL, NULL) == HX_OS_OK);
			CHECK(hx_os_config_parse(NULL, formats[i], copy.data,
				      copy.size, NULL, &document,
				      NULL) == HX_OS_OK);
			CHECK(hx_settings_import(settings, document, NULL) ==
				HX_OS_OK);

			hx_os_config_release(document);
			document = NULL;
		}
	}

	CHECK(hx_settings_set_i64(settings, "number", 1, NULL) == HX_OS_OK);
	CHECK(hx_settings_serialize(settings, HX_OS_CONFIG_PROPERTIES, &copy,
		      NULL, NULL) == HX_OS_UNREPRESENTABLE);
	if (capabilities & HX_OS_CAP_FILE_UTILS) {
		(void)snprintf(directory, sizeof(directory), "hx-settings-%llx",
			(unsigned long long)hx_os_monotonic_ns());
		CHECK(hx_os_directory_create(directory, 0, NULL) == HX_OS_OK);

		(void)snprintf(
			path, sizeof(path), "%s/%s", directory, "配置😀.json");
		CHECK(hx_settings_save_json(settings, path, NULL) == HX_OS_OK);
		CHECK(hx_settings_create(NULL, &loaded, NULL) == HX_OS_OK);
		CHECK(hx_settings_set_default_i64(
			      loaded, "fallback", 123, NULL) == HX_OS_OK);
		CHECK(hx_settings_load_json(loaded, path, NULL) == HX_OS_OK);
		CHECK(hx_settings_get_i64(loaded, "fallback", &integer, NULL) ==
				HX_OS_OK &&
			integer == 123);
		CHECK(hx_os_file_save(path, "{bad", 4, 1, NULL) == HX_OS_OK);
		CHECK(hx_settings_load(loaded, HX_OS_CONFIG_JSON, path, NULL,
			      &detail) == HX_OS_SYNTAX &&
			detail.line == 1);
		CHECK(hx_settings_get_i64(loaded, "number", &integer, NULL) ==
				HX_OS_OK &&
			integer == 1);
		CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
		CHECK(hx_settings_load(loaded, HX_OS_CONFIG_JSON, path, NULL,
			      &detail) == HX_OS_NOT_FOUND &&
			detail.detail.native_code);
		CHECK(hx_settings_destroy(&loaded, NULL) == HX_OS_OK);
		CHECK(hx_os_directory_remove(directory, NULL) == HX_OS_OK);
	}

	CHECK(hx_settings_get_value(settings, hx_string_view_from_cstr("a"),
		      &value, NULL) == HX_OS_OK);
	CHECK(hx_settings_destroy(&settings, NULL) == HX_OS_OK);
	CHECK(hx_settings_value_get_string(value, &copy, NULL) == HX_OS_OK &&
		!strcmp(hx_string_c_str(&copy), "中文😀"));

	hx_settings_value_release(value);
	hx_string_destroy(&copy);
	hx_string_destroy(&json);

	return 0;
}
