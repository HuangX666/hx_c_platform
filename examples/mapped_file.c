#include <hx_os.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_file_map *map = NULL;
	hx_os_error error;
	hx_os_status status;
	if (argc != 2)
		return 1;
	status = hx_os_file_map_open(argv[1], HX_OS_MAP_READ, &map, &error);
	if (status == HX_OS_EOF) {
		puts("Empty file");
		return 0;
	}
	if (status != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(status));
		return 1;
	}
	printf("%zu mapped bytes, CRC32 %08x\n", hx_os_file_map_size(map),
		(unsigned)hx_os_crc32(
			0, hx_os_file_map_view(map), hx_os_file_map_size(map)));
	return hx_os_file_map_destroy(&map, NULL) == HX_OS_OK ? 0 : 1;
}
