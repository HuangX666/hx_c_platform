#include <hx_os.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	hx_string value = HX_STRING_INIT;
	hx_os_status status;
	if (argc != 2) {
		fprintf(stderr, "Usage: environment <variable name>\n");
		return 1;
	}
	status = hx_os_env_get_string(argv[1], &value, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&value));
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	hx_string_destroy(&value);
	return status == HX_OS_OK ? 0 : 1;
}
