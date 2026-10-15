#include "net_private.h"
#if defined(_WIN32)
#include <mstcpip.h>
#ifndef TCP_KEEPCNT
// Older SDKs omit this constant; the provider still checks runtime support.
#define TCP_KEEPCNT 16
#endif
#else
#include <netinet/tcp.h>
#endif
#if defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if.h>
#endif

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
static hx_os_status hx_os_i_option_error(hx_os_error *error, int code)
{
#if defined(_WIN32)
	if (code == WSAENOPROTOOPT || code == WSAEOPNOTSUPP)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_WINSOCK, code);
#else
	if (code == ENOPROTOOPT || code == EOPNOTSUPP)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_ERRNO, code);
#endif
	return hx_os_i_socket_error(error, code);
}

static hx_os_status hx_os_i_option_socket(hx_os_socket *socket,
	int protocol, int family, hx_os_error *error)
{
	if (!socket || (protocol && socket->protocol != protocol) ||
		(family && socket->family != family) ||
		(socket->family != HX_OS_IPV4 && socket->family != HX_OS_IPV6))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (socket->loop)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	return HX_OS_OK;
}

static hx_os_status hx_os_i_option_int(hx_os_socket *socket, int level,
	int name, int value, hx_os_error *error)
{
	return hx_os_socket_option_set(
		socket, level, name, &value, sizeof(value), error);
}
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

	return hx_os_i_option_error(error,
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
		return hx_os_i_option_error(error, hx_os_i_socket_last_error());

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

hx_os_status hx_os_socket_configure_keepalive(hx_os_socket *socket,
	uint32_t idle_seconds, uint32_t interval_seconds, uint32_t probe_count,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_status status =
		hx_os_i_option_socket(socket, HX_OS_TCP, 0, error);
	if (status != HX_OS_OK)
		return status;

	if (!idle_seconds || !interval_seconds)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (idle_seconds > INT_MAX || interval_seconds > INT_MAX ||
		probe_count > INT_MAX)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

#if defined(_WIN32)
	if (idle_seconds > UINT32_MAX / 1000 ||
		interval_seconds > UINT32_MAX / 1000 || probe_count > 255)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
#endif
	if (probe_count) {
#if defined(TCP_KEEPCNT)
		status = hx_os_i_option_int(socket, IPPROTO_TCP,
			TCP_KEEPCNT, (int)probe_count, error);
		if (status != HX_OS_OK)
			return status;
#else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	}

#if defined(_WIN32)
	{
		struct tcp_keepalive values;
		DWORD returned = 0;

		values.onoff = 1;
		values.keepalivetime = idle_seconds * 1000;
		values.keepaliveinterval = interval_seconds * 1000;
		if (WSAIoctl(socket->native, SIO_KEEPALIVE_VALS, &values,
			    sizeof(values), NULL, 0, &returned, NULL, NULL))
			return hx_os_i_option_error(error, WSAGetLastError());

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
#if defined(__APPLE__)
	status = hx_os_i_option_int(socket, IPPROTO_TCP, TCP_KEEPALIVE,
		(int)idle_seconds, error);
#else
	status = hx_os_i_option_int(socket, IPPROTO_TCP, TCP_KEEPIDLE,
		(int)idle_seconds, error);
#endif
	if (status != HX_OS_OK)
		return status;

	status = hx_os_i_option_int(socket, IPPROTO_TCP, TCP_KEEPINTVL,
		(int)interval_seconds, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_socket_set_keepalive(socket, 1, error);
#endif
#else
	(void)socket;
	(void)idle_seconds;
	(void)interval_seconds;
	(void)probe_count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_set_reuseaddr(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
	int value = enabled != 0;

	return hx_os_socket_option_set(
		socket, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value), error);
}

hx_os_status hx_os_socket_set_dual_stack(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_status status =
		hx_os_i_option_socket(socket, 0, HX_OS_IPV6, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_i_option_int(
		socket, IPPROTO_IPV6, IPV6_V6ONLY, !enabled, error);
#else
	(void)socket;
	(void)enabled;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_set_broadcast(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_status status =
		hx_os_i_option_socket(socket, HX_OS_UDP, HX_OS_IPV4, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_i_option_int(
		socket, SOL_SOCKET, SO_BROADCAST, enabled != 0, error);
#else
	(void)socket;
	(void)enabled;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_i_multicast_membership(hx_os_socket *socket,
	const hx_os_address *group, uint32_t interface_index, int join,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	struct group_req request;
	hx_os_i_socklen length;
	hx_os_address address;
	hx_os_status status =
		hx_os_i_option_socket(socket, HX_OS_UDP, 0, error);
	if (status != HX_OS_OK)
		return status;

	if (!group || group->family != socket->family ||
		(group->family == HX_OS_IPV4 &&
			(group->bytes[0] & 0xf0) != 0xe0) ||
		(group->family == HX_OS_IPV6 && group->bytes[0] != 0xff))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	address = *group;
	address.port = 0;
	if (address.family == HX_OS_IPV6) {
		if (address.scope_id && interface_index &&
			address.scope_id != interface_index)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		if (!interface_index)
			interface_index = address.scope_id;

		address.scope_id = interface_index;
	}

	memset(&request, 0, sizeof(request));
	request.gr_interface = interface_index;
	(void)hx_os_i_address_native(&address, &request.gr_group, &length);
#if defined(__APPLE__)
	((struct sockaddr *)&request.gr_group)->sa_len = (uint8_t)length;
#endif
	return hx_os_socket_option_set(socket,
		group->family == HX_OS_IPV4 ? IPPROTO_IP : IPPROTO_IPV6,
		join ? MCAST_JOIN_GROUP : MCAST_LEAVE_GROUP,
		&request, sizeof(request), error);
#else
	(void)socket;
	(void)group;
	(void)interface_index;
	(void)join;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_join_multicast(hx_os_socket *socket,
	const hx_os_address *group, uint32_t interface_index,
	hx_os_error *error)
{
	return hx_os_i_multicast_membership(
		socket, group, interface_index, 1, error);
}

hx_os_status hx_os_socket_leave_multicast(hx_os_socket *socket,
	const hx_os_address *group, uint32_t interface_index,
	hx_os_error *error)
{
	return hx_os_i_multicast_membership(
		socket, group, interface_index, 0, error);
}

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS && defined(__APPLE__)
static hx_os_status hx_os_i_multicast_address(uint32_t interface_index,
	struct in_addr *address, hx_os_error *error)
{
	struct ifaddrs *interfaces = NULL;
	struct ifaddrs *item;
	int found = 0;

	address->s_addr = INADDR_ANY;
	if (!interface_index)
		return HX_OS_OK;

	if (getifaddrs(&interfaces))
		return hx_os_i_errno(error, errno);

	for (item = interfaces; item; item = item->ifa_next) {
		if (!item->ifa_addr || item->ifa_addr->sa_family != AF_INET ||
			if_nametoindex(item->ifa_name) != interface_index)
			continue;

		*address = ((struct sockaddr_in *)item->ifa_addr)->sin_addr;
		found = 1;
		break;
	}
	freeifaddrs(interfaces);

	return found ? HX_OS_OK : hx_os_i_error(
		error, HX_OS_NOT_FOUND, HX_OS_ERROR_ERRNO, ENODEV);
}
#endif

hx_os_status hx_os_socket_set_multicast_interface(
	hx_os_socket *socket, uint32_t interface_index, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_status status =
		hx_os_i_option_socket(socket, HX_OS_UDP, 0, error);
	if (status != HX_OS_OK)
		return status;

	if (socket->family == HX_OS_IPV6)
		return hx_os_socket_option_set(socket, IPPROTO_IPV6,
			IPV6_MULTICAST_IF, &interface_index,
			sizeof(interface_index), error);

#if defined(_WIN32)
	{
		uint32_t encoded;

		// Winsock interprets 0.x.x.x as an index, in network order.
		if (interface_index > 0x00ffffff)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

		encoded = htonl(interface_index);

		return hx_os_socket_option_set(socket, IPPROTO_IP,
			IP_MULTICAST_IF, &encoded, sizeof(encoded), error);
	}
#elif defined(__linux__)
	{
		struct ip_mreqn request;

		if (interface_index > INT_MAX)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

		memset(&request, 0, sizeof(request));
		request.imr_ifindex = (int)interface_index;

		return hx_os_socket_option_set(socket, IPPROTO_IP,
			IP_MULTICAST_IF, &request, sizeof(request), error);
	}
#else
	{
		struct in_addr address;

		status = hx_os_i_multicast_address(
			interface_index, &address, error);
		if (status != HX_OS_OK)
			return status;

		return hx_os_socket_option_set(socket, IPPROTO_IP,
			IP_MULTICAST_IF, &address, sizeof(address), error);
	}
#endif
#else
	(void)socket;
	(void)interface_index;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_i_multicast_value(hx_os_socket *socket,
	int ipv4_name, int ipv6_name, uint32_t value, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_status status =
		hx_os_i_option_socket(socket, HX_OS_UDP, 0, error);
	if (status != HX_OS_OK)
		return status;

	if (value > 255)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	if (socket->family == HX_OS_IPV6)
		return hx_os_i_option_int(
			socket, IPPROTO_IPV6, ipv6_name, (int)value, error);

#if defined(_WIN32)
	return hx_os_socket_option_set(
		socket, IPPROTO_IP, ipv4_name, &value, sizeof(value), error);
#else
	{
		unsigned char byte = (unsigned char)value;

		return hx_os_socket_option_set(socket, IPPROTO_IP,
			ipv4_name, &byte, sizeof(byte), error);
	}
#endif
#else
	(void)socket;
	(void)ipv4_name;
	(void)ipv6_name;
	(void)value;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_set_multicast_hops(
	hx_os_socket *socket, uint32_t hops, hx_os_error *error)
{
	return hx_os_i_multicast_value(socket,
		IP_MULTICAST_TTL, IPV6_MULTICAST_HOPS, hops, error);
}

hx_os_status hx_os_socket_set_multicast_loopback(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
	return hx_os_i_multicast_value(socket, IP_MULTICAST_LOOP,
		IPV6_MULTICAST_LOOP, (uint32_t)(enabled != 0), error);
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
