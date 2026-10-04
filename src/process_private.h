#ifndef HX_C_PLATFORM_PROCESS_PRIVATE_H
#define HX_C_PLATFORM_PROCESS_PRIVATE_H

#include "system_private.h"
#include "file_private.h"
#include "hx_os_process.h"

struct hx_os_process {
	hx_os_process_result result;
	uint64_t identifier;
#if defined(_WIN32)
	HANDLE native;
#else
	int native;
#endif
	int finished;
};

hx_os_status hx_os_i_process_spawn(hx_os_context *context, const char *path,
	const hx_os_string_list *arguments,
	const hx_os_string_list *environment,
	const hx_os_process_options *options, hx_os_process *process,
	hx_os_error *error);

hx_os_status hx_os_i_process_poll(hx_os_process *process, hx_os_error *error);

hx_os_status hx_os_i_process_dispose(
	hx_os_process *process, hx_os_error *error);
#endif
