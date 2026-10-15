#include "net_private.h"
#include "hx_os_net_vectors.h"
#if !defined(_WIN32)
#include <sys/uio.h>
#endif

#if HX_OS_ENABLE_NETWORK
static hx_os_status hx_os_vectors_check(hx_os_socket *socket,
	const hx_os_send_buffer *send, const hx_os_receive_buffer *receive,
	size_t count, size_t *total, hx_os_error *error)
{
	size_t i;

	*total = 0;
	if (!socket || !hx_os_i_socket_stream(socket) ||
		(count && !send && !receive))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (socket->loop)
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

	if (count > 64)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	for (i = 0; i < count; ++i) {
		const void *data = send ? send[i].data : receive[i].data;
		size_t bytes = send ? send[i].bytes : receive[i].bytes;

		if (!data && bytes)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

		if (bytes > SIZE_MAX - *total)
			return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

		*total += bytes;
	}
	return HX_OS_OK;
}

static hx_os_status hx_os_vectors_transfer(hx_os_socket *socket,
	const hx_os_send_buffer *send, const hx_os_receive_buffer *receive,
	size_t count, size_t offset, size_t *completed, hx_os_error *error)
{
	size_t i, used = 0, total = 0;
#if defined(_WIN32)
	WSABUF native[64];
	DWORD result = 0, flags = 0;
	int code;
#else
	struct iovec native[64];
	struct msghdr message;
	ssize_t result;
	int flags = 0;
#endif

	*completed = 0;
	for (i = 0; i < count && total < INT_MAX; ++i) {
		size_t length = send ? send[i].bytes : receive[i].bytes;
		const unsigned char *data =
			send ? send[i].data : receive[i].data;

		if (offset >= length) {
			offset -= length;
			continue;
		}
		data += offset;
		length -= offset;
		offset = 0;
		if (length > (size_t)INT_MAX - total)
			length = (size_t)INT_MAX - total;
#if defined(_WIN32)
		native[used].buf = (char *)data;
		native[used].len = (ULONG)length;
#else
		native[used].iov_base = (void *)data;
		native[used].iov_len = length;
#endif
		total += length;
		++used;
	}
	if (!total)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
#if defined(_WIN32)
	code = send ? WSASend(socket->native, native, (DWORD)used, &result, 0,
			      NULL, NULL)
		    : WSARecv(socket->native, native, (DWORD)used, &result,
			      &flags, NULL, NULL);
	if (code)
		return hx_os_i_socket_error(error, WSAGetLastError());
#else
	memset(&message, 0, sizeof(message));
	message.msg_iov = native;
	message.msg_iovlen = used;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif
	result = send ? sendmsg(socket->native, &message, flags)
		      : recvmsg(socket->native, &message, 0);
	if (result < 0)
		return hx_os_i_socket_error(error, errno);
#endif
	*completed = (size_t)result;

	return hx_os_i_error(error,
		result ? HX_OS_OK : (send ? HX_OS_IO_ERROR : HX_OS_EOF), 0, 0);
}

static int hx_os_vectors_interrupted(const hx_os_error *error)
{
#if defined(_WIN32)
	return error->domain == HX_OS_ERROR_WINSOCK &&
		error->native_code == WSAEINTR;
#else
	return error->domain == HX_OS_ERROR_ERRNO &&
		error->native_code == EINTR;
#endif
}
#endif

static hx_os_status hx_os_vectors_some(hx_os_socket *socket,
	const hx_os_send_buffer *send, const hx_os_receive_buffer *receive,
	size_t count, size_t *completed, hx_os_error *error)
{
	if (!completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = 0;
#if HX_OS_ENABLE_NETWORK
	{
		size_t total;
		hx_os_status status = hx_os_vectors_check(
			socket, send, receive, count, &total, error);
		if (status != HX_OS_OK)
			return status;

		return hx_os_vectors_transfer(
			socket, send, receive, count, 0, completed, error);
	}
#else
	(void)socket;
	(void)send;
	(void)receive;
	(void)count;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

static hx_os_status hx_os_vectors_until(hx_os_socket *socket,
	const hx_os_send_buffer *send, const hx_os_receive_buffer *receive,
	size_t count, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error)
{
	if (!completed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*completed = 0;
#if HX_OS_ENABLE_NETWORK
	{
		hx_os_error native;
		size_t total, progress;
		hx_os_status status = hx_os_vectors_check(
			socket, send, receive, count, &total, &native);

		while (status == HX_OS_OK) {
			status = hx_os_vectors_transfer(socket, send, receive,
				count, *completed, &progress, &native);
			*completed += progress;
			if ((status == HX_OS_OK &&
				    (!send || *completed == total)) ||
				(status != HX_OS_OK &&
					status != HX_OS_WOULD_BLOCK &&
					!hx_os_vectors_interrupted(&native)))
				break;

			if (hx_os_deadline_expired(deadline)) {
				status = hx_os_i_error(
					&native, HX_OS_TIMEOUT, 0, 0);
				break;
			}
			if (status == HX_OS_WOULD_BLOCK)
				status = hx_os_socket_wait_until(socket,
					send ? HX_OS_WAIT_WRITE
					     : HX_OS_WAIT_READ,
					deadline, &native);
			else
				status = HX_OS_OK;
		}
		if (error)
			*error = native;

		return status;
	}
#else
	(void)socket;
	(void)send;
	(void)receive;
	(void)count;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_socket_sendv_some(hx_os_socket *socket,
	const hx_os_send_buffer *buffers, size_t count, size_t *completed,
	hx_os_error *error)
{
	return hx_os_vectors_some(
		socket, buffers, NULL, count, completed, error);
}

hx_os_status hx_os_socket_receivev_some(hx_os_socket *socket,
	const hx_os_receive_buffer *buffers, size_t count, size_t *completed,
	hx_os_error *error)
{
	return hx_os_vectors_some(
		socket, NULL, buffers, count, completed, error);
}

hx_os_status hx_os_socket_sendv_until(hx_os_socket *socket,
	const hx_os_send_buffer *buffers, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error)
{
	return hx_os_vectors_until(
		socket, buffers, NULL, count, deadline, completed, error);
}

hx_os_status hx_os_socket_receivev_until(hx_os_socket *socket,
	const hx_os_receive_buffer *buffers, size_t count,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error)
{
	return hx_os_vectors_until(
		socket, NULL, buffers, count, deadline, completed, error);
}
