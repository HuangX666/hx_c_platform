#include <hx_os_file.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_file *file = NULL;
	unsigned char bytes[4];
	size_t count = 0, i;
	hx_os_status status;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <file>\n", argv[0]);

		return 1;
	}

	status = hx_os_file_open(NULL, argv[1], HX_OS_FILE_READ, &file, NULL);
	if (status == HX_OS_OK)
		status = hx_os_file_read_at(
			file, 4, bytes, sizeof(bytes), &count, NULL);

	(void)hx_os_file_close(&file, NULL);
	if (status != HX_OS_OK && status != HX_OS_EOF) {
		fprintf(stderr, "Read at byte 4: %s\n",
			hx_os_status_name(status));

		return 1;
	}

	if (!count)
		puts("No bytes at offset 4 (EOF).");

	for (i = 0; i < count; ++i)
		printf("%02x%s", (unsigned)bytes[i],
			i + 1 < count ? " " : "\n");

	return 0;
}
