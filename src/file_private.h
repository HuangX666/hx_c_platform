#ifndef HX_C_PLATFORM_FILE_PRIVATE_H
#define HX_C_PLATFORM_FILE_PRIVATE_H
#include "internal.h"

#if defined(_WIN32)
#include <windows.h>
struct hx_os_file {
	HANDLE handle;
	int append;
	int pipe;
};
#else
struct hx_os_file {
	int descriptor;
	int pipe;
};
hx_os_status hx_os_i_pipe_write(int descriptor, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error);
#endif
#endif
