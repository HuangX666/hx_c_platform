#ifndef HX_C_PLATFORM_PIPE_PRIVATE_H
#define HX_C_PLATFORM_PIPE_PRIVATE_H

#include "internal.h"
#include "hx_os_pipe.h"
#include "poll_private.h"
#if defined(_WIN32)
#include <windows.h>

typedef HANDLE hx_os_i_pipe_handle;
#define HX_OS_I_BAD_PIPE INVALID_HANDLE_VALUE
#else
typedef int hx_os_i_pipe_handle;
#define HX_OS_I_BAD_PIPE (-1)
#endif

struct hx_os_pipe {
	hx_os_loop *loop;
	hx_os_pipe *next;
#if defined(_WIN32)
	hx_os_pipe *association;
#endif
	hx_os_i_pipe_handle native;
	hx_os_request *request;
	int direction;
	int closing;
	int close_running;
	hx_os_i_poll_source source;
};

enum {
	HX_OS_I_OP_PIPE_READ = 1000,
	HX_OS_I_OP_PIPE_WRITE,
	HX_OS_I_OP_PIPE_ACCEPT
};

hx_os_status hx_os_i_pipe_pair(int read_async, int write_async,
	hx_os_i_pipe_handle handles[2], hx_os_error *error);

hx_os_status hx_os_i_pipe_dispose(
	hx_os_i_pipe_handle handle, hx_os_error *error);

hx_os_status hx_os_i_pipe_file(hx_os_context *context,
	hx_os_i_pipe_handle native, hx_os_file **out, hx_os_error *error);

hx_os_status hx_os_i_pipe_attach(
	hx_os_loop *loop, hx_os_pipe *pipe, hx_os_error *error);

hx_os_status hx_os_i_pipe_refresh(hx_os_pipe *pipe, hx_os_error *error);

void hx_os_i_pipe_step(hx_os_request *request);
#endif
