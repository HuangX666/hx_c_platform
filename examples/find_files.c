#include <hx_os.h>
#include <hx_os_system.h>
#include <stdio.h>
#include "example_arguments.h"

static hx_os_status HX_OS_CALL print_file(
	hx_string_view path, void *user, hx_os_error *error)
{
	(void)error;
	++*(size_t *)user;
	printf("%.*s\n", (int)path.size, path.data);

	return HX_OS_OK;
}

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_status status;
	size_t found = 0;
	uint64_t needed = HX_OS_CAP_FILE_SEARCH;

	if ((hx_os_capabilities() & needed) != needed) {
		puts("File search is disabled");

		return 0;
	}

	if (argc < 2) {
		puts("Usage: hx_os_find_files ROOT [BASENAME_PATTERN]");

		return 2;
	}

	status = hx_os_directory_find_files(
		argv[1], argc > 2 ? argv[2] : "*", print_file, &found, NULL);
	printf("Files: %zu\n", found);

	if (status != HX_OS_OK)
		fprintf(stderr, "Search failed: %s\n",
			hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
