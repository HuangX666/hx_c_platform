#include <hx_line_reader.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	hx_os_line_reader *reader = NULL;
	hx_string line = HX_STRING_INIT;
	hx_os_status status;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s UTF-8-file\n", argv[0]);

		return 1;
	}

	status = hx_os_line_reader_open(NULL, argv[1], 0, &reader, NULL);
	while (status == HX_OS_OK) {
		status = hx_os_line_reader_next(reader, &line, NULL);
		if (status == HX_OS_OK) {
			(void)fwrite(hx_string_c_str(&line), 1,
				hx_string_size_bytes(&line), stdout);
			(void)fputc('\n', stdout);
		}
	}
	if (status != HX_OS_EOF)
		fprintf(stderr, "Read failed: %s\n", hx_os_status_name(status));

	{
		hx_os_status closed = hx_os_line_reader_close(&reader, NULL);

		if (status == HX_OS_EOF)
			status = closed;
	}
	hx_string_destroy(&line);

	return status == HX_OS_OK ? 0 : 1;
}
