#include "pipe_private.h"
#include "file_private.h"
#if HX_OS_ENABLE_LOOP
#include "loop_private.h"
#endif
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

hx_os_status hx_os_i_pipe_pair(int read_async, int write_async,
	hx_os_i_pipe_handle handles[2], hx_os_error *error)
{
	int pair[2], code = 0, i;
#if defined(__linux__)
	if (pipe2(pair, O_CLOEXEC))
		return hx_os_i_errno(error, errno);
#else
	if (pipe(pair))
		return hx_os_i_errno(error, errno);
#endif
	for (i = 0; i < 2; ++i) {
#if !defined(__linux__)
		if (fcntl(pair[i], F_SETFD, FD_CLOEXEC)) {
			code = errno;
			break;
		}
#endif
		if ((i ? write_async : read_async) &&
			fcntl(pair[i], F_SETFL, O_NONBLOCK)) {
			code = errno;
			break;
		}
	}

	if (code) {
		(void)close(pair[0]);
		(void)close(pair[1]);

		return hx_os_i_errno(error, code);
	}

	handles[0] = pair[0];
	handles[1] = pair[1];

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pipe_file(hx_os_context *context, int native,
	hx_os_file **out, hx_os_error *error)
{
	hx_os_file *file = hx_os_i_zero(context, sizeof(*file));
	if (!file)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	file->descriptor = native;
	file->pipe = 1;
	*out = file;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_pipe_dispose(int handle, hx_os_error *error)
{
	return hx_os_i_errno(error, close(handle) ? errno : 0);
}

#if HX_OS_ENABLE_LOOP
void hx_os_i_pipe_step(hx_os_request *request)
{
	hx_os_pipe *pipe = request->pipe;
	hx_os_operation *op = &request->operation;
	hx_os_status status;
	size_t count = 0, remaining = op->bytes - request->completion.bytes;

	if (request->stop_status || !remaining) {
		request->ready = 1;

		return;
	}

	if (request->started && !(pipe->source.ready & request->directions))
		return;

	pipe->source.ready &= ~request->directions;
	request->started = 1;
	if (remaining > INT_MAX)
		remaining = INT_MAX;

	if (op->kind == HX_OS_I_OP_PIPE_READ) {
		ssize_t result;

		do {
			result = read(pipe->native, op->data, remaining);
		} while (result < 0 && errno == EINTR);

		status = result < 0
			? hx_os_i_errno(&request->completion.error, errno)
			: hx_os_i_error(&request->completion.error,
				  result ? HX_OS_OK : HX_OS_EOF, 0, 0);
		if (result > 0)
			count = (size_t)result;
	}
	else {
		status = hx_os_i_pipe_write(pipe->native,
			(char *)op->data + request->completion.bytes, remaining,
			&count, &request->completion.error);
		if (status == HX_OS_OK && !count)
			status = hx_os_i_error(&request->completion.error,
				HX_OS_IO_ERROR, 0, 0);
	}

	request->completion.bytes += count;
	if (status == HX_OS_WOULD_BLOCK ||
		(status == HX_OS_OK && op->kind == HX_OS_I_OP_PIPE_WRITE &&
			request->completion.bytes < op->bytes)) {
		status = hx_os_i_pipe_refresh(pipe, &request->completion.error);
		if (status == HX_OS_OK)
			return;
	}

	request->ready = 1;
}
#endif
