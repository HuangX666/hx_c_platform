#include <hx_os.h>
#include <stdio.h>

static int show_version(const char *path)
{
	hx_os_windows_version_info version;
	hx_string description = HX_STRING_INIT;
	hx_os_status status;

	if (path && hx_os_windows_version_available()) {
		status = hx_os_windows_version_query(
			path, &version, sizeof(version), NULL);
		if (status != HX_OS_OK) {
			fprintf(stderr, "Version: %s\n",
				hx_os_status_name(status));

			return 1;
		}

		printf("File version: %u.%u.%u.%u\n", (unsigned)version.file[0],
			(unsigned)version.file[1], (unsigned)version.file[2],
			(unsigned)version.file[3]);
		status = hx_os_windows_version_string(
			path, "FileDescription", &description, NULL);
		if (status == HX_OS_OK)
			puts(hx_string_c_str(&description));
		else
			printf("Description: %s\n", hx_os_status_name(status));

		hx_string_destroy(&description);
	}

	return 0;
}

int main(void)
{
	hx_os_windows_gpu_info gpu;
	hx_os_string_list *arguments = NULL;
	hx_os_status status;
	uint32_t index;
	int result;

	status = hx_os_process_arguments(NULL, &arguments, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Arguments: %s\n", hx_os_status_name(status));

		return 1;
	}

	result = show_version(hx_os_string_list_at(arguments, 1));
	hx_os_string_list_destroy(&arguments);
	if (result)
		return result;

	if (!hx_os_windows_desktop_available()) {
		puts("Windows desktop helpers are unavailable in this build.");

		return 0;
	}

	for (index = 0; index < 256; ++index) {
		status =
			hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), NULL);
		if (status == HX_OS_NOT_FOUND)
			break;
		if (status != HX_OS_OK) {
			fprintf(stderr, "GPU: %s\n", hx_os_status_name(status));

			return 1;
		}

		printf("GPU %u: %s; LUID %016llx; video memory %llu MiB\n",
			index, gpu.description, (unsigned long long)gpu.luid,
			(unsigned long long)(gpu.dedicated_video_bytes /
				1048576));
	}

	return 0;
}
