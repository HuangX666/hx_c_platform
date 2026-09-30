#include "hx_os.h"
#include <stdio.h>

int main(void)
{
	hx_os_context *context = NULL;
	hx_string text;
	hx_os_status status;
	unsigned char utf16[128];
	hx_os_text_result converted;
	status = hx_os_context_create(NULL, &context, NULL);
	if (status != HX_OS_OK)
		return 1;
	hx_string_init(&text, context);
	status = hx_string_assign_cstr(&text, "Hello, 中文", NULL);
	if (status == HX_OS_OK)
		status = hx_string_append_format(&text, NULL, " %d", 2026);
	if (status == HX_OS_OK)
		status = hx_string_encode(&text, HX_OS_UTF16LE, utf16,
			sizeof(utf16), &converted, NULL);
	if (status == HX_OS_OK)
		printf("%s: %zu UTF-8 bytes, %zu codepoints, %zu UTF-16 "
		       "bytes\n",
			hx_os_platform_name(), hx_string_size_bytes(&text),
			hx_string_codepoint_count(&text),
			converted.output_used);
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	hx_string_destroy(&text);
	if (hx_os_context_destroy(&context, NULL) != HX_OS_OK)
		return 2;
	return status == HX_OS_OK ? 0 : 1;
}
