#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_temp *file = NULL;
	size_t written = 0;
	hx_os_status status =
		hx_os_temp_create_file(NULL, NULL, "device", &file, NULL);

	if (status == HX_OS_OK) {
		printf("Temporary path: %s\n", hx_os_temp_path(file));
		status = hx_os_file_write_all(hx_os_temp_file(file),
			"{\"device\":1}\n", 13, &written, NULL);
	}
	if (file) {
		hx_os_status closed = hx_os_temp_close(&file, NULL);

		if (closed != HX_OS_OK) {
			fprintf(stderr, "Cleanup %s: %s\n",
				hx_os_temp_path(file),
				hx_os_status_name(closed));
			return 1;
		}
	}
	if (status == HX_OS_OK)
		puts("Temporary file removed");
	return status == HX_OS_OK ? 0 : 1;
}
