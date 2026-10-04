#include "net_private.h"

hx_os_status hx_os_i_socket_configure(
	hx_os_i_socket native, int nonblocking, hx_os_error *error)
{
#if defined(_WIN32)
	u_long mode = nonblocking != 0;

	if (ioctlsocket(native, FIONBIO, &mode))
		return hx_os_i_socket_error(error, WSAGetLastError());

	if (!SetHandleInformation((HANDLE)native, HANDLE_FLAG_INHERIT, 0))
		return hx_os_i_win32(error, GetLastError());
#else
	/* Only newly created/accepted, privately owned sockets reach here. */
	if (fcntl(native, F_SETFL, nonblocking ? O_NONBLOCK : 0) ||
		fcntl(native, F_SETFD, FD_CLOEXEC))
		return hx_os_i_errno(error, errno);
#if defined(SO_NOSIGPIPE)
	{
		int enabled = 1;

		if (setsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
			    sizeof(enabled)))
			return hx_os_i_errno(error, errno);
	}
#endif
#endif
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_i_socket_last_error(void)
{
#if defined(_WIN32)
	return WSAGetLastError();
#else
	return errno;
#endif
}

hx_os_status hx_os_i_socket_error(hx_os_error *error, int code)
{
#if defined(_WIN32)
	hx_os_status status = HX_OS_IO_ERROR;

	switch (code) {
	case 0:
		status = HX_OS_OK;
		break;
	case WSAEWOULDBLOCK:
	case WSAEINPROGRESS:
	case WSAEALREADY:
		status = HX_OS_WOULD_BLOCK;
		break;
	case WSAETIMEDOUT:
		status = HX_OS_TIMEOUT;
		break;
	case WSAEACCES:
		status = HX_OS_ACCESS_DENIED;
		break;
	case WSAEINVAL:
		status = HX_OS_INVALID;
		break;
	case WSA_NOT_ENOUGH_MEMORY:
		status = HX_OS_NO_MEMORY;
		break;
	case WSAEMSGSIZE:
		status = HX_OS_LIMIT;
		break;

	default:
		break;
	}

	return hx_os_i_error(error, status, HX_OS_ERROR_WINSOCK, code);
#else
	if (code == EINPROGRESS || code == EALREADY)
		return hx_os_i_error(
			error, HX_OS_WOULD_BLOCK, HX_OS_ERROR_ERRNO, code);

	if (code == EMSGSIZE)
		return hx_os_i_error(
			error, HX_OS_LIMIT, HX_OS_ERROR_ERRNO, code);

	return hx_os_i_errno(error, code);
#endif
}

void hx_os_i_socket_close(hx_os_i_socket socket)
{
#if defined(_WIN32)
	(void)closesocket(socket);
#else
	(void)close(socket);
#endif
}

hx_os_status hx_os_i_socket_wait(hx_os_i_socket native, int events,
	hx_os_deadline deadline, hx_os_error *error)
{
	if (native == HX_OS_I_BAD_SOCKET || !events || (events & ~3))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (;;) {
		int result;

		uint32_t milliseconds = hx_os_i_wait_ms(deadline);
#if defined(_WIN32)
		fd_set read_set;
		fd_set write_set;
		fd_set errors;
		struct timeval duration;

		FD_ZERO(&read_set);
		FD_ZERO(&write_set);
		FD_ZERO(&errors);
		if (events & HX_OS_WAIT_READ)
			FD_SET(native, &read_set);

		if (events & HX_OS_WAIT_WRITE)
			FD_SET(native, &write_set);

		FD_SET(native, &errors);
		duration.tv_sec = (long)(milliseconds / 1000);
		duration.tv_usec = (long)(milliseconds % 1000) * 1000;

		result = select(0, &read_set, &write_set, &errors,
			milliseconds == UINT32_MAX ? NULL : &duration);
#else
		struct pollfd descriptor;

		descriptor.fd = native;
		descriptor.events =
			(short)(((events & HX_OS_WAIT_READ) ? POLLIN : 0) |
				((events & HX_OS_WAIT_WRITE) ? POLLOUT : 0));
		descriptor.revents = 0;

		result = poll(&descriptor, 1,
			milliseconds == UINT32_MAX ? -1 : (int)milliseconds);
		if (result < 0 && errno == EINTR)
			continue;
#endif
		if (result > 0)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);

		if (result < 0)
			return hx_os_i_socket_error(
				error, hx_os_i_socket_last_error());

		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
	}
}
