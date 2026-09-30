#include "loop_private.h"
#include "thread_private.h"
#include "file_private.h"
#include "vm_private.h"
#include <stdio.h>

#define HX_OS_LAYOUT(type)                                                     \
	do {                                                                   \
		typedef struct hx_os_layout_##type {                           \
			char prefix;                                           \
			type value;                                            \
		} hx_os_layout_##type;                                         \
		printf("%s|%zu|%zu\n", #type, sizeof(type),                    \
			offsetof(hx_os_layout_##type, value));                 \
	} while (0)

int main(void)
{
	HX_OS_LAYOUT(hx_string);
	HX_OS_LAYOUT(hx_os_error);
	HX_OS_LAYOUT(hx_os_operation);
	HX_OS_LAYOUT(hx_os_pipe_operation);
	HX_OS_LAYOUT(hx_os_thread_options);
	HX_OS_LAYOUT(hx_os_file_info_extended);
	HX_OS_LAYOUT(hx_os_thread);
	HX_OS_LAYOUT(hx_os_socket);
	HX_OS_LAYOUT(hx_os_file);
	HX_OS_LAYOUT(hx_os_loop);
	HX_OS_LAYOUT(hx_os_request);
	HX_OS_LAYOUT(hx_os_pipe);
	HX_OS_LAYOUT(hx_os_timer);
	HX_OS_LAYOUT(hx_os_pages);
	HX_OS_LAYOUT(hx_os_file_map);
	return 0;
}
