#include <hx_os.h>
#include <stdio.h>
int main(void)
{
	static const unsigned char bytes[] = {0x32, 0x22};
	static const hx_os_obfuscated value = {bytes, sizeof(bytes), 0x5a};
	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status = hx_os_obfuscate_string(&value, &text, &error);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
