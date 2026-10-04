#include <hx_os.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_file_times times = HX_OS_FILE_TIMES_INIT;
	int64_t milliseconds;
	hx_os_error error;

	if (argc != 2 ||
		hx_os_wall_time_read(&milliseconds, &error) != HX_OS_OK)
		return 1;
	times.fields = HX_OS_TIME_MODIFIED;
	times.modified_seconds = milliseconds / 1000;
	if (milliseconds % 1000 < 0) {
		--times.modified_seconds;
		milliseconds = milliseconds % 1000 + 1000;
	}

	times.modified_nanoseconds = (uint32_t)(milliseconds % 1000) * 1000000;
	if (hx_os_file_set_times(argv[1], &times, 1, &error) != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(error.status));

		return 1;
	}

	puts("Modification time updated; access time preserved.");

	return 0;
}
