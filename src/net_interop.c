#include "net_private.h"
#include "hx_os_net_interop.h"

hx_os_status hx_os_socket_import(hx_os_network *network,
	uintptr_t *native_owner, int mode, hx_os_socket **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	hx_os_socket *socket;
	struct sockaddr_storage address;
	hx_os_i_socklen length = sizeof(address), option_size;
	hx_os_i_socket native;
	int kind, family;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!network || !native_owner || *native_owner == UINTPTR_MAX ||
		(mode != HX_OS_SOCKET_TAKE && mode != HX_OS_SOCKET_DUP))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if !defined(_WIN32)
	if (*native_owner > INT_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#endif
	native = (hx_os_i_socket)*native_owner;
	option_size = sizeof(kind);
	if (getsockopt(
		    native, SOL_SOCKET, SO_TYPE, (char *)&kind, &option_size) ||
		getsockname(native, (struct sockaddr *)&address, &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
	family = address.ss_family == AF_INET	? HX_OS_IPV4
		: address.ss_family == AF_INET6 ? HX_OS_IPV6
						: 0;
	if (!family || (kind != SOCK_STREAM && kind != SOCK_DGRAM))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#if !defined(_WIN32)
	{
		int flags = fcntl(native, F_GETFL);

		if (flags < 0)
			return hx_os_i_errno(error, errno);
		if (!(flags & O_NONBLOCK))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
#if defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
	{
		int enabled = 0;

		option_size = sizeof(enabled);
		if (getsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &enabled,
			    &option_size))
			return hx_os_i_errno(error, errno);
		if (!enabled)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	}
#endif
#endif
	socket = hx_os_i_zero(network->context, sizeof(*socket));
	if (!socket)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	if (mode == HX_OS_SOCKET_DUP) {
#if defined(_WIN32)
		WSAPROTOCOL_INFOW info;

		if (WSADuplicateSocketW(native, GetCurrentProcessId(), &info))
			native = INVALID_SOCKET;
		else
			native = WSASocketW(FROM_PROTOCOL_INFO,
				FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO, &info,
				0,
				WSA_FLAG_OVERLAPPED |
					WSA_FLAG_NO_HANDLE_INHERIT);
#else
		native = fcntl(native, F_DUPFD_CLOEXEC, 0);
#endif
		if (native == HX_OS_I_BAD_SOCKET) {
			int code = hx_os_i_socket_last_error();

			hx_os_memory_free(socket);
			return hx_os_i_socket_error(error, code);
		}
	}
	else {
#if defined(_WIN32)
		if (!SetHandleInformation(
			    (HANDLE)native, HANDLE_FLAG_INHERIT, 0)) {
			DWORD code = GetLastError();

			hx_os_memory_free(socket);
			return hx_os_i_win32(error, code);
		}
#else
		int flags = fcntl(native, F_GETFD);

		if (flags < 0 || fcntl(native, F_SETFD, flags | FD_CLOEXEC)) {
			int code = errno;

			hx_os_memory_free(socket);
			return hx_os_i_errno(error, code);
		}
#endif
	}
	socket->network = network;
	socket->native = native;
	socket->family = family;
	socket->protocol = kind == SOCK_STREAM ? HX_OS_TCP : HX_OS_UDP;
	hx_os_atomic_store(&socket->pending, 0, HX_OS_RELAXED);
	(void)hx_os_atomic_fetch_add(&network->objects, 1, HX_OS_RELAXED);
	*out = socket;
	if (mode == HX_OS_SOCKET_TAKE)
		*native_owner = UINTPTR_MAX;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)network;
	(void)native_owner;
	(void)mode;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_shutdown_drain_until(hx_os_socket *socket,
	hx_os_deadline deadline, uint64_t *discarded, hx_os_error *error)
{
	if (!discarded)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*discarded = 0;
#if HX_OS_ENABLE_NETWORK
	{
		unsigned char buffer[4096];
		hx_os_status status;
		size_t count;

		if (!socket || socket->protocol != HX_OS_TCP)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		if (socket->loop ||
			hx_os_atomic_load(&socket->pending, HX_OS_ACQUIRE))
			return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
		if (!socket->shutdown_stage) {
			status = hx_os_socket_shutdown(
				socket, HX_OS_SHUT_WRITE, error);
			if (status != HX_OS_OK)
				return status;
			socket->shutdown_stage = 1;
		}
		while (socket->shutdown_stage == 1) {
			status = hx_os_socket_receive_until(socket, buffer,
				sizeof(buffer), deadline, &count, error);
			*discarded += count;
			if (status == HX_OS_EOF)
				socket->shutdown_stage = 2;
			else if (status != HX_OS_OK)
				return status;
			else if (hx_os_deadline_expired(deadline))
				return hx_os_i_error(
					error, HX_OS_TIMEOUT, 0, 0);
		}
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)socket;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
