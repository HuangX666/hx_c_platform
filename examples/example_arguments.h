#ifndef HX_C_PLATFORM_EXAMPLE_ARGUMENTS_H
#define HX_C_PLATFORM_EXAMPLE_ARGUMENTS_H

#include <hx_os.h>
#include <stdio.h>
#include <string.h>

static int hx_example_main(int argc, const char *const *argv);

int main(int argc, char **argv)
{
	hx_os_string_list *arguments = NULL;
	const char *const *values = (const char *const *)argv;
	hx_os_status status;
	int result, i;
	size_t invalid;

	if (hx_os_capabilities() & HX_OS_CAP_SYSTEM_UTILS) {
		status = hx_os_process_arguments(NULL, &arguments, NULL);
		if (status != HX_OS_OK) {
			fprintf(stderr, "Arguments: %s\n",
				hx_os_status_name(status));

			return 1;
		}

		argc = (int)hx_os_string_list_count(arguments);
		values = hx_os_string_list_data(arguments);
	}
	else {
		/* A trimmed build requires the host to provide UTF-8 argv.
		 * Windows narrow CRT argv cannot promise Unicode fidelity.
		 */
		for (i = 0; i < argc; ++i)
			if (hx_os_text_validate_utf8(argv[i], strlen(argv[i]),
				    &invalid) != HX_OS_OK)
				return 1;
	}

	result = hx_example_main(argc, values);

	hx_os_string_list_destroy(&arguments);

	return result;
}
#endif
