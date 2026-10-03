#include "pipe_private.h"
#include "file_private.h"
#if HX_OS_ENABLE_LOOP
#include "loop_private.h"
#endif

#if HX_OS_ENABLE_PIPE
static hx_os_status hx_os_pipe_create_pair(hx_os_context *context,
	hx_os_pipe **read_async, hx_os_pipe **write_async,
	hx_os_file **read_sync, hx_os_file **write_sync, hx_os_error *error)
{
	hx_os_i_pipe_handle handles[2] = {HX_OS_I_BAD_PIPE, HX_OS_I_BAD_PIPE};
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_status status;
	if (read_async) {
		*read_async = NULL;
		reader = hx_os_i_zero(context, sizeof(*reader));
		if (!reader)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}
	if (write_async) {
		*write_async = NULL;
		writer = hx_os_i_zero(context, sizeof(*writer));
		if (!writer) {
			hx_os_memory_free(reader);
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		}
	}
	status = hx_os_i_pipe_pair(
		read_async != NULL, write_async != NULL, handles, error);
	if (status == HX_OS_OK && read_sync)
		status = hx_os_i_pipe_file(
			context, handles[0], read_sync, error);
	if (status == HX_OS_OK && write_sync)
		status = hx_os_i_pipe_file(
			context, handles[1], write_sync, error);
	if (status != HX_OS_OK) {
		if (read_sync && *read_sync) {
			(void)hx_os_file_close(read_sync, NULL);
			handles[0] = HX_OS_I_BAD_PIPE;
		}
		if (handles[0] != HX_OS_I_BAD_PIPE)
			(void)hx_os_i_pipe_dispose(handles[0], NULL);
		if (handles[1] != HX_OS_I_BAD_PIPE)
			(void)hx_os_i_pipe_dispose(handles[1], NULL);
		hx_os_memory_free(reader);
		hx_os_memory_free(writer);
		return status;
	}
	if (read_sync)
		(*read_sync)->flags = HX_OS_FILE_READ;
	if (write_sync)
		(*write_sync)->flags = HX_OS_FILE_WRITE;
	if (reader) {
		reader->native = handles[0];
		reader->direction = HX_OS_WAIT_READ;
		*read_async = reader;
	}
	if (writer) {
		writer->native = handles[1];
		writer->direction = HX_OS_WAIT_WRITE;
		*write_async = writer;
	}
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
#endif

hx_os_status hx_os_pipe_pair_anonymous(hx_os_context *context,
	hx_os_file **read_end, hx_os_file **write_end, hx_os_error *error)
{
	if (!read_end || !write_end || read_end == write_end)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*read_end = NULL;
	*write_end = NULL;
#if HX_OS_ENABLE_PIPE
	return hx_os_pipe_create_pair(
		context, NULL, NULL, read_end, write_end, error);
#else
	(void)context;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pipe_pair_async(hx_os_context *context,
	hx_os_pipe **read_end, hx_os_pipe **write_end, hx_os_error *error)
{
	if (!read_end || !write_end || read_end == write_end)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*read_end = NULL;
	*write_end = NULL;
#if HX_OS_ENABLE_PIPE && HX_OS_ENABLE_LOOP
	return hx_os_pipe_create_pair(
		context, read_end, write_end, NULL, NULL, error);
#else
	(void)context;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pipe_pair_child(hx_os_context *context, int parent_reads,
	hx_os_pipe **parent, hx_os_file **child, hx_os_error *error)
{
	if (!parent || !child || (parent_reads != 0 && parent_reads != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*parent = NULL;
	*child = NULL;
#if HX_OS_ENABLE_PIPE && HX_OS_ENABLE_LOOP
	return hx_os_pipe_create_pair(context, parent_reads ? parent : NULL,
		parent_reads ? NULL : parent, parent_reads ? NULL : child,
		parent_reads ? child : NULL, error);
#else
	(void)context;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pipe_operation_init(
	hx_os_pipe_operation *op, size_t capacity, int kind)
{
	if (!op || capacity < sizeof(*op) ||
		(kind != HX_OS_PIPE_READ && kind != HX_OS_PIPE_WRITE))
		return HX_OS_INVALID;
	memset(op, 0, sizeof(*op));
	op->size = sizeof(*op);
	op->version = 1;
	op->kind = kind;
	op->deadline = HX_OS_DEADLINE_INFINITE;
	op->send_mode = HX_OS_SEND_COPY;
	return HX_OS_OK;
}

hx_os_status hx_os_loop_submit_pipe(hx_os_loop *loop,
	const hx_os_pipe_operation *op, hx_os_completion_fn completion,
	void *user, hx_os_request **out, hx_os_error *error)
{
#if HX_OS_ENABLE_PIPE && HX_OS_ENABLE_LOOP
	hx_os_operation native;
	if (out)
		*out = NULL;
	if (!op || op->size < sizeof(*op) || op->version != 1 || !op->pipe ||
		(!op->data && op->bytes) ||
		(op->kind != HX_OS_PIPE_READ && op->kind != HX_OS_PIPE_WRITE) ||
		(op->send_mode != HX_OS_SEND_COPY &&
			op->send_mode != HX_OS_SEND_BORROW))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	hx_os_operation_init(&native,
		op->kind == HX_OS_PIPE_READ ? HX_OS_I_OP_PIPE_READ
					    : HX_OS_I_OP_PIPE_WRITE);
	native.data = op->data;
	native.bytes = op->bytes;
	native.send_mode = op->send_mode;
	native.deadline = op->deadline;
	return hx_os_i_loop_submit(
		loop, &native, op->pipe, NULL, completion, user, out, error);
#else
	(void)loop;
	(void)op;
	(void)completion;
	(void)user;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_pipe_read_async(hx_os_loop *loop, hx_os_pipe *pipe,
	void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error)
{
	hx_os_pipe_operation op;
	(void)hx_os_pipe_operation_init(&op, sizeof(op), HX_OS_PIPE_READ);
	op.pipe = pipe;
	op.data = data;
	op.bytes = bytes;
	op.deadline = deadline;
	return hx_os_loop_submit_pipe(loop, &op, completion, user, out, error);
}

hx_os_status hx_os_pipe_write_async(hx_os_loop *loop, hx_os_pipe *pipe,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_completion_fn completion, void *user, hx_os_request **out,
	hx_os_error *error)
{
	hx_os_pipe_operation op;
	(void)hx_os_pipe_operation_init(&op, sizeof(op), HX_OS_PIPE_WRITE);
	op.pipe = pipe;
	op.data = (void *)data;
	op.bytes = bytes;
	op.deadline = deadline;
	return hx_os_loop_submit_pipe(loop, &op, completion, user, out, error);
}

#if HX_OS_ENABLE_LOOP
hx_os_status hx_os_i_pipe_attach(
	hx_os_loop *loop, hx_os_pipe *pipe, hx_os_error *error)
{
#if defined(_WIN32)
	/* Duplicate pipe handles share the underlying IOCP association. The
	 * owning endpoint attaches its reader before submitting writer I/O.
	 */
	if (pipe->association)
		return hx_os_i_error(error,
			pipe->association->loop == loop ? HX_OS_OK
							: HX_OS_INVALID,
			0, 0);
	if (!CreateIoCompletionPort(pipe->native, loop->port, 0, 0))
		return hx_os_i_win32(error, GetLastError());
#else
	(void)loop;
	(void)pipe;
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pipe_refresh(hx_os_pipe *pipe, hx_os_error *error)
{
#if !defined(_WIN32)
	return hx_os_i_loop_watch(pipe->loop, pipe->native, &pipe->source,
		pipe->request ? pipe->direction : 0, error);
#else
	(void)pipe;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#endif
}

#if !HX_OS_ENABLE_PIPE
void hx_os_i_pipe_step(hx_os_request *request)
{
	(void)hx_os_i_error(
		&request->completion.error, HX_OS_NOT_SUPPORTED, 0, 0);
	request->ready = 1;
}
#endif
#endif

hx_os_status hx_os_pipe_close_until(
	hx_os_pipe **pipe, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_PIPE && HX_OS_ENABLE_LOOP
	hx_os_pipe *value, **cursor;
	hx_os_status status;
	hx_os_loop *loop;
	if (!pipe)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*pipe)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	value = *pipe;
	loop = value->loop;
	if (loop && loop->owner != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (value->close_running)
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
	value->closing = 1;
	if (value->request) {
		hx_os_request_cancel(value->request);
		if (loop->running)
			return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);
		value->close_running = 1;
		while (value->request) {
			status = hx_os_loop_poll_until(loop, deadline, error);
			if (status != HX_OS_OK) {
				value->close_running = 0;
				return status;
			}
			if (value->request &&
				hx_os_deadline_expired(deadline)) {
				value->close_running = 0;
				return hx_os_i_error(
					error, HX_OS_TIMEOUT, 0, 0);
			}
		}
		value->close_running = 0;
	}
	if (loop) {
		status = hx_os_i_pipe_refresh(value, error);
		if (status != HX_OS_OK)
			return status;
		for (cursor = &loop->pipes; *cursor; cursor = &(*cursor)->next)
			if (*cursor == value) {
				*cursor = value->next;
				break;
			}
	}
	status = hx_os_i_pipe_dispose(value->native, error);
	hx_os_memory_free(value);
	*pipe = NULL;
	return status;
#else
	(void)pipe;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
