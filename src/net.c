#include "net_private.h"
#include <stdio.h>

struct hx_os_address_list {
	size_t count;
	hx_os_address addresses[256];
};

hx_os_status hx_os_network_create(
	hx_os_context *context, hx_os_network **out, hx_os_error *error)
{
	hx_os_network *network;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	network = hx_os_i_zero(context, sizeof(*network));
	if (!network)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
#if defined(_WIN32)
	{
		WSADATA data;

		int code = WSAStartup(MAKEWORD(2, 2), &data);
		if (code) {
			hx_os_memory_free(network);

			return hx_os_i_socket_error(error, code);
		}
	}
#endif
	network->context = context;
	*out = network;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_network_destroy(hx_os_network **network, hx_os_error *error)
{
	if (!network)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*network)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_atomic_load(&(*network)->objects, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
#if defined(_WIN32)
	if (WSACleanup())
		return hx_os_i_socket_error(error, WSAGetLastError());
#endif
	hx_os_memory_free(*network);
	*network = NULL;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_i_address_native(const hx_os_address *address,
	struct sockaddr_storage *native, hx_os_i_socklen *length)
{
	memset(native, 0, sizeof(*native));
	if (address->family == HX_OS_IPV4) {
		struct sockaddr_in *value = (struct sockaddr_in *)native;

		value->sin_family = AF_INET;
		value->sin_port = htons(address->port);
		memcpy(&value->sin_addr, address->bytes, 4);
		*length = sizeof(*value);

		return 1;
	}

	if (address->family == HX_OS_IPV6) {
		struct sockaddr_in6 *value = (struct sockaddr_in6 *)native;

		value->sin6_family = AF_INET6;
		value->sin6_port = htons(address->port);
		value->sin6_scope_id = address->scope_id;
		memcpy(&value->sin6_addr, address->bytes, 16);
		*length = sizeof(*value);

		return 1;
	}

	return 0;
}

int hx_os_i_address_public(
	const struct sockaddr *native, hx_os_address *address)
{
	memset(address, 0, sizeof(*address));
	if (native->sa_family == AF_INET) {
		const struct sockaddr_in *value =
			(const struct sockaddr_in *)native;

		address->family = HX_OS_IPV4;
		address->port = ntohs(value->sin_port);
		memcpy(address->bytes, &value->sin_addr, 4);

		return 1;
	}

	if (native->sa_family == AF_INET6) {
		const struct sockaddr_in6 *value =
			(const struct sockaddr_in6 *)native;

		address->family = HX_OS_IPV6;
		address->port = ntohs(value->sin6_port);
		address->scope_id = value->sin6_scope_id;
		memcpy(address->bytes, &value->sin6_addr, 16);

		return 1;
	}

	return 0;
}

hx_os_status hx_os_address_parse(const char *numeric_host, uint16_t port,
	hx_os_address *out, hx_os_error *error)
{
	if (!numeric_host || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	memset(out, 0, sizeof(*out));
	out->port = port;
	if (inet_pton(AF_INET, numeric_host, out->bytes) == 1)
		out->family = HX_OS_IPV4;
	else if (inet_pton(AF_INET6, numeric_host, out->bytes) == 1)
		out->family = HX_OS_IPV6;
	else
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_address_format(const hx_os_address *address, char *buffer,
	size_t capacity, hx_os_error *error)
{
	int family;

	if (!address || !buffer || !capacity || capacity > INT_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	family = address->family == HX_OS_IPV4	? AF_INET
		: address->family == HX_OS_IPV6 ? AF_INET6
						: 0;
	if (!family)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!inet_ntop(
		    family, address->bytes, buffer, (hx_os_i_socklen)capacity))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_resolve(hx_os_network *network, const char *host,
	uint16_t port, int protocol, hx_os_address_list **out,
	hx_os_error *error)
{
	struct addrinfo hints;
	struct addrinfo *native = NULL;
	struct addrinfo *cursor;
	char service[8];
	hx_os_address_list *list;
	int code;
	size_t i;
	hx_os_status status = HX_OS_OK;

	if (!network || !host || !host[0] || !out ||
		(protocol != HX_OS_TCP && protocol != HX_OS_UDP))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;

	for (i = 0; host[i]; ++i)
		if ((unsigned char)host[i] >= 128)
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	list = hx_os_i_zero(network->context, sizeof(*list));
	if (!list)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = protocol == HX_OS_TCP ? SOCK_STREAM : SOCK_DGRAM;
	(void)snprintf(service, sizeof(service), "%u", (unsigned)port);
	code = getaddrinfo(host, service, &hints, &native);
	if (code) {
		int cause = errno;

		hx_os_memory_free(list);

		status = code == EAI_MEMORY  ? HX_OS_NO_MEMORY
			: code == EAI_NONAME ? HX_OS_NOT_FOUND
					     : HX_OS_IO_ERROR;
		(void)hx_os_i_error(error, status, HX_OS_ERROR_GAI, code);
#ifdef EAI_SYSTEM
		if (error && code == EAI_SYSTEM)
			error->cause_code = cause;
#else
		(void)cause;
#endif
		return status;
	}

	for (cursor = native; cursor; cursor = cursor->ai_next) {
		hx_os_address address;
		int duplicate = 0;

		if (!hx_os_i_address_public(cursor->ai_addr, &address))
			continue;

		for (i = 0; i < list->count; ++i) {
			if (!memcmp(&list->addresses[i], &address,
				    sizeof(address))) {
				duplicate = 1;
				break;
			}
		}

		if (duplicate)
			continue;

		if (list->count == 256) {
			status = HX_OS_LIMIT;
			break;
		}

		list->addresses[list->count++] = address;
	}

	freeaddrinfo(native);
	if (!list->count && status == HX_OS_OK)
		status = HX_OS_NOT_FOUND;

	if (status != HX_OS_OK)
		hx_os_memory_free(list);
	else
		*out = list;

	return hx_os_i_error(error, status, 0, 0);
}

size_t hx_os_address_list_count(const hx_os_address_list *list)
{
	return list ? list->count : 0;
}

const hx_os_address *hx_os_address_list_get(
	const hx_os_address_list *list, size_t index)
{
	return list && index < list->count ? &list->addresses[index] : NULL;
}

void hx_os_address_list_destroy(hx_os_address_list *list)
{
	hx_os_memory_free(list);
}

hx_os_status hx_os_i_socket_wrap(hx_os_network *network, hx_os_i_socket native,
	int family, int protocol, hx_os_socket **out, hx_os_error *error)
{
	hx_os_socket *socket;
	size_t allocation = sizeof(*socket);

	hx_os_status status = hx_os_i_socket_configure(native, 1, error);
	if (status != HX_OS_OK) {
		hx_os_i_socket_close(native);

		return status;
	}
#if HX_OS_ENABLE_LOCAL_SOCKETS
	if (family == HX_OS_LOCAL)
		allocation += sizeof(hx_os_i_local_state);
#endif
	socket = hx_os_i_zero(network->context, allocation);
	if (!socket) {
		hx_os_i_socket_close(native);

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	socket->native = native;
	socket->network = network;
	socket->family = family;
	socket->protocol = protocol;
#if HX_OS_ENABLE_LOCAL_SOCKETS
	if (family == HX_OS_LOCAL)
		socket->local = (hx_os_i_local_state *)(socket + 1);
#endif
	(void)hx_os_atomic_fetch_add(&network->objects, 1, HX_OS_RELAXED);
	*out = socket;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_create(hx_os_network *network, int family,
	int protocol, hx_os_socket **out, hx_os_error *error)
{
	hx_os_i_socket native;
	int type;

	if (family == HX_OS_LOCAL && protocol == HX_OS_LOCAL_STREAM)
		return hx_os_local_socket_create(network, out, error);

	if (!network || !out ||
		(family != HX_OS_IPV4 && family != HX_OS_IPV6) ||
		(protocol != HX_OS_TCP && protocol != HX_OS_UDP))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	type = protocol == HX_OS_TCP ? SOCK_STREAM : SOCK_DGRAM;
#if defined(_WIN32)
	native = WSASocketW(family == HX_OS_IPV4 ? AF_INET : AF_INET6, type, 0,
		NULL, 0, WSA_FLAG_OVERLAPPED);
#else
#if defined(SOCK_CLOEXEC)
	type |= SOCK_CLOEXEC;
#endif
	native = socket(family == HX_OS_IPV4 ? AF_INET : AF_INET6, type, 0);
#endif
	if (native == HX_OS_I_BAD_SOCKET)
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	return hx_os_i_socket_wrap(
		network, native, family, protocol, out, error);
}

hx_os_status hx_os_socket_bind(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error)
{
	struct sockaddr_storage native;
	hx_os_i_socklen length;

	if (!socket || !address ||
		!hx_os_i_address_native(address, &native, &length))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_socket_error(error,
		bind(socket->native, (struct sockaddr *)&native, length)
			? hx_os_i_socket_last_error()
			: 0);
}

hx_os_status hx_os_socket_listen(
	hx_os_socket *socket, int backlog, hx_os_error *error)
{
	if (!socket || !hx_os_i_socket_stream(socket) || backlog < 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_socket_error(error,
		listen(socket->native, backlog) ? hx_os_i_socket_last_error()
						: 0);
}

hx_os_status hx_os_socket_local_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error)
{
	struct sockaddr_storage native;
	hx_os_i_socklen length = sizeof(native);

	if (!socket || !address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (getsockname(socket->native, (struct sockaddr *)&native, &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	if (!hx_os_i_address_public((struct sockaddr *)&native, address))
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_accept(hx_os_socket *listener, hx_os_socket **out,
	hx_os_address *peer, hx_os_error *error)
{
	struct sockaddr_storage address;
	hx_os_i_socklen length = sizeof(address);
	hx_os_i_socket native;

	if (!listener || !out || !hx_os_i_socket_stream(listener))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (listener->family == HX_OS_LOCAL && peer)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	native = accept(listener->native, (struct sockaddr *)&address, &length);
	if (native == HX_OS_I_BAD_SOCKET)
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	if (peer)
		(void)hx_os_i_address_public((struct sockaddr *)&address, peer);

	return hx_os_i_socket_wrap(listener->network, native, listener->family,
		listener->protocol, out, error);
}

hx_os_status hx_os_socket_connect(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error)
{
	struct sockaddr_storage native;
	hx_os_i_socklen length;

	if (!socket || !address ||
		!hx_os_i_address_native(address, &native, &length))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_i_socket_error(error,
		connect(socket->native, (struct sockaddr *)&native, length)
			? hx_os_i_socket_last_error()
			: 0);
}

hx_os_status hx_os_i_socket_connected(hx_os_socket *socket, hx_os_error *error)
{
	int code = 0;
	hx_os_i_socklen length = sizeof(code);

	if (getsockopt(socket->native, SOL_SOCKET, SO_ERROR, (char *)&code,
		    &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
	return hx_os_i_socket_error(error, code);
}

hx_os_status hx_os_socket_wait_until(hx_os_socket *socket, int events,
	hx_os_deadline deadline, hx_os_error *error)
{
	if (!socket)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_socket_wait(socket->native, events, deadline, error);
}

hx_os_status hx_os_socket_connect_until(hx_os_socket *socket,
	const hx_os_address *address, hx_os_deadline deadline,
	hx_os_error *error)
{
	hx_os_status status = hx_os_socket_connect(socket, address, error);
	if (status != HX_OS_WOULD_BLOCK)
		return status;

	status = hx_os_socket_wait_until(
		socket, HX_OS_WAIT_WRITE, deadline, error);

	return status == HX_OS_OK ? hx_os_i_socket_connected(socket, error)
				  : status;
}

hx_os_status hx_os_socket_send_some(hx_os_socket *socket, const void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	int count;
	int flags = 0;

	if (!socket || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = 0;
	if (!bytes && hx_os_i_socket_stream(socket))
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (socket->protocol == HX_OS_UDP && bytes > INT_MAX)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	if (bytes > INT_MAX)
		bytes = INT_MAX;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif
	count = (int)send(socket->native, data ? (const char *)data : "",
		(int)bytes, flags);
	if (count < 0)
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	*completed = (size_t)count;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_receive_some(hx_os_socket *socket, void *data,
	size_t bytes, size_t *completed, hx_os_error *error)
{
	int count;

	if (!socket || !completed || (!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = 0;
	if (socket->protocol == HX_OS_UDP)
		return hx_os_socket_receive_from(
			socket, NULL, data, bytes, completed, error);

	if (!bytes)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (bytes > INT_MAX)
		bytes = INT_MAX;

	count = (int)recv(socket->native, (char *)data, (int)bytes, 0);
	if (count < 0)
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	*completed = (size_t)count;

	return hx_os_i_error(error, count ? HX_OS_OK : HX_OS_EOF, 0, 0);
}

hx_os_status hx_os_socket_receive_until(hx_os_socket *socket, void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	if (!socket || !hx_os_i_socket_stream(socket) || !completed ||
		(!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;

	for (;;) {
		hx_os_status status = hx_os_socket_receive_some(
			socket, data, bytes, completed, error);
		if (status != HX_OS_WOULD_BLOCK)
			return status;

		status = hx_os_socket_wait_until(
			socket, HX_OS_WAIT_READ, deadline, error);
		if (status != HX_OS_OK)
			return status;

		if (hx_os_deadline_expired(deadline)) {
			status = hx_os_socket_receive_some(
				socket, data, bytes, completed, error);

			return status == HX_OS_WOULD_BLOCK
				? hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0)
				: status;
		}
	}
}

hx_os_status hx_os_socket_send_until(hx_os_socket *socket, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	const unsigned char *input = data;

	if (!socket || !hx_os_i_socket_stream(socket) || !completed ||
		(!data && bytes))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;

	while (*completed < bytes) {
		size_t count;

		hx_os_status status = hx_os_socket_send_some(socket,
			input + *completed, bytes - *completed, &count, error);
		*completed += count;
		if (status != HX_OS_OK && status != HX_OS_WOULD_BLOCK)
			return status;

		if (*completed == bytes)
			break;

		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);

		if (!count) {
			status = hx_os_socket_wait_until(
				socket, HX_OS_WAIT_WRITE, deadline, error);
			if (status != HX_OS_OK)
				return status;
		}
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_send_to(hx_os_socket *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	struct sockaddr_storage address;
	hx_os_i_socklen length;
	int count;
	int flags = 0;

	if (!socket || socket->protocol != HX_OS_UDP || !peer || !completed ||
		(!data && bytes) || bytes > INT_MAX ||
		!hx_os_i_address_native(peer, &address, &length))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif
	count = (int)sendto(socket->native, data ? (const char *)data : "",
		(int)bytes, flags, (struct sockaddr *)&address, length);
	if (count < 0)
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());

	*completed = (size_t)count;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_socket_receive_from(hx_os_socket *socket,
	hx_os_address *peer, void *data, size_t bytes, size_t *completed,
	hx_os_error *error)
{
	struct sockaddr_storage address;
	hx_os_i_socklen length = sizeof(address);
	hx_os_status status = HX_OS_OK;

	if (!socket || socket->protocol != HX_OS_UDP || !completed ||
		(!data && bytes) || bytes > INT_MAX)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*completed = 0;
#if defined(_WIN32)
	{
		WSABUF buffer;
		DWORD count = 0;
		DWORD flags = 0;
		int code;

		buffer.buf = data;
		buffer.len = (ULONG)bytes;
		code = WSARecvFrom(socket->native, &buffer, 1, &count, &flags,
			       (struct sockaddr *)&address, &length, NULL, NULL)
			? WSAGetLastError()
			: 0;
		if (code && code != WSAEMSGSIZE)
			return hx_os_i_socket_error(error, code);

		*completed = count;
		if (code)
			status = HX_OS_LIMIT;
	}
#else
	{
		struct iovec vector;
		struct msghdr message;
		ssize_t count;

		memset(&message, 0, sizeof(message));
		vector.iov_base = data;
		vector.iov_len = bytes;
		message.msg_name = &address;
		message.msg_namelen = length;
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		count = recvmsg(socket->native, &message, 0);
		if (count < 0)
			return hx_os_i_socket_error(error, errno);

		*completed = (size_t)count;
		if (message.msg_flags & MSG_TRUNC)
			status = HX_OS_LIMIT;
	}
#endif
	if (peer)
		(void)hx_os_i_address_public((struct sockaddr *)&address, peer);

	return hx_os_i_error(error, status, 0, 0);
}

hx_os_status hx_os_socket_shutdown(
	hx_os_socket *socket, int direction, hx_os_error *error)
{
	int how = direction == HX_OS_SHUT_READ	? 0
		: direction == HX_OS_SHUT_WRITE ? 1
						: 2;

	if (!socket || direction < 1 || direction > 3)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	return hx_os_i_socket_error(error,
		shutdown(socket->native, how) ? hx_os_i_socket_last_error()
					      : 0);
}

hx_os_status hx_os_socket_close(hx_os_socket **socket, hx_os_error *error)
{
	hx_os_network *network;
	hx_os_status status;

	if (!socket)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*socket)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_atomic_load(&(*socket)->pending, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if ((*socket)->loop) {
		status = hx_os_i_loop_detach(*socket, error);
		if (status != HX_OS_OK)
			return status;
	}

	network = (*socket)->network;

	hx_os_i_socket_close((*socket)->native);
	hx_os_memory_free(*socket);
	*socket = NULL;
	(void)hx_os_atomic_fetch_add(&network->objects, -1, HX_OS_RELEASE);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
