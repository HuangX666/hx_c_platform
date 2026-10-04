#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string uuid = HX_STRING_INIT;
	hx_os_error error;

	if (hx_os_uuid_generate_string(&uuid, &error) != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(error.status));

		return 1;
	}

	puts(hx_string_c_str(&uuid));

	hx_string_destroy(&uuid);

	return 0;
}
