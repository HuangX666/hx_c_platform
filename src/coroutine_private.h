#ifndef HX_C_PLATFORM_COROUTINE_PRIVATE_H
#define HX_C_PLATFORM_COROUTINE_PRIVATE_H

#include "coroutine_native.h"
#include "hx_os_coroutine.h"

typedef struct hx_os_coro_worker hx_os_coro_worker;
typedef struct hx_os_coro_waiter hx_os_coro_waiter;
typedef struct hx_os_coro_io_state hx_os_coro_io_state;
#if HX_OS_ENABLE_PROFILER
typedef struct hx_os_coro_local hx_os_coro_local;
#endif

enum { HX_OS_CO_READY, HX_OS_CO_RUNNING, HX_OS_CO_WAITING, HX_OS_CO_FINISHED };
enum {
	HX_OS_CO_WAIT_NONE,
	HX_OS_CO_WAIT_YIELD,
	HX_OS_CO_WAIT_SLEEP,
	HX_OS_CO_WAIT_TASK,
	HX_OS_CO_WAIT_CHANNEL,
	HX_OS_CO_WAIT_IO
};

typedef struct hx_os_coro_execution {
	void *signal;
	void *profiler;
	void *hash;
} hx_os_coro_execution;

struct hx_os_coro {
	hx_os_atomic references;
	hx_os_atomic cancelled;
	hx_os_atomic finished;
	hx_os_coro_runtime *runtime;
	hx_os_coro_worker *worker;
	hx_os_coro *previous;
	hx_os_coro *next;
	hx_os_coro *ready_next;
	hx_os_coro *joiners;
	hx_os_coro *join_next;
	hx_os_coro *join_previous;
	hx_os_coro *target;
	hx_os_coro_fn function;
	hx_os_coro_cleanup_fn cleanup;
	void *user;
	hx_os_i_coro_context *context;
	hx_os_coro_execution execution;
	hx_os_error result;
	hx_os_error wait_result;
	hx_os_coro_waiter *cases;
	void *io;
#if HX_OS_ENABLE_PROFILER
	hx_os_coro_local *locals;
#endif
	hx_os_deadline deadline;
	size_t timer_index;
	size_t stack_cost;
	size_t case_count;
	size_t selected;
	size_t bias;
	int kind;
	int state;
	int started;
	int returning;
	int wait_kind;
	int wait_done;
	int preparing;
};

struct hx_os_coro_worker {
	hx_os_coro_runtime *runtime;
	hx_os_thread *thread;
	hx_os_coro *first;
	hx_os_coro *last;
	hx_os_i_coro_context root;
	hx_os_coro_execution execution;
	int root_ready;
};

struct hx_os_coro_runtime {
	hx_os_atomic references;
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_coro_options options;
	hx_os_coro_worker *workers;
	hx_os_coro **timers;
	hx_os_coro *first;
	hx_os_coro_channel *channels;
	hx_os_coro_io_state *io;
	size_t worker_count;
	size_t next_worker;
	size_t timer_count;
	size_t active;
	size_t objects;
	size_t stack_cost;
	size_t stack_bytes;
	size_t channel_count;
	size_t channel_bytes;
	size_t preparing;
	int started;
	int closing;
	int joining;
	int closed;
};

/* The runtime mutex protects scheduler/wait lists. No user callback runs with
 * it held. Wait completion never resumes a coroutine on a foreign worker.
 */
void hx_os_i_coro_runtime_retain(hx_os_coro_runtime *runtime);
void hx_os_i_coro_runtime_release(hx_os_coro_runtime *runtime);
void hx_os_i_coro_enqueue(hx_os_coro *coroutine);
void hx_os_i_coro_finish_wait(hx_os_coro *coroutine, const hx_os_error *result);
void hx_os_i_coro_cancel_wait(hx_os_coro *coroutine, hx_os_status status);
hx_os_status hx_os_i_coro_begin_wait(hx_os_coro *coroutine, int kind,
	hx_os_deadline deadline, hx_os_error *error);
hx_os_status hx_os_i_coro_suspend(hx_os_coro *coroutine, hx_os_error *error);
hx_os_status hx_os_i_coro_consume(hx_os_coro *coroutine, hx_os_error *error);
int hx_os_i_coro_wait_ready(hx_os_coro *coroutine, int kind);
void hx_os_i_coro_clear_wait(hx_os_coro *coroutine);
void hx_os_i_coro_channel_unlink(hx_os_coro *coroutine);
void hx_os_i_coro_channel_dispose_wait(hx_os_coro *coroutine);
void hx_os_i_coro_io_cancel(hx_os_coro *coroutine);
void hx_os_i_coro_io_dispose_wait(hx_os_coro *coroutine);
hx_os_status hx_os_i_coro_io_create(
	hx_os_coro_runtime *runtime, hx_os_error *error);
hx_os_status hx_os_i_coro_io_close(hx_os_coro_runtime *runtime,
	hx_os_deadline deadline, hx_os_error *error);
void hx_os_i_coro_io_destroy(hx_os_coro_runtime *runtime);
int hx_os_i_coro_io_inside(void);
enum { HX_OS_CO_DIAL = 100, HX_OS_CO_LISTEN, HX_OS_CO_PIPE };
hx_os_status hx_os_i_coro_io_call(hx_os_coro_stream *stream, int kind,
	const hx_os_operation *input, int backlog, int reuse,
	hx_os_coro_stream **output, hx_os_coro_stream **second,
	hx_os_address_list **addresses, hx_os_completion *completion,
	hx_os_error *error);

#endif
