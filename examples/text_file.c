#include <hx_os.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status;
	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage: text_file <input> [output]\n");
		return 1;
	}
	status = hx_os_file_read_text(argv[1], &text, &error);
	if (status == HX_OS_OK && argc == 3)
		status = hx_os_file_write_text(argv[2], &text, &error);
	if (status == HX_OS_OK) {
		if (fwrite(hx_string_c_str(&text), 1, text.size, stdout) !=
			text.size)
			status = HX_OS_IO_ERROR;
	}
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
