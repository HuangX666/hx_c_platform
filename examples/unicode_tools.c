#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string output = HX_STRING_INIT;
	hx_string_view input = {"Stra\xc3\x9f"
				"e",
		7};
	hx_os_status status = hx_os_unicode_casefold(input, &output, NULL);

	if (status == HX_OS_OK)
		puts(hx_string_c_str(&output));
	else
		fprintf(stderr, "Unicode failed: %s\n",
			hx_os_status_name(status));

	hx_string_destroy(&output);
	return status == HX_OS_OK ? 0 : 1;
}
