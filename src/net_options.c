#include "net_private.h"
#if !defined(_WIN32)
#include <netinet/tcp.h>
#endif

hx_os_status hx_os_socket_option_set(hx_os_socket *socket, int level, int name,
	const void *value, size_t bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	if (!socket || (!value && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (socket->loop)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (bytes > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	return hx_os_i_socket_error(error,
		setsockopt(socket->native, level, name, (const char *)value,
			(hx_os_i_socklen)bytes)
			? hx_os_i_socket_last_error()
			: 0);
#else
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_option_get(hx_os_socket *socket, int level, int name,
	void *value, size_t *bytes, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_i_socklen length;
	if (!socket || !bytes || (!value && *bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (*bytes > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	length = (hx_os_i_socklen)*bytes;
	if (getsockopt(socket->native, level, name, (char *)value, &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
	*bytes = (size_t)length;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)socket;
	(void)level;
	(void)name;
	(void)value;
	(void)bytes;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_set_nodelay(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
	int value = enabled != 0;
	return hx_os_socket_option_set(
		socket, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value), error);
}

hx_os_status hx_os_socket_set_keepalive(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
	int value = enabled != 0;
	return hx_os_socket_option_set(
		socket, SOL_SOCKET, SO_KEEPALIVE, &value, sizeof(value), error);
}

hx_os_status hx_os_socket_set_reuseaddr(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
	int value = enabled != 0;
	return hx_os_socket_option_set(
		socket, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value), error);
}

hx_os_status hx_os_socket_peer_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	struct sockaddr_storage native;
	hx_os_address result;
	hx_os_i_socklen length = sizeof(native);
	if (!socket || !address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (getpeername(socket->native, (struct sockaddr *)&native, &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
	if (!hx_os_i_address_public((struct sockaddr *)&native, &result))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	*address = result;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)socket;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
