#include "loop_private.h"

const char *hx_os_loop_backend(void)
{
	return "iocp";
}

hx_os_status hx_os_i_loop_native_create(hx_os_loop *loop, hx_os_error *error)
{
	loop->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
	if (!loop->port)
		return hx_os_i_win32(error, GetLastError());
	return HX_OS_OK;
}

void hx_os_i_loop_native_destroy(hx_os_loop *loop)
{
	(void)CloseHandle(loop->port);
}

void hx_os_i_loop_wake(hx_os_loop *loop)
{
	int64_t expected = 0;
	if (hx_os_atomic_compare_exchange(&loop->wake_pending, &expected, 1,
		    HX_OS_ACQ_REL, HX_OS_RELAXED))
		(void)PostQueuedCompletionStatus(loop->port, 0, 0, NULL);
}

hx_os_status hx_os_i_loop_attach(
	hx_os_loop *loop, hx_os_socket *socket, hx_os_error *error)
{
	if (!CreateIoCompletionPort((HANDLE)socket->native, loop->port, 0, 0))
		return hx_os_i_win32(error, GetLastError());
	return HX_OS_OK;
}

hx_os_status hx_os_i_loop_refresh(hx_os_socket *socket, hx_os_error *error)
{
	(void)socket;
	(void)error;
	/* IOCP associations last until the socket is closed. */
	return HX_OS_OK;
}

#if HX_OS_ENABLE_NETWORK
static int hx_os_loop_connect(hx_os_request *request)
{
	hx_os_socket *socket = request->operation.socket;
	LPFN_CONNECTEX connect_ex = NULL;
	GUID id = WSAID_CONNECTEX;
	DWORD bytes;
	BOOL result;
	struct sockaddr_storage local;
	int length = sizeof(local);
	if (WSAIoctl(socket->native, SIO_GET_EXTENSION_FUNCTION_POINTER, &id,
		    sizeof(id), &connect_ex, sizeof(connect_ex), &bytes, NULL,
		    NULL))
		return WSAGetLastError();
	if (getsockname(socket->native, (struct sockaddr *)&local, &length)) {
		hx_os_address address;
		hx_os_error error;
		hx_os_status status;
		memset(&address, 0, sizeof(address));
		address.family = (uint16_t)socket->family;
		status = hx_os_socket_bind(socket, &address, &error);
		if (status != HX_OS_OK)
			return (int)error.native_code;
	}
	(void)hx_os_i_address_native(&request->operation.address,
		&request->address, &request->address_length);
	result = connect_ex(socket->native,
		(struct sockaddr *)&request->address, request->address_length,
		NULL, 0, NULL, &request->overlapped);
	return result ? 0 : WSAGetLastError();
}

static int hx_os_loop_accept(hx_os_request *request)
{
	hx_os_socket *socket = request->operation.socket;
	LPFN_ACCEPTEX accept_ex = NULL;
	GUID id = WSAID_ACCEPTEX;
	DWORD bytes;
	BOOL result;
	hx_os_status status;
	if (WSAIoctl(socket->native, SIO_GET_EXTENSION_FUNCTION_POINTER, &id,
		    sizeof(id), &accept_ex, sizeof(accept_ex), &bytes, NULL,
		    NULL))
		return WSAGetLastError();
	status = hx_os_socket_create(socket->network, socket->family, HX_OS_TCP,
		&request->accepted, &request->completion.error);
	if (status != HX_OS_OK)
		return WSA_NOT_ENOUGH_MEMORY;
	result = accept_ex(socket->native, request->accepted->native,
		request->accept_addresses, 0,
		sizeof(struct sockaddr_storage) + 16,
		sizeof(struct sockaddr_storage) + 16, &bytes,
		&request->overlapped);
	return result ? 0 : WSAGetLastError();
}

static void hx_os_loop_start_native(hx_os_request *request)
{
	hx_os_operation *op = &request->operation;
	WSABUF buffer;
	size_t remaining = op->bytes - request->completion.bytes;
	int result;
	int code;
	memset(&request->overlapped, 0, sizeof(request->overlapped));
	request->native_done = 0;
	request->native_error = 0;
	request->native_bytes = 0;
	request->flags = 0;
	request->started = 1;
	buffer.buf = op->data ? (char *)op->data + request->completion.bytes
			      : request->accept_addresses;
	buffer.len = remaining > INT_MAX ? INT_MAX : (ULONG)remaining;
	if (op->kind == HX_OS_OP_CONNECT)
		code = hx_os_loop_connect(request);
	else if (op->kind == HX_OS_OP_ACCEPT)
		code = hx_os_loop_accept(request);
	else {
		if (op->kind == HX_OS_OP_READ)
			result = WSARecv(op->socket->native, &buffer, 1, NULL,
				&request->flags, &request->overlapped, NULL);
		else if (op->kind == HX_OS_OP_WRITE)
			result = WSASend(op->socket->native, &buffer, 1, NULL,
				0, &request->overlapped, NULL);
		else if (op->kind == HX_OS_OP_SEND_TO) {
			(void)hx_os_i_address_native(&op->address,
				&request->address, &request->address_length);
			result = WSASendTo(op->socket->native, &buffer, 1, NULL,
				0, (struct sockaddr *)&request->address,
				request->address_length, &request->overlapped,
				NULL);
		}
		else {
			request->address_length = sizeof(request->address);
			result = WSARecvFrom(op->socket->native, &buffer, 1,
				NULL, &request->flags,
				(struct sockaddr *)&request->address,
				&request->address_length, &request->overlapped,
				NULL);
		}
		code = result ? WSAGetLastError() : 0;
	}
	if (!code || code == WSA_IO_PENDING)
		request->native_pending = 1;
	else {
		(void)hx_os_i_socket_error(&request->completion.error, code);
		(void)hx_os_socket_close(&request->accepted, NULL);
		request->ready = 1;
	}
}

static void hx_os_loop_native_result(hx_os_request *request)
{
	hx_os_operation *op = &request->operation;
	int code = 0;
	request->completion.bytes += request->native_bytes;
	if (request->native_error) {
		hx_os_status status;
		status = hx_os_i_win32(
			&request->completion.error, request->native_error);
		if (request->native_error == ERROR_OPERATION_ABORTED)
			status = HX_OS_CANCELLED;
		if (request->native_error == ERROR_MORE_DATA ||
			request->native_error == WSAEMSGSIZE)
			status = HX_OS_LIMIT;
		request->completion.error.status = status;
		request->ready = 1;
	}
	else if (op->kind == HX_OS_OP_ACCEPT) {
		int length = sizeof(request->address);
		if (setsockopt(request->accepted->native, SOL_SOCKET,
			    SO_UPDATE_ACCEPT_CONTEXT,
			    (char *)&op->socket->native,
			    sizeof(op->socket->native)) ||
			getpeername(request->accepted->native,
				(struct sockaddr *)&request->address, &length))
			code = WSAGetLastError();
		else
			(void)hx_os_i_address_public(
				(struct sockaddr *)&request->address,
				&request->completion.peer);
		(void)hx_os_i_socket_error(&request->completion.error, code);
		request->ready = 1;
	}
	else if (op->kind == HX_OS_OP_CONNECT) {
		if (setsockopt(op->socket->native, SOL_SOCKET,
			    SO_UPDATE_CONNECT_CONTEXT, NULL, 0))
			code = WSAGetLastError();
		(void)hx_os_i_socket_error(&request->completion.error, code);
		request->ready = 1;
	}
	else if (op->kind == HX_OS_OP_WRITE &&
		request->completion.bytes < op->bytes &&
		!request->stop_status) {
		if (!request->native_bytes) {
			(void)hx_os_i_error(&request->completion.error,
				HX_OS_IO_ERROR, 0, 0);
			request->ready = 1;
		}
		else
			hx_os_loop_start_native(request);
	}
	else {
		hx_os_status status =
			op->kind == HX_OS_OP_READ && !request->native_bytes
			? HX_OS_EOF
			: HX_OS_OK;
		(void)hx_os_i_error(&request->completion.error, status, 0, 0);
		request->ready = 1;
	}
	if (op->kind == HX_OS_OP_RECEIVE_FROM && request->ready)
		(void)hx_os_i_address_public(
			(struct sockaddr *)&request->address,
			&request->completion.peer);
	if (request->ready && request->completion.error.status != HX_OS_OK)
		(void)hx_os_socket_close(&request->accepted, NULL);
}

void hx_os_i_loop_step(hx_os_request *request)
{
	if (request->native_pending) {
		if (request->stop_status && !request->native_cancelled) {
			(void)CancelIoEx(
				(HANDLE)request->operation.socket->native,
				&request->overlapped);
			request->native_cancelled = 1;
		}
		return;
	}
	if (request->native_done) {
		request->native_done = 0;
		hx_os_loop_native_result(request);
		return;
	}
	if (request->stop_status ||
		((request->operation.kind == HX_OS_OP_READ ||
			 request->operation.kind == HX_OS_OP_WRITE) &&
			!request->operation.bytes)) {
		request->ready = 1;
		return;
	}
	if (!request->started)
		hx_os_loop_start_native(request);
}
#else
void hx_os_i_loop_step(hx_os_request *request)
{
	(void)hx_os_i_error(
		&request->completion.error, HX_OS_NOT_SUPPORTED, 0, 0);
	request->ready = 1;
}
#endif

hx_os_status hx_os_i_loop_wait(
	hx_os_loop *loop, hx_os_deadline deadline, hx_os_error *error)
{
	DWORD bytes;
	ULONG_PTR key;
	OVERLAPPED *overlapped = NULL;
	BOOL result = GetQueuedCompletionStatus(loop->port, &bytes, &key,
		&overlapped, hx_os_i_wait_ms(deadline));
	DWORD code = result ? 0 : GetLastError();
	if (overlapped) {
		hx_os_request *request = (hx_os_request *)((char *)overlapped -
			offsetof(hx_os_request, overlapped));
		request->native_pending = 0;
		request->native_done = 1;
		request->native_bytes = bytes;
		request->native_error = code;
		return HX_OS_OK;
	}
	if (result) {
		hx_os_atomic_store(&loop->wake_pending, 0, HX_OS_RELEASE);
		return HX_OS_OK;
	}
	if (code == WAIT_TIMEOUT)
		return HX_OS_TIMEOUT;
	return hx_os_i_win32(error, code);
}
