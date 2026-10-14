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
	hx_os_config_error error;
	hx_string text = HX_STRING_INIT;
	hx_os_status status =
		hx_os_config_create(NULL, &root, NULL, &document, &error);

	if (status == HX_OS_OK)
		status = hx_os_config_serialize(
			document, HX_OS_CONFIG_JSON, &text, NULL, NULL);

	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_os_config_release(document);
	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
