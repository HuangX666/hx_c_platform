#include "coroutine_private.h"

hx_os_status hx_os_coro_dial(const hx_os_address *address,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_error *error)
{
	hx_os_operation operation;

	if (!out || !address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_operation_init(&operation, HX_OS_OP_CONNECT);
	operation.address = *address;
	operation.deadline = deadline;

	return hx_os_i_coro_io_call(NULL, HX_OS_CO_DIAL, &operation, 0, 0, out,
		NULL, NULL, NULL, error);
}

hx_os_status hx_os_coro_listen(const hx_os_address *address, int backlog,
	int reuse_address, hx_os_coro_stream **out, hx_os_address *bound,
	hx_os_error *error)
{
	hx_os_operation operation;
	hx_os_completion completion;
	hx_os_status status;

	if (!out || !address || backlog <= 0)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_operation_init(&operation, HX_OS_OP_POST);
	operation.address = *address;
	operation.deadline = HX_OS_DEADLINE_INFINITE;

	status = hx_os_i_coro_io_call(NULL, HX_OS_CO_LISTEN, &operation,
		backlog, reuse_address, out, NULL, NULL, &completion, error);
	if (status == HX_OS_OK && bound)
		*bound = completion.peer;

	return status;
}

hx_os_status hx_os_coro_accept(hx_os_coro_stream *listener,
	hx_os_deadline deadline, hx_os_coro_stream **out, hx_os_address *peer,
	hx_os_error *error)
{
	hx_os_operation operation;
	hx_os_completion completion;
	hx_os_status status;

	if (!listener || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_operation_init(&operation, HX_OS_OP_ACCEPT);
	operation.deadline = deadline;

	status = hx_os_i_coro_io_call(listener, HX_OS_OP_ACCEPT, &operation, 0,
		0, out, NULL, NULL, &completion, error);
	if (status == HX_OS_OK && peer)
		*peer = completion.peer;

	return status;
}

static hx_os_status hx_os_coro_transfer(hx_os_coro_stream *stream, int kind,
	void *data, size_t bytes, hx_os_deadline deadline, size_t *completed,
	const hx_os_address *destination, hx_os_address *peer,
	hx_os_error *error)
{
	hx_os_operation operation;
	hx_os_completion completion;
	hx_os_status status;

	if (completed)
		*completed = 0;

	if (peer)
		memset(peer, 0, sizeof(*peer));

	if (!stream || (!data && bytes) || !completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	memset(&completion, 0, sizeof(completion));
	hx_os_operation_init(&operation, kind);
	operation.data = data;
	operation.bytes = bytes;
	operation.deadline = deadline;
	if (destination)
		operation.address = *destination;

	status = hx_os_i_coro_io_call(stream, kind, &operation, 0, 0, NULL,
		NULL, NULL, &completion, error);
	*completed = completion.bytes;
	if (peer && (status == HX_OS_OK || status == HX_OS_LIMIT))
		*peer = completion.peer;

	return status;
}

hx_os_status hx_os_coro_read(hx_os_coro_stream *stream, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	return hx_os_coro_transfer(stream, HX_OS_OP_READ, data, bytes, deadline,
		completed, NULL, NULL, error);
}

hx_os_status hx_os_coro_write(hx_os_coro_stream *stream, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	return hx_os_coro_transfer(stream, HX_OS_OP_WRITE, (void *)data, bytes,
		deadline, completed, NULL, NULL, error);
}

hx_os_status hx_os_coro_udp_bind(const hx_os_address *address,
	int reuse_address, hx_os_coro_stream **out, hx_os_address *bound,
	hx_os_error *error)
{
	hx_os_operation operation;
	hx_os_completion completion;
	hx_os_status status;

	if (out)
		*out = NULL;

	if (!out || !address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	hx_os_operation_init(&operation, HX_OS_OP_POST);
	operation.address = *address;

	status = hx_os_i_coro_io_call(NULL, HX_OS_CO_UDP, &operation, 0,
		reuse_address, out, NULL, NULL, &completion, error);
	if (status == HX_OS_OK && bound)
		*bound = completion.peer;

	return status;
}

hx_os_status hx_os_coro_send_to(hx_os_coro_stream *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	if (!peer) {
		if (completed)
			*completed = 0;

		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}

	return hx_os_coro_transfer(socket, HX_OS_OP_SEND_TO, (void *)data,
		bytes, deadline, completed, peer, NULL, error);
}

hx_os_status hx_os_coro_receive_from(hx_os_coro_stream *socket,
	hx_os_address *peer, void *data, size_t capacity,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	return hx_os_coro_transfer(socket, HX_OS_OP_RECEIVE_FROM, data,
		capacity, deadline, completed, NULL, peer, error);
}

hx_os_status hx_os_coro_resolve(const char *host, uint16_t port,
	hx_os_deadline deadline, hx_os_address_list **out, hx_os_error *error)
{
	hx_os_operation operation;

	if (!out || !host)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	hx_os_operation_init(&operation, HX_OS_OP_RESOLVE);
	operation.host = host;
	operation.address.port = port;
	operation.protocol = HX_OS_TCP;
	operation.deadline = deadline;

	return hx_os_i_coro_io_call(NULL, HX_OS_OP_RESOLVE, &operation, 0, 0,
		NULL, NULL, out, NULL, error);
}

hx_os_status hx_os_coro_pipe_pair(hx_os_coro_stream **reader,
	hx_os_coro_stream **writer, hx_os_error *error)
{
	hx_os_operation operation;

	if (!reader || !writer || reader == writer)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*reader = *writer = NULL;
	hx_os_operation_init(&operation, HX_OS_OP_POST);
	operation.deadline = HX_OS_DEADLINE_INFINITE;

	return hx_os_i_coro_io_call(NULL, HX_OS_CO_PIPE, &operation, 0, 0,
		reader, writer, NULL, NULL, error);
}
