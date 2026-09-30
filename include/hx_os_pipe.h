#ifndef HX_C_PLATFORM_PIPE_H
#define HX_C_PLATFORM_PIPE_H

#include "hx_os_file.h"
#include "hx_os_loop.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_pipe hx_os_pipe;
enum { HX_OS_PIPE_READ, HX_OS_PIPE_WRITE };

/*
 * Blocking synchronous byte streams, non-inheritable by default. Use file
 * read/write/close. No seeking, size, truncation or persistence operations.
 * Closing every writer produces EOF after buffered bytes are consumed.
 * Writing after every reader closes returns CLOSED without raising SIGPIPE.
 * Exclude concurrent close; use async endpoints for deadlines/cancellation.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_pair_anonymous(
	hx_os_context *context, hx_os_file **read_end, hx_os_file **write_end,
	hx_os_error *error);

/* Dedicated asynchronous endpoints, never usable as synchronous files. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_pair_async(hx_os_context *context,
	hx_os_pipe **read_end, hx_os_pipe **write_end, hx_os_error *error);
/* Parent is async, child is synchronous; neither is initially inheritable. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_pair_child(hx_os_context *context,
	int parent_reads, hx_os_pipe **parent, hx_os_file **child,
	hx_os_error *error);

typedef struct hx_os_pipe_operation {
	uint32_t size;
	uint32_t version;
	hx_os_pipe *pipe;
	void *data;
	size_t bytes;
	hx_os_deadline deadline;
	int kind;
	int send_mode;
} hx_os_pipe_operation;
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_operation_init(
	hx_os_pipe_operation *operation, size_t capacity, int kind);
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_submit_pipe(hx_os_loop *loop,
	const hx_os_pipe_operation *operation, hx_os_completion_fn completion,
	void *user, hx_os_request **out, hx_os_error *error);
/* Read completes with any bytes or EOF. Write completes all bytes or reports
 * partial progress on failure/cancel. Default write copies; explicit BORROW
 * and read storage remain valid until callback returns. Both share the
 * loop's request/byte budget. One pending operation per endpoint.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_read_async(hx_os_loop *loop,
	hx_os_pipe *pipe, void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_write_async(hx_os_loop *loop,
	hx_os_pipe *pipe, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_completion_fn completion, void *user,
	hx_os_request **out, hx_os_error *error);
/*
 * On first submit the endpoint belongs to that loop/owner until close.
 * Close rejects submissions, cancels its request and drives native retirement
 * on the owner thread, possibly dispatching other loop callbacks. Timeout
 * keeps *pipe valid for retry. Never frees native I/O still using a buffer.
 * A callback cannot wait for an unfinished operation on its own loop.
 * Close endpoints before destroying the loop. The final native close consumes
 * it even on a close error, matching file close. Timeout/BUSY retain it.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_close_until(
	hx_os_pipe **pipe, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
