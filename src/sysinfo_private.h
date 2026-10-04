#ifndef HX_C_PLATFORM_SYSINFO_PRIVATE_H
#define HX_C_PLATFORM_SYSINFO_PRIVATE_H

#include "internal.h"
#include "hx_os_sysinfo.h"

enum {
	HX_OS_SYS_TOTAL,
	HX_OS_SYS_AVAILABLE,
	HX_OS_SYS_PHYSICAL,
	HX_OS_SYS_LOGICAL,
	HX_OS_SYS_PAGE
};

hx_os_status hx_os_i_sys_value(int field, uint64_t *out, hx_os_error *error);

hx_os_status hx_os_i_sys_disk(
	const char *path, hx_os_sys_disk_space_info *out, hx_os_error *error);

hx_os_status hx_os_i_sys_memory(
	hx_os_sys_proc_memory_info *out, hx_os_error *error);

hx_os_status hx_os_i_sys_version(
	int numbers[3], char text[256], hx_os_error *error);

hx_os_status hx_os_i_sys_cpu(uint64_t *nanoseconds, hx_os_error *error);

#endif
