#ifndef HX_C_PLATFORM_LOOP_FD_PRIVATE_H
#define HX_C_PLATFORM_LOOP_FD_PRIVATE_H

#include "internal.h"
#include "poll_private.h"
#include "hx_os_loop_fd.h"

struct hx_os_loop_watch {
	hx_os_loop *loop;
	hx_os_request *request;
	hx_os_loop_watch *next;
	hx_os_loop_fd_fn function;
	hx_os_loop_fd_cleanup_fn cleanup;
	void *user;
	hx_os_i_poll_source source;
	int descriptor;
	int events;
	int close_running;
};

hx_os_status hx_os_i_loop_fd_register(
	hx_os_loop_watch *watch, hx_os_error *error);

size_t hx_os_i_loop_fd_step(hx_os_request *request);
#endif
