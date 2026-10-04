#ifndef HX_C_PLATFORM_FILE_EXTRA_PRIVATE_H
#define HX_C_PLATFORM_FILE_EXTRA_PRIVATE_H

#include "file_private.h"
#include "hx_os_file_extra.h"

hx_os_status hx_os_i_file_lock(hx_os_file *file, uint64_t offset,
	uint64_t bytes, int shared, int unlock, hx_os_error *error);

hx_os_status hx_os_i_file_set_times(const char *path,
	const hx_os_file_times *times, int follow, hx_os_error *error);

hx_os_status hx_os_i_path_permissions(
	const char *path, unsigned permissions, int follow, hx_os_error *error);
#endif
