#ifndef HX_C_PLATFORM_FILE_PRIVATE_H
#define HX_C_PLATFORM_FILE_PRIVATE_H
#include "internal.h"

#if defined(_WIN32)
#include <windows.h>
struct hx_os_file {
	HANDLE handle;
	PVOID volatile positioned;
	uint32_t flags;
	int pipe;
};
#else
struct hx_os_file {
	int descriptor;
	int pipe;
	uint32_t flags;
};
hx_os_status hx_os_i_pipe_write(int descriptor, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error);
#endif
hx_os_status hx_os_i_file_at(hx_os_file *file, int writing, uint64_t offset,
	void *data, size_t bytes, size_t *completed, hx_os_error *error);
#endif
