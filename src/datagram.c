#include "net_private.h"
#include "hx_os_datagram.h"

#if !defined(_WIN32)
#include <sys/uio.h>
#if defined(__APPLE__)
#include <net/if_dl.h>
#endif
#endif

#if HX_OS_ENABLE_NETWORK
#if defined(_WIN32)
typedef WSAMSG hx_os_i_message;
typedef WSACMSGHDR hx_os_i_control_header;
typedef IN_PKTINFO hx_os_i_packet4;
typedef IN6_PKTINFO hx_os_i_packet6;
#define HX_OS_CMSG_FIRST WSA_CMSG_FIRSTHDR
#define HX_OS_CMSG_NEXT WSA_CMSG_NXTHDR
#define HX_OS_CMSG_DATA WSA_CMSG_DATA
#define HX_OS_CMSG_LENGTH WSA_CMSG_LEN
#define HX_OS_CMSG_SPACE WSA_CMSG_SPACE
#else
typedef struct msghdr hx_os_i_message;
typedef struct cmsghdr hx_os_i_control_header;
#if defined(IP_PKTINFO)
typedef struct in_pktinfo hx_os_i_packet4;
#endif
typedef struct in6_pktinfo hx_os_i_packet6;
#define HX_OS_CMSG_FIRST CMSG_FIRSTHDR
#define HX_OS_CMSG_NEXT CMSG_NXTHDR
#define HX_OS_CMSG_DATA CMSG_DATA
#define HX_OS_CMSG_LENGTH CMSG_LEN
#define HX_OS_CMSG_SPACE CMSG_SPACE
#endif

typedef union hx_os_i_packet_control {
	hx_os_i_control_header alignment;
	unsigned char bytes[256];
} hx_os_i_packet_control;

static hx_os_status hx_os_packet_check(hx_os_socket *socket, hx_os_error *error)
{
	if (!socket || socket->protocol != HX_OS_UDP ||
		(socket->family != HX_OS_IPV4 && socket->family != HX_OS_IPV6))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (socket->loop)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	return HX_OS_OK;
}

static hx_os_status hx_os_packet_error(hx_os_error *error, int code)
{
#if defined(_WIN32)
	if (code == WSAEOPNOTSUPP || code == WSAENOPROTOOPT)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_WINSOCK, code);
#else
	if (code == EOPNOTSUPP || code == ENOPROTOOPT)
		return hx_os_i_error(
			error, HX_OS_NOT_SUPPORTED, HX_OS_ERROR_ERRNO, code);
#endif
	return hx_os_i_socket_error(error, code);
}

static hx_os_status hx_os_packet_option(hx_os_socket *socket, int level,
	int name, int value, hx_os_error *error)
{
	return hx_os_packet_error(error,
		setsockopt(socket->native, level, name, (const char *)&value,
			(hx_os_i_socklen)sizeof(value))
			? hx_os_i_socket_last_error()
			: 0);
}

static int hx_os_packet_mapped(const hx_os_address *address)
{
	static const unsigned char prefix[12] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

	return address->family == HX_OS_IPV6 &&
		!memcmp(address->bytes, prefix, sizeof(prefix));
}

static void hx_os_packet_parse(
	hx_os_i_message *message, hx_os_packet_info *info)
{
	hx_os_i_control_header *header;

	for (header = HX_OS_CMSG_FIRST(message); header;
		header = HX_OS_CMSG_NEXT(message, header)) {
#if defined(IP_PKTINFO)
		if (header->cmsg_level == IPPROTO_IP &&
			header->cmsg_type == IP_PKTINFO &&
			header->cmsg_len >=
				HX_OS_CMSG_LENGTH(sizeof(hx_os_i_packet4))) {
			hx_os_i_packet4 packet;

			memcpy(&packet, HX_OS_CMSG_DATA(header),
				sizeof(packet));
			info->local.family = HX_OS_IPV4;
			memcpy(info->local.bytes, &packet.ipi_addr, 4);
			info->interface_index = (uint32_t)packet.ipi_ifindex;
			info->flags |= HX_OS_PACKET_LOCAL_ADDRESS |
				HX_OS_PACKET_INTERFACE;
		}
#endif
		if (header->cmsg_level == IPPROTO_IPV6 &&
			header->cmsg_type == IPV6_PKTINFO &&
			header->cmsg_len >=
				HX_OS_CMSG_LENGTH(sizeof(hx_os_i_packet6))) {
			hx_os_i_packet6 packet;

			memcpy(&packet, HX_OS_CMSG_DATA(header),
				sizeof(packet));
			info->local.family = HX_OS_IPV6;
			memcpy(info->local.bytes, &packet.ipi6_addr, 16);
			info->interface_index = packet.ipi6_ifindex;
			if ((info->local.bytes[0] == 0xfe &&
				    (info->local.bytes[1] & 0xc0) == 0x80) ||
				(info->local.bytes[0] == 0xff &&
					(info->local.bytes[1] & 15) <= 2))
				info->local.scope_id = info->interface_index;
			info->flags |= HX_OS_PACKET_LOCAL_ADDRESS |
				HX_OS_PACKET_INTERFACE;
		}
#if defined(__APPLE__)
		if (header->cmsg_level == IPPROTO_IP &&
			header->cmsg_type == IP_RECVDSTADDR &&
			header->cmsg_len >= HX_OS_CMSG_LENGTH(4)) {
			info->local.family = HX_OS_IPV4;
			memcpy(info->local.bytes, HX_OS_CMSG_DATA(header), 4);
			info->flags |= HX_OS_PACKET_LOCAL_ADDRESS;
		}
		if (header->cmsg_level == IPPROTO_IP &&
			header->cmsg_type == IP_RECVIF &&
			header->cmsg_len >=
				HX_OS_CMSG_LENGTH(sizeof(struct sockaddr_dl))) {
			struct sockaddr_dl link;

			memcpy(&link, HX_OS_CMSG_DATA(header), sizeof(link));
			info->interface_index = link.sdl_index;
			info->flags |= HX_OS_PACKET_INTERFACE;
		}
#endif
	}
}

static hx_os_status hx_os_packet_control(const hx_os_address *peer,
	const hx_os_packet_info *route, hx_os_i_packet_control *control,
	size_t *bytes, hx_os_error *error)
{
	hx_os_i_control_header *header = &control->alignment;
	uint32_t index = 0;
	int family = hx_os_packet_mapped(peer) ? HX_OS_IPV4 : peer->family;
	int local;

	*bytes = 0;
	if (!route)
		return HX_OS_OK;

	if (route->flags & ~15u)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	local = (route->flags & HX_OS_PACKET_LOCAL_ADDRESS) != 0;
	if (route->flags & HX_OS_PACKET_INTERFACE)
		index = route->interface_index;

	if (!local && !index)
		return HX_OS_OK;

	if (local &&
		(route->local.port ||
			(route->local.family != family &&
				!(family == HX_OS_IPV4 &&
					hx_os_packet_mapped(&route->local)))))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (family == HX_OS_IPV6 && local && route->local.scope_id) {
		if (index && index != route->local.scope_id)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		index = route->local.scope_id;
	}
	memset(control, 0, sizeof(*control));
	if (family == HX_OS_IPV6) {
		hx_os_i_packet6 packet;

		memset(&packet, 0, sizeof(packet));
		packet.ipi6_ifindex = index;
		if (local)
			memcpy(&packet.ipi6_addr, route->local.bytes, 16);

		header->cmsg_level = IPPROTO_IPV6;
		header->cmsg_type = IPV6_PKTINFO;
		header->cmsg_len = HX_OS_CMSG_LENGTH(sizeof(packet));
		memcpy(HX_OS_CMSG_DATA(header), &packet, sizeof(packet));
		*bytes = HX_OS_CMSG_SPACE(sizeof(packet));
	}
	else {
#if defined(IP_PKTINFO)
		hx_os_i_packet4 packet;
		const unsigned char *address = route->local.bytes;

		if (index > INT_MAX)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

		if (hx_os_packet_mapped(&route->local))
			address += 12;

		memset(&packet, 0, sizeof(packet));

#if defined(_WIN32)
		packet.ipi_ifindex = index;
#else
		packet.ipi_ifindex = (int)index;
#endif
		if (local) {
#if defined(_WIN32)
			memcpy(&packet.ipi_addr, address, 4);
#else
			memcpy(&packet.ipi_spec_dst, address, 4);
#endif
		}
		header->cmsg_level = IPPROTO_IP;
		header->cmsg_type = IP_PKTINFO;
		header->cmsg_len = HX_OS_CMSG_LENGTH(sizeof(packet));
		memcpy(HX_OS_CMSG_DATA(header), &packet, sizeof(packet));
		*bytes = HX_OS_CMSG_SPACE(sizeof(packet));
#else
		// Do not emulate this by changing socket-wide routing.
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	}
	return HX_OS_OK;
}
#endif

hx_os_status hx_os_socket_set_packet_info(
	hx_os_socket *socket, int enabled, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	hx_os_status status = hx_os_packet_check(socket, error);

	if (status != HX_OS_OK)
		return status;

	if (socket->family == HX_OS_IPV6) {
		int only_ipv6 = 0;
		hx_os_i_socklen length = sizeof(only_ipv6);
#if defined(_WIN32)
		int option = IPV6_PKTINFO;
#else
		int option = IPV6_RECVPKTINFO;
#endif
		status = hx_os_packet_option(
			socket, IPPROTO_IPV6, option, enabled != 0, error);
		if (status != HX_OS_OK)
			return status;

		if (getsockopt(socket->native, IPPROTO_IPV6, IPV6_V6ONLY,
			    (char *)&only_ipv6, &length))
			return hx_os_packet_error(
				error, hx_os_i_socket_last_error());

		if (only_ipv6)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#if defined(IP_PKTINFO)
	return hx_os_packet_option(
		socket, IPPROTO_IP, IP_PKTINFO, enabled != 0, error);
#elif defined(__APPLE__)
	status = hx_os_packet_option(
		socket, IPPROTO_IP, IP_RECVDSTADDR, enabled != 0, error);
	if (status != HX_OS_OK)
		return status;

	return hx_os_packet_option(
		socket, IPPROTO_IP, IP_RECVIF, enabled != 0, error);
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
#else
	(void)socket;
	(void)enabled;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_send_packet(hx_os_socket *socket,
	const hx_os_address *peer, const hx_os_packet_info *route,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error)
{
	if (completed)
		*completed = 0;
#if HX_OS_ENABLE_NETWORK
	{
		struct sockaddr_storage address;
		hx_os_i_socklen length;
		hx_os_i_packet_control control;
		hx_os_i_message message;
		size_t control_bytes;
		hx_os_status status = hx_os_packet_check(socket, error);

		if (status != HX_OS_OK)
			return status;

		if (!completed || (!data && bytes) || !peer ||
			peer->family != socket->family ||
			!hx_os_i_address_native(peer, &address, &length))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		if (bytes > INT_MAX)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		status = hx_os_packet_control(
			peer, route, &control, &control_bytes, error);
		if (status != HX_OS_OK)
			return status;

		if (!control_bytes)
			return hx_os_socket_send_to(
				socket, peer, data, bytes, completed, error);

		memset(&message, 0, sizeof(message));
#if defined(_WIN32)
		{
			WSABUF vector;
			DWORD count = 0;
			GUID id = WSAID_WSASENDMSG;

			if (!socket->send_message &&
				WSAIoctl(socket->native,
					SIO_GET_EXTENSION_FUNCTION_POINTER, &id,
					sizeof(id), &socket->send_message,
					sizeof(socket->send_message), &count,
					NULL, NULL))
				return hx_os_packet_error(
					error, WSAGetLastError());

			vector.buf = data ? (char *)data : (char *)"";
			vector.len = (ULONG)bytes;
			message.name = (struct sockaddr *)&address;
			message.namelen = length;
			message.lpBuffers = &vector;
			message.dwBufferCount = 1;
			message.Control.buf = (char *)control.bytes;
			message.Control.len = (ULONG)control_bytes;
			if (socket->send_message(socket->native, &message, 0,
				    &count, NULL, NULL))
				return hx_os_packet_error(
					error, WSAGetLastError());

			*completed = count;
		}
#else
		{
			struct iovec vector;
			ssize_t count;
			int flags = 0;

			vector.iov_base = (void *)data;
			vector.iov_len = bytes;
			message.msg_name = &address;
			message.msg_namelen = length;
			message.msg_iov = &vector;
			message.msg_iovlen = 1;
			message.msg_control = control.bytes;
			message.msg_controllen = control_bytes;
#ifdef MSG_NOSIGNAL
			flags = MSG_NOSIGNAL;
#endif
			count = sendmsg(socket->native, &message, flags);
			if (count < 0)
				return hx_os_packet_error(error, errno);

			*completed = (size_t)count;
		}
#endif
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)socket;
	(void)peer;
	(void)route;
	(void)data;
	(void)bytes;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_receive_packet(hx_os_socket *socket,
	hx_os_address *peer, hx_os_packet_info *info, void *data,
	size_t capacity, size_t *completed, hx_os_error *error)
{
	if (completed)
		*completed = 0;

	if (peer)
		memset(peer, 0, sizeof(*peer));

	if (info)
		memset(info, 0, sizeof(*info));
#if HX_OS_ENABLE_NETWORK
	{
		struct sockaddr_storage address;
		hx_os_i_packet_control control;
		hx_os_i_message message;
		hx_os_packet_info received;
		hx_os_status status = hx_os_packet_check(socket, error);
		int code = 0, flags;

		if (status != HX_OS_OK)
			return status;

		if (!completed || (!data && capacity))
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		if (capacity > INT_MAX)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (!info)
			return hx_os_socket_receive_from(
				socket, peer, data, capacity, completed, error);

		memset(&message, 0, sizeof(message));
		memset(&address, 0, sizeof(address));
		memset(&control, 0, sizeof(control));
		memset(&received, 0, sizeof(received));
#if defined(_WIN32)
		{
			WSABUF vector;
			DWORD count = 0;
			char empty;
			GUID id = WSAID_WSARECVMSG;

			if (!socket->receive_message &&
				WSAIoctl(socket->native,
					SIO_GET_EXTENSION_FUNCTION_POINTER, &id,
					sizeof(id), &socket->receive_message,
					sizeof(socket->receive_message), &count,
					NULL, NULL))
				return hx_os_packet_error(
					error, WSAGetLastError());

			count = 0;
			vector.buf = data ? data : &empty;
			vector.len = (ULONG)capacity;
			message.name = (struct sockaddr *)&address;
			message.namelen = sizeof(address);
			message.lpBuffers = &vector;
			message.dwBufferCount = 1;
			message.Control.buf = (char *)control.bytes;
			message.Control.len = sizeof(control.bytes);
			if (socket->receive_message(socket->native, &message,
				    &count, NULL, NULL))
				code = WSAGetLastError();

			if (code && code != WSAEMSGSIZE)
				return hx_os_packet_error(error, code);

			*completed = count;
			flags = (int)message.dwFlags;
			if (code == WSAEMSGSIZE && !(flags & MSG_CTRUNC))
				flags |= MSG_TRUNC;
		}
#else
		{
			struct iovec vector;
			ssize_t count;

			vector.iov_base = data;
			vector.iov_len = capacity;
			message.msg_name = &address;
			message.msg_namelen = sizeof(address);
			message.msg_iov = &vector;
			message.msg_iovlen = 1;
			message.msg_control = control.bytes;
			message.msg_controllen = sizeof(control.bytes);
			count = recvmsg(socket->native, &message, 0);
			if (count < 0)
				return hx_os_packet_error(error, errno);

			*completed = (size_t)count;
			flags = message.msg_flags;
		}
#endif
		if (peer)
			(void)hx_os_i_address_public(
				(struct sockaddr *)&address, peer);

		hx_os_packet_parse(&message, &received);
		if (flags & MSG_TRUNC)
			received.flags |= HX_OS_PACKET_TRUNCATED;

		if (flags & MSG_CTRUNC)
			received.flags |= HX_OS_PACKET_CONTROL_TRUNCATED;

		if (info)
			*info = received;

		if (code)
			return hx_os_packet_error(error, code);

		return hx_os_i_error(error,
			(flags & (MSG_TRUNC | MSG_CTRUNC)) ? HX_OS_LIMIT
							   : HX_OS_OK,
			0, 0);
	}
#else
	(void)socket;
	(void)data;
	(void)capacity;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static int hx_os_packet_interrupted(const hx_os_error *error)
{
#if defined(_WIN32)
	return error->domain == HX_OS_ERROR_WINSOCK &&
		error->native_code == WSAEINTR;
#else
	return error->domain == HX_OS_ERROR_ERRNO &&
		error->native_code == EINTR;
#endif
}

static hx_os_status hx_os_packet_wait(hx_os_socket *socket, int direction,
	hx_os_deadline deadline, hx_os_status status, hx_os_error *error)
{
	int interrupted = hx_os_packet_interrupted(error);

	if (status != HX_OS_WOULD_BLOCK && !interrupted)
		return status;

	if (hx_os_deadline_expired(deadline))
		return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);

	if (interrupted)
		return HX_OS_OK;

	return hx_os_socket_wait_until(socket, direction, deadline, error);
}

hx_os_status hx_os_socket_send_packet_until(hx_os_socket *socket,
	const hx_os_address *peer, const hx_os_packet_info *route,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	hx_os_error native;
	hx_os_status status;

	for (;;) {
		status = hx_os_socket_send_packet(
			socket, peer, route, data, bytes, completed, &native);
		if (status == HX_OS_OK)
			break;

		status = hx_os_packet_wait(
			socket, HX_OS_WAIT_WRITE, deadline, status, &native);
		if (status != HX_OS_OK)
			break;
	}
	if (error)
		*error = native;

	return status;
}

hx_os_status hx_os_socket_receive_packet_until(hx_os_socket *socket,
	hx_os_address *peer, hx_os_packet_info *info, void *data,
	size_t capacity, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	hx_os_error native;
	hx_os_status status;

	for (;;) {
		status = hx_os_socket_receive_packet(
			socket, peer, info, data, capacity, completed, &native);
		if (status == HX_OS_OK || status == HX_OS_LIMIT)
			break;

		status = hx_os_packet_wait(
			socket, HX_OS_WAIT_READ, deadline, status, &native);
		if (status != HX_OS_OK)
			break;
	}
	if (error)
		*error = native;

	return status;
}

hx_os_status hx_os_socket_send_to_until(hx_os_socket *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	return hx_os_socket_send_packet_until(
		socket, peer, NULL, data, bytes, deadline, completed, error);
}

hx_os_status hx_os_socket_receive_from_until(hx_os_socket *socket,
	hx_os_address *peer, void *data, size_t capacity,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	return hx_os_socket_receive_packet_until(
		socket, peer, NULL, data, capacity, deadline, completed, error);
}

hx_os_status hx_os_socket_send_batch_until(hx_os_socket *socket,
	const hx_os_datagram_send *items, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	if (completed)
		*completed = 0;

	if (!completed || (!items && count))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (count > 64)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
#if HX_OS_ENABLE_NETWORK
	{
		hx_os_status status = hx_os_packet_check(socket, error);

		if (status != HX_OS_OK)
			return status;

		while (*completed < count) {
			const hx_os_datagram_send *item = &items[*completed];
			size_t bytes;

			status = hx_os_socket_send_packet_until(socket,
				&item->peer, &item->route, item->data,
				item->bytes, deadline, &bytes, error);
			if (status != HX_OS_OK)
				return status;

			++*completed;
		}
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)socket;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_receive_batch_until(hx_os_socket *socket,
	hx_os_datagram_receive *items, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	size_t i;

	if (completed)
		*completed = 0;

	if (!completed || (!items && count))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (count > 64)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	for (i = 0; i < count; ++i) {
		items[i].bytes = 0;
		memset(&items[i].peer, 0, sizeof(items[i].peer));
		memset(&items[i].info, 0, sizeof(items[i].info));
		items[i].status = HX_OS_WOULD_BLOCK;
	}
#if HX_OS_ENABLE_NETWORK
	{
		hx_os_status status = hx_os_packet_check(socket, error);

		if (status != HX_OS_OK)
			return status;

		for (i = 0; i < count; ++i) {
			hx_os_datagram_receive *item = &items[i];

			status = hx_os_socket_receive_packet_until(socket,
				&item->peer, &item->info, item->data,
				item->capacity,
				i ? HX_OS_DEADLINE_NOW : deadline, &item->bytes,
				error);
			if (i && status == HX_OS_TIMEOUT)
				break;

			item->status = status;
			if (status == HX_OS_OK || status == HX_OS_LIMIT)
				++*completed;

			if (status != HX_OS_OK)
				return status;
		}
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)socket;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
