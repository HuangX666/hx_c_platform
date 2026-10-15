#include <hx_os.h>
#include <stdio.h>
#include <string.h>

// Descriptions borrow input only until create() returns. Values retain types.
int main(void)
{
	hx_os_config_value children[] = {
		{{NULL, 0}, {"true", 4}, NULL, NULL, 0, 0, HX_OS_CONFIG_BOOL},
		{{NULL, 0}, {"2.5", 3}, NULL, NULL, 0, 0, HX_OS_CONFIG_REAL}};
	hx_os_config_value branch = {{"children", 8}, {NULL, 0}, children, NULL,
		2, 0, HX_OS_CONFIG_ARRAY};
	hx_os_config_value items[] = {
		{{NULL, 0}, {"1", 1}, NULL, NULL, 0, 0, HX_OS_CONFIG_INTEGER},
		{{NULL, 0}, {"文字", sizeof("文字") - 1}, NULL, NULL, 0, 0,
			HX_OS_CONFIG_STRING},
		{{NULL, 0}, {NULL, 0}, NULL, NULL, 0, 0, HX_OS_CONFIG_NULL},
		{{NULL, 0}, {NULL, 0}, &branch, NULL, 1, 0,
			HX_OS_CONFIG_OBJECT}};
	hx_os_config_value array = {
		{"items", 5}, {NULL, 0}, items, NULL, 4, 0, HX_OS_CONFIG_ARRAY};
	hx_os_config_value root = {
		{NULL, 0}, {NULL, 0}, &array, NULL, 1, 0, HX_OS_CONFIG_OBJECT};
	hx_os_config_document *document = NULL;
	hx_os_config_document *parsed = NULL;
	const hx_os_config_node *value = NULL;
	hx_os_config_error error;
	hx_string text = HX_STRING_INIT;
	double number = 0.0;
	hx_os_status status =
		hx_os_config_create(NULL, &root, NULL, &document, &error);

	if (status == HX_OS_OK)
		status = hx_os_config_serialize(
			document, HX_OS_CONFIG_JSON, &text, NULL, NULL);

	// Read the generated text as a new document, then select one value.
	if (status == HX_OS_OK)
		status = hx_os_config_parse(NULL, HX_OS_CONFIG_JSON, text.data,
			text.size, NULL, &parsed, &error);
	if (status == HX_OS_OK)
		status = hx_os_config_pointer(hx_os_config_root(parsed),
			"/items/3/children/1", &value);
	if (status == HX_OS_OK)
		status = hx_os_config_get_double(value, &number);

	if (status == HX_OS_OK) {
		puts(hx_string_c_str(&text));
		fprintf(stderr, "items[3].children[1] = %.1f\n", number);
	}
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_os_config_release(parsed);
	hx_os_config_release(document);
	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
