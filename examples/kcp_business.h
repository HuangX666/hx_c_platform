#ifndef HX_EXAMPLE_KCP_BUSINESS_H
#define HX_EXAMPLE_KCP_BUSINESS_H

#include "network_demo.h"
#include <hx_os_kcp.h>

enum { KCP_TEXT_CAPACITY = 128, KCP_DEMO_CONV = 7301 };

typedef struct kcp_session {
	hx_os_socket *socket;
	hx_os_kcp *engine;
	hx_os_mutex *access;
	hx_os_address peer;
	hx_os_deadline deadline;
	int peer_known;
	int drop_first;
	unsigned int dropped;
} kcp_session;

/* Called inline by update/flush with access held. No blocking or reentry.
 * A dropped datagram is reported as locally sent; KCP must retransmit it.
 */
static hx_os_status HX_OS_CALL kcp_output(
	const void *data, size_t bytes, void *user, hx_os_error *error)
{
	kcp_session *session = user;
	size_t completed = 0;
	hx_os_status status;
	if (!session->peer_known)
		return HX_OS_INVALID;
	if (session->drop_first && !session->dropped) {
		++session->dropped;
		puts("[KCP injection] first outgoing datagram dropped");
		return HX_OS_OK;
	}
	status = hx_os_socket_send_to(session->socket, &session->peer, data,
		bytes, &completed, error);
	/* Backpressure drops this transmission, retained segments retry. */
	if (status == HX_OS_WOULD_BLOCK)
		return HX_OS_OK;
	return status == HX_OS_OK && completed != bytes ? HX_OS_IO_ERROR
							: status;
}

static inline int kcp_start(kcp_session *session)
{
	hx_os_error error = {0};
	hx_os_kcp_options options;
	hx_os_status status = hx_os_kcp_options_init(&options, sizeof(options));
	options.max_message_bytes = KCP_TEXT_CAPACITY - 1;
	options.max_send_segments = 16;
	options.receive_window = 128;
	options.mtu = 1200;
	if (status == HX_OS_OK)
		status = hx_os_mutex_create(NULL, 0, &session->access, &error);
	if (status == HX_OS_OK)
		status = hx_os_kcp_create(NULL, KCP_DEMO_CONV, &options,
			kcp_output, session, &session->engine, &error);
	session->deadline = hx_os_deadline_after_ms(10000);
	return net_demo_check(status, &error, "KCP create");
}

/* Exactly one I/O thread drives this function. Producers only enqueue via
 * kcp_send_text. The short mutex protects every operation on the engine.
 * This demo serves one peer/conversation per process lifetime.
 */
static inline int kcp_pump(kcp_session *session)
{
	hx_os_error error = {0};
	hx_os_status status;
	unsigned int count;
	if (hx_os_deadline_expired(session->deadline))
		return net_demo_check(HX_OS_TIMEOUT, NULL, "KCP session");
	status = hx_os_mutex_lock(session->access, &error);
	if (!net_demo_check(status, &error, "KCP exclusion"))
		return 0;
	for (count = 0; count < 32; ++count) {
		unsigned char packet[1400];
		hx_os_address peer;
		size_t bytes = 0;
		status = hx_os_socket_receive_from(session->socket, &peer,
			packet, sizeof(packet), &bytes, &error);
		if (status == HX_OS_WOULD_BLOCK) {
			status = HX_OS_OK;
			break;
		}
		if (status != HX_OS_OK)
			break;
		if (session->peer_known &&
			!net_demo_same_peer(&peer, &session->peer))
			continue;
		status =
			hx_os_kcp_input(session->engine, packet, bytes, &error);
		if (status == HX_OS_WOULD_BLOCK) {
			status = HX_OS_OK;
			continue;
		}
		if (status != HX_OS_OK)
			break;
		session->peer = peer;
		session->peer_known = 1;
	}
	if (status == HX_OS_OK)
		status = hx_os_kcp_update(session->engine,
			hx_os_monotonic_ns() / UINT64_C(1000000), &error);
	if (status == HX_OS_OK)
		status = hx_os_kcp_flush(session->engine, &error);
	(void)hx_os_mutex_unlock(session->access, NULL);
	return net_demo_check(status, &error, "KCP UDP/update");
}

static inline int kcp_send_text(kcp_session *session, const char *text)
{
	hx_os_error error = {0};
	hx_os_status status = hx_os_mutex_lock(session->access, &error);
	if (!net_demo_check(status, &error, "KCP exclusion"))
		return 0;
	status = hx_os_kcp_send(session->engine, text, strlen(text), &error);
	(void)hx_os_mutex_unlock(session->access, NULL);
	return net_demo_check(status, &error, "KCP enqueue");
}

static inline hx_os_status kcp_receive_text(
	kcp_session *session, char text[KCP_TEXT_CAPACITY])
{
	size_t bytes = 0;
	hx_os_status status = hx_os_mutex_lock(session->access, NULL);
	if (status != HX_OS_OK)
		return status;
	status = hx_os_kcp_receive(
		session->engine, text, KCP_TEXT_CAPACITY - 1, &bytes, NULL);
	(void)hx_os_mutex_unlock(session->access, NULL);
	if (status == HX_OS_OK) {
		if (memchr(text, '\0', bytes))
			return HX_OS_SYNTAX;
		text[bytes] = '\0';
	}
	return status;
}

static inline int kcp_drained(kcp_session *session)
{
	size_t pending;
	if (hx_os_mutex_lock(session->access, NULL) != HX_OS_OK)
		return 0;
	pending = hx_os_kcp_pending_segments(session->engine);
	(void)hx_os_mutex_unlock(session->access, NULL);
	return !pending;
}

#endif
