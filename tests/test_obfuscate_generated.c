#include <hx_os.h>
#include "obfuscated_fixture.h"
#include <string.h>
int main(void)
{
	hx_string text = HX_STRING_INIT;
	hx_os_status status;
	int success;
	if (!(hx_os_capabilities() & HX_OS_CAP_OBFUSCATION))
		return 0;
	status = hx_os_obfuscate_string(&hx_generated, &text, NULL);
	success = status == HX_OS_OK &&
		!strcmp(hx_string_c_str(&text), "generated 中文😀");
	hx_string_destroy(&text);
	return success ? 0 : 1;
}
