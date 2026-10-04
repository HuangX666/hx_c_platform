#include <hx_os_paths.h>
#include <stdio.h>

int main(void)
{
	hx_string path = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status = hx_os_path_config_home_string(&path, &error);
	if (status != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(status));

		hx_string_destroy(&path);

		return 1;
	}

	puts(hx_string_c_str(&path));

	hx_string_destroy(&path);

	return 0;
}
