#include "duplex_private.h"

static hx_os_status hx_os_duplex_socket(SOCKET *out, hx_os_error *error)
{
	hx_os_status status;

	*out = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
	if (*out == INVALID_SOCKET)
		return hx_os_i_socket_error(error, WSAGetLastError());

	status = hx_os_i_socket_configure(*out, 1, error);
	if (status != HX_OS_OK) {
		(void)closesocket(*out);
		*out = INVALID_SOCKET;
	}

	return status;
}

hx_os_status hx_os_i_duplex_pair(hx_os_context *context,
	hx_os_deadline deadline, hx_os_file **first, hx_os_file **second,
	hx_os_error *error)
{
	hx_os_i_duplex_file *ends[2] = {NULL, NULL};
	SOCKET listener = INVALID_SOCKET, client = INVALID_SOCKET;
	SOCKET accepted = INVALID_SOCKET;
	struct sockaddr_in address, client_address, peer;
	WSADATA data;
	hx_os_status status = HX_OS_OK;
	int starts = 0, code, length, exclusive = 1, attempts = 0;

	ends[0] = hx_os_i_zero(context, sizeof(*ends[0]));
	ends[1] = hx_os_i_zero(context, sizeof(*ends[1]));
	if (!ends[0] || !ends[1]) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	// Each published endpoint balances one native provider reference.
	// No process-wide mutable library network singleton is introduced.
	while (starts < 2) {
		code = WSAStartup(MAKEWORD(2, 2), &data);
		if (code) {
			status = hx_os_i_socket_error(error, code);
			goto done;
		}

		++starts;
	}

	status = hx_os_duplex_socket(&listener, error);
	if (status == HX_OS_OK)
		status = hx_os_duplex_socket(&client, error);

	if (status != HX_OS_OK)
		goto done;

	if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		    (const char *)&exclusive, sizeof(exclusive)) ||
		setsockopt(client, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			(const char *)&exclusive, sizeof(exclusive))) {
		status = hx_os_i_socket_error(error, WSAGetLastError());
		goto done;
	}

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
		listen(listener, 8) ||
		bind(client, (struct sockaddr *)&address, sizeof(address))) {
		status = hx_os_i_socket_error(error, WSAGetLastError());
		goto done;
	}

	length = sizeof(address);
	if (getsockname(listener, (struct sockaddr *)&address, &length)) {
		status = hx_os_i_socket_error(error, WSAGetLastError());
		goto done;
	}

	length = sizeof(client_address);
	if (getsockname(client, (struct sockaddr *)&client_address, &length)) {
		status = hx_os_i_socket_error(error, WSAGetLastError());
		goto done;
	}

	if (connect(client, (struct sockaddr *)&address, sizeof(address))) {
		code = WSAGetLastError();
		if (code != WSAEWOULDBLOCK && code != WSAEINPROGRESS) {
			status = hx_os_i_socket_error(error, code);
			goto done;
		}
	}

	while (attempts++ < 64) {
		status = hx_os_i_socket_wait(
			listener, HX_OS_WAIT_READ, deadline, error);
		if (status != HX_OS_OK)
			goto done;

		length = sizeof(peer);
		accepted = accept(listener, (struct sockaddr *)&peer, &length);
		if (accepted == INVALID_SOCKET) {
			code = WSAGetLastError();
			if (code == WSAEWOULDBLOCK) {
				if (hx_os_deadline_expired(deadline)) {
					status = hx_os_i_error(
						error, HX_OS_TIMEOUT, 0, 0);
					goto done;
				}

				continue;
			}

			status = hx_os_i_socket_error(error, code);
			goto done;
		}

		if (length == sizeof(peer) && peer.sin_family == AF_INET &&
			peer.sin_port == client_address.sin_port &&
			peer.sin_addr.s_addr == client_address.sin_addr.s_addr)
			break;
		(void)closesocket(accepted);
		accepted = INVALID_SOCKET;
	}

	if (accepted == INVALID_SOCKET) {
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		goto done;
	}

	status = hx_os_i_socket_wait(client, HX_OS_WAIT_WRITE, deadline, error);
	if (status != HX_OS_OK)
		goto done;

	length = sizeof(code);
	if (getsockopt(client, SOL_SOCKET, SO_ERROR, (char *)&code, &length)) {
		status = hx_os_i_socket_error(error, WSAGetLastError());
		goto done;
	}

	if (code) {
		status = hx_os_i_socket_error(error, code);
		goto done;
	}

	status = hx_os_i_socket_configure(client, 0, error);
	if (status == HX_OS_OK)
		status = hx_os_i_socket_configure(accepted, 0, error);

	if (status != HX_OS_OK)
		goto done;

	ends[0]->native = client;
	ends[1]->native = accepted;
	ends[0]->base.pipe = ends[1]->base.pipe = HX_OS_I_FILE_SOCKET;
	ends[0]->base.flags = ends[1]->base.flags =
		HX_OS_FILE_READ | HX_OS_FILE_WRITE;
	*first = &ends[0]->base;
	*second = &ends[1]->base;
	client = accepted = INVALID_SOCKET;
	ends[0] = ends[1] = NULL;
	starts = 0;

	status = hx_os_i_error(error, HX_OS_OK, 0, 0);

done:
	if (listener != INVALID_SOCKET)
		(void)closesocket(listener);

	if (client != INVALID_SOCKET)
		(void)closesocket(client);

	if (accepted != INVALID_SOCKET)
		(void)closesocket(accepted);

	while (starts-- > 0)
		(void)WSACleanup();

	hx_os_memory_free(ends[0]);
	hx_os_memory_free(ends[1]);

	return status;
}

static hx_os_status hx_os_duplex_error(hx_os_error *error, int code)
{
	if (code == WSAECONNRESET || code == WSAECONNABORTED ||
		code == WSAESHUTDOWN)
		return hx_os_i_error(
			error, HX_OS_CLOSED, HX_OS_ERROR_WINSOCK, code);
	return hx_os_i_socket_error(error, code);
}

hx_os_status hx_os_i_duplex_read(hx_os_file *file, void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	int count = recv(((hx_os_i_duplex_file *)file)->native, data,
		bytes > INT_MAX ? INT_MAX : (int)bytes, 0);
	if (count < 0)
		return hx_os_duplex_error(error, WSAGetLastError());

	*completed = (size_t)count;

	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_EOF, 0, 0);
}

hx_os_status hx_os_i_duplex_write(hx_os_file *file, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	int count = send(((hx_os_i_duplex_file *)file)->native, data,
		bytes > INT_MAX ? INT_MAX : (int)bytes, 0);
	if (count < 0)
		return hx_os_duplex_error(error, WSAGetLastError());

	*completed = (size_t)count;

	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_IO_ERROR, 0, 0);
}

hx_os_status hx_os_i_duplex_close(hx_os_file **file, hx_os_error *error)
{
	hx_os_i_duplex_file *value = (hx_os_i_duplex_file *)*file;

	int code = closesocket(value->native) ? WSAGetLastError() : 0;
	if (WSACleanup() && !code)
		code = WSAGetLastError();

	hx_os_memory_free(value);
	*file = NULL;

	return hx_os_duplex_error(error, code);
}

hx_os_status hx_os_i_duplex_shutdown(
	hx_os_file *file, int direction, hx_os_error *error)
{
	int how = direction == HX_OS_SHUT_READ	? SD_RECEIVE
		: direction == HX_OS_SHUT_WRITE ? SD_SEND
						: SD_BOTH;

	return hx_os_duplex_error(error,
		shutdown(((hx_os_i_duplex_file *)file)->native, how)
			? WSAGetLastError()
			: 0);
}
