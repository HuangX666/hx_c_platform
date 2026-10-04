#ifndef HX_EXAMPLE_UDP_BUSINESS_H
#define HX_EXAMPLE_UDP_BUSINESS_H

#include "network_demo.h"
#include "business_json.h"

enum { UDP_TEXT_CAPACITY = JSON_MESSAGE_CAPACITY };

typedef struct udp_endpoint {
	hx_os_socket *socket;
	hx_os_mutex *access;
} udp_endpoint;

static inline int udp_send_text(
	udp_endpoint *endpoint, const hx_os_address *peer, const char *text)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(1000);

	size_t bytes = strlen(text);
	if (bytes >= UDP_TEXT_CAPACITY)
		return net_demo_check(HX_OS_LIMIT, NULL, "UDP payload");

	for (;;) {
		size_t completed = 0;
		hx_os_error error = {0};
		hx_os_status status;

		if (hx_os_deadline_expired(deadline))
			return net_demo_check(HX_OS_TIMEOUT, NULL, "UDP send");

		if (!net_demo_check(hx_os_mutex_lock(endpoint->access, &error),
			    &error, "UDP exclusion"))
			return 0;
		status = hx_os_socket_send_to(endpoint->socket, peer, text,
			bytes, &completed, &error);
		(void)hx_os_mutex_unlock(endpoint->access, NULL);
		if (status == HX_OS_WOULD_BLOCK) {
			(void)hx_os_sleep_until(
				hx_os_deadline_after_ms(1), NULL);
			continue;
		}

		return net_demo_check(status, &error, "UDP send") &&
			completed == bytes;
	}
}

/* One datagram per call, no stream framing or implicit session protocol.
 * The mutex covers only a nonblocking call, never an entire receive wait.
 */
static inline hx_os_status udp_receive_text(udp_endpoint *endpoint,
	hx_os_address *peer, char text[UDP_TEXT_CAPACITY],
	hx_os_deadline deadline, hx_os_error *error)
{
	for (;;) {
		size_t bytes = 0;
		hx_os_status status;

		if (hx_os_deadline_expired(deadline))
			return HX_OS_TIMEOUT;

		status = hx_os_mutex_lock(endpoint->access, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_socket_receive_from(endpoint->socket, peer, text,
			UDP_TEXT_CAPACITY - 1, &bytes, error);
		(void)hx_os_mutex_unlock(endpoint->access, NULL);
		if (status == HX_OS_WOULD_BLOCK) {
			(void)hx_os_sleep_until(
				hx_os_deadline_after_ms(1), NULL);
			continue;
		}

		if (status != HX_OS_OK)
			return status;

		if (memchr(text, '\0', bytes))
			return HX_OS_SYNTAX;

		text[bytes] = '\0';

		return HX_OS_OK;
	}
}

#endif
