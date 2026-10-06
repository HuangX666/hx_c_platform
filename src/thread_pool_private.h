#ifndef HX_C_PLATFORM_THREAD_POOL_PRIVATE_H
#define HX_C_PLATFORM_THREAD_POOL_PRIVATE_H

#include "internal.h"

typedef struct hx_os_delayed_task {
	hx_os_mutex *mutex;
	hx_os_thread_pool *pool;
	hx_os_deadline due;
	hx_os_status failure;
} hx_os_delayed_task;

struct hx_os_task {
	hx_os_atomic references;
	hx_os_atomic state;
	hx_os_atomic cancelled;
	hx_os_event *finished;
	hx_os_task_fn function;
	hx_os_task_cleanup cleanup;
	void *user;
	hx_os_status result;
	hx_os_thread_pool *pool;
	hx_os_task *next;
	hx_os_task *pending_previous;
	hx_os_task *pending_next;
	uint64_t sequence;
#if HX_OS_ENABLE_DELAYED_TASKS
	hx_os_delayed_task *delayed;
#endif
};

struct hx_os_thread_pool {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_condition *completion;
	hx_os_thread **threads;
	size_t thread_count;
	size_t capacity;
	size_t queued;
	size_t submitting;
	size_t waiters;
	uint64_t accepted;
	hx_os_task *pending_first;
	hx_os_task *pending_last;
	hx_os_task *first;
	hx_os_task *last;
#if HX_OS_ENABLE_DELAYED_TASKS
	hx_os_task *delayed;
#endif
	hx_os_atomic close_claim;
	int closing;
	int joined;
};
#endif
