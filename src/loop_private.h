#ifndef HX_C_PLATFORM_LOOP_PRIVATE_H
#define HX_C_PLATFORM_LOOP_PRIVATE_H

#include "net_private.h"
#include "hx_os_loop.h"
#include "pipe_private.h"
#include "timer_private.h"
#include "loop_fd_private.h"

#if defined(_WIN32)
#include <mswsock.h>
#endif

struct hx_os_request {
	hx_os_atomic references;
	hx_os_atomic cancelled;
	hx_os_atomic state;
	hx_os_atomic dns_finished;
	hx_os_mutex *mutex;
	hx_os_event *finished;
	hx_os_loop *loop;
	hx_os_request *next;
	hx_os_operation operation;
	hx_os_completion completion;
	hx_os_completion_fn callback;
	void *user;
	void *owned_data;
	char *owned_host;
	hx_os_resolver_options *resolver_options;
	hx_os_socket *accepted;
	hx_os_pipe *pipe;
	hx_os_timer *timer;
	hx_os_loop_watch *fd_watch;
	hx_os_address_list *addresses;
	size_t budget;
	int directions;
	int started;
	int ready;
	hx_os_status stop_status;
#if defined(_WIN32)
	OVERLAPPED overlapped;
	hx_os_deadline accept_retry_after;
	int native_pending;
	int native_done;
	int native_cancelled;
	DWORD native_bytes;
	DWORD native_error;
	DWORD flags;
	struct sockaddr_storage address;
	int address_length;
	char accept_addresses[2 * (sizeof(struct sockaddr_storage) + 16)];
#endif
};

struct hx_os_loop {
	hx_os_network *network;
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_loop_options options;
	hx_os_request *inbox;
	hx_os_request *inbox_last;
	hx_os_request *active;
	hx_os_socket *sockets;
	hx_os_pipe *pipes;
	hx_os_timer *timers;
	hx_os_loop_watch *fd_watches;
	hx_os_thread_pool *dns_pool;
	hx_os_atomic wake_pending;
	size_t pending;
	size_t bytes;
	size_t reserving;
	uint64_t owner;
	int running;
	int closing;
	int closed;
#if defined(_WIN32)
	HANDLE port;
#else
	int port;
	int wake_read;
	int wake_write;
#endif
};

hx_os_status hx_os_i_loop_native_create(hx_os_loop *loop, hx_os_error *error);

void hx_os_i_loop_native_destroy(hx_os_loop *loop);

void hx_os_i_loop_wake(hx_os_loop *loop);

hx_os_status hx_os_i_loop_wait(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error);

hx_os_status hx_os_i_loop_attach(
	hx_os_loop *loop, hx_os_socket *socket, hx_os_error *error);

hx_os_status hx_os_i_loop_refresh(hx_os_socket *socket, hx_os_error *error);

void hx_os_i_loop_step(hx_os_request *request);

hx_os_status hx_os_i_loop_cancel_until(hx_os_loop *loop, hx_os_request *request,
	hx_os_deadline deadline, int *close_running, hx_os_error *error);

hx_os_status hx_os_i_loop_submit(hx_os_loop *loop,
	const hx_os_operation *operation, hx_os_pipe *pipe,
	hx_os_loop_watch *watch, hx_os_completion_fn completion, void *user,
	hx_os_request **out, hx_os_error *error);
#if !defined(_WIN32)
hx_os_status hx_os_i_loop_watch(hx_os_loop *loop, int descriptor,
	hx_os_i_poll_source *source, int wanted, hx_os_error *error);
#endif

#endif
