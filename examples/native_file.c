#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include <hx_os.h>
#include <inttypes.h>
#include <stdio.h>

static int inspect_file(const char *path)
{
	hx_os_file *file = NULL;
	uintptr_t native;
	uint64_t bytes = 0;
	hx_os_status status;
	int result = 1;

	status = hx_os_file_open(NULL, path, HX_OS_FILE_READ, &file, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Open: %s\n", hx_os_status_name(status));

		return 1;
	}

	status = hx_os_file_native_borrow(file, &native, NULL);
	if (status == HX_OS_OK) {
#if defined(_WIN32)
		LARGE_INTEGER size;

		if (GetFileSizeEx((HANDLE)native, &size)) {
			bytes = (uint64_t)size.QuadPart;
			result = 0;
		}
#else
		struct stat info;

		if (fstat((int)native, &info) == 0 && info.st_size >= 0) {
			bytes = (uint64_t)info.st_size;
			result = 0;
		}
#endif
	}
	else
		fprintf(stderr, "Borrow: %s\n", hx_os_status_name(status));

	// The library still owns the file; never close the borrowed native
	// value.
	status = hx_os_file_close(&file, NULL);
	if (status != HX_OS_OK || result) {
		fputs("Native inspection or file close failed.\n", stderr);

		return 1;
	}

	printf("Native query: %" PRIu64 " bytes\n", bytes);

	return 0;
}

int main(void)
{
	hx_os_string_list *arguments = NULL;
	hx_os_status status = hx_os_process_arguments(NULL, &arguments, NULL);
	int result = 0;

	if (status != HX_OS_OK) {
		fprintf(stderr, "Arguments: %s\n", hx_os_status_name(status));

		return 1;
	}
	if (hx_os_string_list_count(arguments) < 2)
		puts("Usage: hx_os_native_file <UTF-8 file path>");
	else
		result = inspect_file(hx_os_string_list_at(arguments, 1));

	hx_os_string_list_destroy(&arguments);

	return result;
}
