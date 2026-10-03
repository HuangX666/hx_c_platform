#include <hx_os.h>
#include <hx_os_system.h>
#include <stdio.h>

static hx_os_status HX_OS_CALL print_file(
	hx_string_view path, void *user, hx_os_error *error)
{
	(void)error;
	++*(size_t *)user;
	printf("%.*s\n", (int)path.size, path.data);
	return HX_OS_OK;
}
int main(void)
{
	hx_os_string_list *arguments = NULL;
	hx_os_status status;
	size_t found = 0;
	uint64_t needed = HX_OS_CAP_FILE_SEARCH | HX_OS_CAP_SYSTEM_UTILS;
	if ((hx_os_capabilities() & needed) != needed) {
		puts("File search or command-line arguments are disabled");
		return 0;
	}
	status = hx_os_process_arguments(NULL, &arguments, NULL);
	if (status != HX_OS_OK)
		goto finish;
	if (hx_os_string_list_count(arguments) < 2) {
		puts("Usage: hx_os_find_files ROOT [BASENAME_PATTERN]");
		hx_os_string_list_destroy(&arguments);
		return 2;
	}
	status = hx_os_directory_find_files(hx_os_string_list_at(arguments, 1),
		hx_os_string_list_count(arguments) > 2
			? hx_os_string_list_at(arguments, 2)
			: "*",
		print_file, &found, NULL);
	printf("Files: %zu\n", found);
finish:
	if (status != HX_OS_OK)
		fprintf(stderr, "Search failed: %s\n",
			hx_os_status_name(status));
	hx_os_string_list_destroy(&arguments);
	return status == HX_OS_OK ? 0 : 1;
}
