#include "loop_private.h"

hx_os_status hx_os_i_loop_attach(
	hx_os_loop *loop, hx_os_socket *socket, hx_os_error *error)
{
	(void)loop;
	(void)socket;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

void hx_os_i_loop_step(hx_os_request *request)
{
#if HX_OS_ENABLE_NETWORK
	hx_os_operation *op = &request->operation;
	hx_os_socket *socket = op->socket;
	hx_os_status status;
	size_t count = 0;
	int was_started = request->started;
	if (request->stop_status) {
		request->ready = 1;
		return;
	}
	if (was_started && !(socket->source.ready & request->directions))
		return;
	socket->source.ready &= ~request->directions;
	request->started = 1;
	switch (op->kind) {
	case HX_OS_OP_CONNECT:
		status = was_started
			? hx_os_i_socket_connected(
				  socket, &request->completion.error)
			: hx_os_socket_connect(socket, &op->address,
				  &request->completion.error);
		break;
	case HX_OS_OP_ACCEPT:
		status = hx_os_socket_accept(socket, &request->accepted,
			&request->completion.peer, &request->completion.error);
		break;
	case HX_OS_OP_READ:
		status = hx_os_socket_receive_some(socket, op->data, op->bytes,
			&count, &request->completion.error);
		break;
	case HX_OS_OP_WRITE:
		status = hx_os_socket_send_some(socket,
			op->data ? (char *)op->data + request->completion.bytes
				 : NULL,
			op->bytes - request->completion.bytes, &count,
			&request->completion.error);
		break;
	case HX_OS_OP_SEND_TO:
		status = hx_os_socket_send_to(socket, &op->address, op->data,
			op->bytes, &count, &request->completion.error);
		break;
	case HX_OS_OP_RECEIVE_FROM:
		status = hx_os_socket_receive_from(socket,
			&request->completion.peer, op->data, op->bytes, &count,
			&request->completion.error);
		break;
	default:
		status = hx_os_i_error(
			&request->completion.error, HX_OS_INVALID, 0, 0);
		break;
	}
	request->completion.bytes += count;
	if (status == HX_OS_WOULD_BLOCK ||
		(status == HX_OS_OK && op->kind == HX_OS_OP_WRITE &&
			request->completion.bytes < op->bytes)) {
		status = hx_os_i_loop_refresh(
			socket, &request->completion.error);
		if (status == HX_OS_OK)
			return;
	}
	request->ready = 1;
#else
	(void)hx_os_i_error(
		&request->completion.error, HX_OS_NOT_SUPPORTED, 0, 0);
	request->ready = 1;
#endif
}
