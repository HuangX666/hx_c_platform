#ifndef HX_C_PLATFORM_DUPLEX_PRIVATE_H
#define HX_C_PLATFORM_DUPLEX_PRIVATE_H

#include "net_private.h"
#include "file_private.h"

#define HX_OS_I_FILE_SOCKET 2

hx_os_status hx_os_i_duplex_pair(hx_os_context *context,
	hx_os_deadline deadline, hx_os_file **first, hx_os_file **second,
	hx_os_error *error);

hx_os_status hx_os_i_duplex_shutdown(
	hx_os_file *file, int direction, hx_os_error *error);
#if defined(_WIN32)
typedef struct hx_os_i_duplex_file {
	hx_os_file base;
	SOCKET native;
} hx_os_i_duplex_file;

hx_os_status hx_os_i_duplex_read(hx_os_file *file, void *data, size_t bytes,
	size_t *completed, hx_os_error *error);

hx_os_status hx_os_i_duplex_write(hx_os_file *file, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error);

hx_os_status hx_os_i_duplex_close(hx_os_file **file, hx_os_error *error);
#endif
#endif
