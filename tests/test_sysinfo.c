#include "hx_os.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

int main(void)
{
	hx_os_sys_disk_space_info disk = HX_OS_SYS_DISK_SPACE_INFO_INIT;
	hx_os_sys_proc_memory_info memory = HX_OS_SYS_PROC_MEMORY_INFO_INIT;
	hx_os_context *context = NULL;
	hx_os_cpu_usage *usage = NULL;
	hx_string version = HX_STRING_INIT;
	hx_os_error error;
	uint64_t total, available;
	size_t page, required;
	int logical, physical, major, minor, patch;
	double percent = -1.0;
	char text[256];
	hx_os_status status;

	CHECK(hx_os_version_string(NULL, 0, &required) == HX_OS_OUTPUT_FULL);
	CHECK(required > 1 && required < sizeof(text));
	CHECK(hx_os_version_string(text, sizeof(text), &required) == HX_OS_OK);
	CHECK(!strcmp(text, "0.1.0"));
	CHECK(hx_os_abi_version() == HX_OS_ABI_VERSION);

	CHECK(hx_os_sys_total_memory(NULL, &error) == HX_OS_INVALID);
	CHECK(error.status == HX_OS_INVALID);
	CHECK(hx_os_sys_disk_space(NULL, &disk, NULL) == HX_OS_INVALID);
	CHECK(hx_os_sys_disk_space("\xff", &disk, NULL) ==
		HX_OS_INVALID_ENCODING);

	if (!(hx_os_capabilities() & HX_OS_CAP_SYSINFO)) {
		CHECK(hx_os_sys_total_memory(&total, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_available_memory(&available, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_logical_cores(&logical, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_physical_cores(&physical, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_page_size(&page, NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_disk_space(".", &disk, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_proc_memory(&memory, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_version(text, sizeof(text), &required, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_version_numbers(&major, &minor, &patch, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_sys_version_string(&version, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_cpu_usage_create(NULL, &usage, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!usage);

		return 0;
	}

	CHECK(hx_os_sys_total_memory(&total, &error) == HX_OS_OK && total);
	CHECK(hx_os_sys_available_memory(&available, NULL) == HX_OS_OK);
	CHECK(available <= total);

	CHECK(hx_os_sys_logical_cores(&logical, NULL) == HX_OS_OK &&
		logical > 0);

	status = hx_os_sys_physical_cores(&physical, NULL);
	CHECK(status == HX_OS_NOT_SUPPORTED ||
		(status == HX_OS_OK && physical > 0 && physical <= logical));
	CHECK(hx_os_sys_page_size(&page, NULL) == HX_OS_OK && page > 0);

	CHECK(hx_os_sys_disk_space(".", &disk, NULL) == HX_OS_OK);
	CHECK(disk.available_bytes <= disk.free_bytes);
	CHECK(disk.free_bytes <= disk.total_bytes);

	disk.version = 0;
	CHECK(hx_os_sys_disk_space(".", &disk, NULL) == HX_OS_INVALID);
	CHECK(hx_os_sys_proc_memory(&memory, NULL) == HX_OS_OK);
	CHECK(memory.resident_bytes && memory.virtual_bytes);
	CHECK(hx_os_sys_version(NULL, 0, &required, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(hx_os_sys_version(text, 1, &required, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(!text[0]);
	CHECK(hx_os_sys_version(text, sizeof(text), &required, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_sys_version_string(&version, NULL) == HX_OS_OK);
	CHECK(!strcmp(text, hx_string_c_str(&version)));
	CHECK(hx_os_sys_version_numbers(&major, &minor, &patch, NULL) ==
		HX_OS_OK);
	CHECK(major > 0 && minor >= 0 && patch >= 0);

	hx_string_destroy(&version);
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_cpu_usage_create(context, &usage, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(10), NULL) == HX_OS_OK);
	CHECK(hx_os_cpu_usage_query(usage, &percent, NULL) == HX_OS_OK);
	CHECK(isfinite(percent) && percent >= 0);

	hx_os_cpu_usage_destroy(&usage);
	CHECK(!usage && hx_os_context_allocations(context) == 0);

	hx_os_cpu_usage_destroy(&usage);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	puts("system information contracts passed");

	return 0;
}
