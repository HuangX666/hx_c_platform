#include "net_private.h"
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
#if defined(_WIN32)
#include <afunix.h>
#else
#include <sys/un.h>
#endif
static hx_os_status hx_os_local_check(hx_os_socket *value, hx_os_error *error)
{
	if (!value || value->family != HX_OS_LOCAL || !value->local)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->loop || hx_os_atomic_load(&value->pending, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	return HX_OS_OK;
}
static hx_os_status hx_os_local_native(const hx_os_local_address *address,
	struct sockaddr_un *native, hx_os_i_socklen *length, hx_os_error *error)
{
	const char *end;
	size_t bytes, invalid;
	if (!address)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	end = memchr(address->path, 0, sizeof(address->path));
	if (!end || end == address->path)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	bytes = (size_t)(end - address->path);
	if (bytes >= sizeof(native->sun_path))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if (hx_os_text_validate_utf8(address->path, bytes, &invalid) !=
		HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	memset(native, 0, sizeof(*native));
	native->sun_family = AF_UNIX;
	memcpy(native->sun_path, address->path, bytes + 1);
#if defined(_WIN32)
	*length = sizeof(*native);
#else
	*length = (hx_os_i_socklen)(offsetof(struct sockaddr_un, sun_path) +
		bytes + 1);
#if defined(__APPLE__)
	native->sun_len = (unsigned char)*length;
#endif
#endif
	return HX_OS_OK;
}
static int hx_os_local_missing(int code)
{
#if defined(_WIN32)
	return code == WSAEAFNOSUPPORT || code == WSAEPROTONOSUPPORT ||
		code == WSAESOCKTNOSUPPORT;
#else
	return code == EAFNOSUPPORT || code == EPROTONOSUPPORT;
#endif
}
#endif
hx_os_status hx_os_local_address_init(
	hx_os_local_address *out, const char *path, hx_os_error *error)
{
	hx_os_local_address value;
	hx_os_status status;
	size_t bytes;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_i_path_validate(path, error);
	if (status != HX_OS_OK)
		return status;
	bytes = strlen(path);
	if (bytes >= sizeof(value.path))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	memset(&value, 0, sizeof(value));
	memcpy(value.path, path, bytes + 1);
	*out = value;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
hx_os_status hx_os_local_socket_create(
	hx_os_network *network, hx_os_socket **out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	{
		hx_os_i_socket native;
		int type = SOCK_STREAM;
		if (!network)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
#if defined(_WIN32)
		native = WSASocketW(
			AF_UNIX, type, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
#if defined(SOCK_CLOEXEC)
		type |= SOCK_CLOEXEC;
#endif
		native = socket(AF_UNIX, type, 0);
#endif
		if (native == HX_OS_I_BAD_SOCKET) {
			int code = hx_os_i_socket_last_error();
			hx_os_status status = hx_os_i_socket_error(error, code);
			if (hx_os_local_missing(code)) {
				if (error)
					error->status = HX_OS_NOT_SUPPORTED;
				return HX_OS_NOT_SUPPORTED;
			}
			return status;
		}
		return hx_os_i_socket_wrap(network, native, HX_OS_LOCAL,
			HX_OS_LOCAL_STREAM, out, error);
	}
#else
	(void)network;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_bind(hx_os_socket *value,
	const hx_os_local_address *address, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	struct sockaddr_un native;
	hx_os_i_socklen length;
	hx_os_status status = hx_os_local_check(value, error);
	if (status == HX_OS_OK)
		status = hx_os_local_native(address, &native, &length, error);
	if (status != HX_OS_OK)
		return status;
	return hx_os_i_socket_error(error,
		bind(value->native, (struct sockaddr *)&native, length)
			? hx_os_i_socket_last_error()
			: 0);
#else
	(void)value;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_listen(
	hx_os_socket *value, int backlog, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	hx_os_status status = hx_os_local_check(value, error);
	return status == HX_OS_OK ? hx_os_socket_listen(value, backlog, error)
				  : status;
#else
	(void)value;
	(void)backlog;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_connect(hx_os_socket *value,
	const hx_os_local_address *address, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	struct sockaddr_un native;
	hx_os_i_socklen length;
	hx_os_status status = hx_os_local_check(value, error);
	int code;
	if (status == HX_OS_OK)
		status = hx_os_local_native(address, &native, &length, error);
	if (status != HX_OS_OK)
		return status;
	if (value->local->connecting &&
		strcmp(value->local->address.path, address->path))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (value->local->connecting == 3)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	if (value->local->connecting == 4)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (value->local->connecting == 2) {
		status = hx_os_i_socket_connected(value, error);
		if (status != HX_OS_OK) {
			value->local->connecting = 4;
			return status;
		}
		length = sizeof(native);
		if (!getpeername(value->native, (struct sockaddr *)&native,
			    &length)) {
			value->local->connecting = 3;
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		}
		code = hx_os_i_socket_last_error();
#if defined(_WIN32)
		if (code == WSAENOTCONN)
#else
		if (code == ENOTCONN)
#endif
			return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);
		value->local->connecting = 4;
		return hx_os_i_socket_error(error, code);
	}
	value->local->address = *address;
	if (!connect(value->native, (struct sockaddr *)&native, length)) {
		value->local->connecting = 3;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	code = hx_os_i_socket_last_error();
#if defined(_WIN32)
	if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS ||
		code == WSAEALREADY) {
#else
	if (code == EINPROGRESS || code == EALREADY || code == EINTR) {
#endif
		value->local->connecting = 2;
		(void)hx_os_i_socket_error(error, code);
		if (error)
			error->status = HX_OS_WOULD_BLOCK;
		return HX_OS_WOULD_BLOCK;
	}
#if !defined(_WIN32)
	if (code == EAGAIN || code == EWOULDBLOCK) {
		/* AF_UNIX queue-full has not started a connection. */
		value->local->connecting = 1;
		return hx_os_i_socket_error(error, code);
	}
#endif
	value->local->connecting = 4;
	return hx_os_i_socket_error(error, code);
#else
	(void)value;
	(void)address;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_connect_until(hx_os_socket *value,
	const hx_os_local_address *address, hx_os_deadline deadline,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	for (;;) {
		hx_os_status status =
			hx_os_local_socket_connect(value, address, error);
		if (status != HX_OS_WOULD_BLOCK)
			return status;
		if (hx_os_deadline_expired(deadline)) {
			if (error)
				error->status = HX_OS_TIMEOUT;
			return HX_OS_TIMEOUT;
		}
		if (value->local->connecting == 1) {
			hx_os_deadline wake = hx_os_deadline_after_ms(5);
			if (wake > deadline)
				wake = deadline;
			status = hx_os_sleep_until(wake, error);
		}
		else
			status = hx_os_socket_wait_until(
				value, HX_OS_WAIT_WRITE, deadline, error);
		if (status != HX_OS_OK)
			return status;
	}
#else
	(void)value;
	(void)address;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_finish_connect(
	hx_os_socket *value, hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	hx_os_status status = hx_os_local_check(value, error);
	if (status != HX_OS_OK)
		return status;
	if (!value->local->connecting)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	return hx_os_local_socket_connect_until(
		value, &value->local->address, deadline, error);
#else
	(void)value;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_accept_until(hx_os_socket *value,
	hx_os_deadline deadline, hx_os_socket **out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	{
		hx_os_status status = hx_os_local_check(value, error);
		if (status != HX_OS_OK)
			return status;
		for (;;) {
			status = hx_os_socket_accept(value, out, NULL, error);
			if (status != HX_OS_WOULD_BLOCK)
				return status;
			status = hx_os_socket_wait_until(
				value, HX_OS_WAIT_READ, deadline, error);
			if (status != HX_OS_OK)
				return status;
			if (hx_os_deadline_expired(deadline)) {
				status = hx_os_socket_accept(
					value, out, NULL, error);
				return status == HX_OS_WOULD_BLOCK
					? hx_os_i_error(
						  error, HX_OS_TIMEOUT, 0, 0)
					: status;
			}
		}
	}
#else
	(void)value;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_socket_address(hx_os_socket *value, int peer,
	hx_os_local_address *out, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_LOCAL_SOCKETS
	struct sockaddr_un native;
	hx_os_local_address result = {{0}};
	hx_os_i_socklen length = sizeof(native);
	size_t offset = offsetof(struct sockaddr_un, sun_path), available,
	       bytes, invalid;
	const char *end;
	if (!value || value->family != HX_OS_LOCAL || !out ||
		(peer != 0 && peer != 1))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	memset(&native, 0, sizeof(native));
	if (peer ? getpeername(
			   value->native, (struct sockaddr *)&native, &length)
		 : getsockname(
			   value->native, (struct sockaddr *)&native, &length))
		return hx_os_i_socket_error(error, hx_os_i_socket_last_error());
#if defined(_WIN32)
	if (length < 0)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
#endif
	if ((size_t)length > sizeof(native))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if ((size_t)length <= offset) {
		*out = result;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
	available = (size_t)length - offset;
	if (!native.sun_path[0]) {
#if defined(_WIN32)
		size_t i;
		for (i = 0; i < available; ++i)
			if (native.sun_path[i])
				return hx_os_i_error(
					error, HX_OS_NOT_SUPPORTED, 0, 0);
		*out = result;
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
	}
	end = memchr(native.sun_path, 0, available);
	bytes = end ? (size_t)(end - native.sun_path) : available;
	if (bytes >= sizeof(result.path))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	memcpy(result.path, native.sun_path, bytes);
	if (hx_os_text_validate_utf8(result.path, bytes, &invalid) != HX_OS_OK)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING, 0, 0);
	*out = result;
	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)value;
	(void)peer;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_local_listen(hx_os_network *network, const char *path,
	int backlog, hx_os_socket **out, hx_os_error *error)
{
	hx_os_local_address address;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (backlog < 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	status = hx_os_local_address_init(&address, path, error);
	if (status == HX_OS_OK)
		status = hx_os_local_socket_create(network, out, error);
	if (status == HX_OS_OK)
		status = hx_os_local_socket_bind(*out, &address, error);
	if (status == HX_OS_OK)
		status = hx_os_local_socket_listen(*out, backlog, error);
	if (status != HX_OS_OK)
		(void)hx_os_socket_close(out, NULL);
	return status;
}
hx_os_status hx_os_local_connect(hx_os_network *network, const char *path,
	hx_os_deadline deadline, hx_os_socket **out, hx_os_error *error)
{
	hx_os_local_address address;
	hx_os_status status;
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	status = hx_os_local_address_init(&address, path, error);
	if (status == HX_OS_OK)
		status = hx_os_local_socket_create(network, out, error);
	if (status == HX_OS_OK)
		status = hx_os_local_socket_connect_until(
			*out, &address, deadline, error);
	if (status != HX_OS_OK && status != HX_OS_TIMEOUT)
		(void)hx_os_socket_close(out, NULL);
	return status;
}
