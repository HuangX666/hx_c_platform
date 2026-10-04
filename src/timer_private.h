#ifndef HX_C_PLATFORM_TIMER_PRIVATE_H
#define HX_C_PLATFORM_TIMER_PRIVATE_H

#include "internal.h"
#include "hx_os_timer.h"

struct hx_os_timer {
	hx_os_loop *loop;
	hx_os_request *request;
	hx_os_timer *next;
	hx_os_timer_fn function;
	hx_os_timer_cleanup_fn cleanup;
	void *user;
	uint64_t interval;
	int close_running;
};

size_t hx_os_i_timer_step(hx_os_request *request);
#endif
