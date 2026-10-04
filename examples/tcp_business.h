#ifndef HX_EXAMPLE_TCP_BUSINESS_H
#define HX_EXAMPLE_TCP_BUSINESS_H

#include "network_demo.h"
#include "business_json.h"

enum { TCP_TEXT_CAPACITY = JSON_MESSAGE_CAPACITY };

typedef struct tcp_stream {
	hx_os_socket *socket;
	hx_os_mutex *access;
	hx_os_deadline deadline;
} tcp_stream;

/* One sender and one receiver may use this stream. Native calls are short
 * nonblocking operations serialized by access. No lock covers a sleep.
 * A partial frame followed by failure is fatal to this demo connection.
 */
static inline int tcp_transfer(
	tcp_stream *stream, void *data, size_t bytes, int sending)
{
	size_t offset = 0;
	while (offset < bytes) {
		size_t completed = 0;
		hx_os_error error = {0};
		hx_os_status status;
		if (hx_os_deadline_expired(stream->deadline))
			return net_demo_check(HX_OS_TIMEOUT, NULL, "TCP frame");
		if (!net_demo_check(hx_os_mutex_lock(stream->access, &error),
			    &error, "socket exclusion"))
			return 0;
		if (sending)
			status = hx_os_socket_send_some(stream->socket,
				(unsigned char *)data + offset, bytes - offset,
				&completed, &error);
		else
			status = hx_os_socket_receive_some(stream->socket,
				(unsigned char *)data + offset, bytes - offset,
				&completed, &error);
		(void)hx_os_mutex_unlock(stream->access, NULL);
		if (status == HX_OS_WOULD_BLOCK) {
			(void)hx_os_sleep_until(
				hx_os_deadline_after_ms(1), NULL);
			continue;
		}
		if (!net_demo_check(status, &error, "TCP I/O"))
			return 0;
		if (!completed)
			return net_demo_check(
				HX_OS_CLOSED, NULL, "TCP progress");
		offset += completed;
	}
	return 1;
}

/* Wire format: two-byte big-endian length, then that many UTF-8 JSON bytes.
 * TCP has no message boundaries. Keep this framing local to this example.
 */
static inline int tcp_send_text(tcp_stream *stream, const char *text)
{
	unsigned char frame[2 + TCP_TEXT_CAPACITY];
	size_t bytes = strlen(text);
	if (!bytes || bytes >= TCP_TEXT_CAPACITY)
		return net_demo_check(HX_OS_LIMIT, NULL, "TCP message length");
	frame[0] = (unsigned char)(bytes >> 8);
	frame[1] = (unsigned char)bytes;
	memcpy(frame + 2, text, bytes);
	return tcp_transfer(stream, frame, bytes + 2, 1);
}

static inline int tcp_receive_text(
	tcp_stream *stream, char text[TCP_TEXT_CAPACITY])
{
	unsigned char header[2];
	size_t bytes;
	if (!tcp_transfer(stream, header, sizeof(header), 0))
		return 0;
	bytes = (size_t)header[0] * 256 + header[1];
	if (!bytes || bytes >= TCP_TEXT_CAPACITY)
		return net_demo_check(HX_OS_LIMIT, NULL, "TCP message length");
	if (!tcp_transfer(stream, text, bytes, 0))
		return 0;
	if (memchr(text, '\0', bytes))
		return net_demo_check(HX_OS_SYNTAX, NULL, "TCP embedded NUL");
	text[bytes] = '\0';
	return 1;
}

#endif
