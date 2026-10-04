#include <hx_os_file.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_file_info_extended info = HX_OS_FILE_INFO_EXTENDED_INIT;
	hx_os_error error;

	hx_os_status status = hx_os_path_stat_extended(
		argc > 1 ? argv[1] : ".", 0, &info, &error);
	if (status != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(status));

		return 1;
	}

	printf("kind=%u size=%llu readonly=%u known_permissions=%u\n",
		(unsigned)info.basic.kind, (unsigned long long)info.basic.size,
		(unsigned)info.read_only, (unsigned)info.known_permissions);

	return 0;
}
