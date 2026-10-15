#include "ipc_private.h"
#include "hx_os_kcp_session.h"

struct hx_os_kcp_session {
	hx_ipc *endpoint;
};

hx_os_status hx_os_kcp_session_options_init(
	hx_os_kcp_session_options *options, size_t capacity)
{
	return hx_ipc_options_init(options, capacity);
}

static hx_os_status hx_os_kcp_session_open(const char *address,
	const hx_os_kcp_session_options *options, int server,
	hx_os_deadline deadline, hx_os_kcp_session **out, hx_os_error *error)
{
	hx_os_kcp_session *session;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	session = hx_os_i_zero(NULL, sizeof(*session));
	if (!session)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	status = hx_ipc_i_open_kcp(NULL, address, options, server, deadline,
		&session->endpoint, error);
	if (session->endpoint)
		*out = session;
	else
		hx_os_memory_free(session);
	return status;
}

hx_os_status hx_os_kcp_session_listen(const char *endpoint,
	const hx_os_kcp_session_options *options, hx_os_kcp_session **out,
	hx_os_error *error)
{
	return hx_os_kcp_session_open(
		endpoint, options, 1, HX_OS_DEADLINE_NOW, out, error);
}

hx_os_status hx_os_kcp_session_connect_until(const char *endpoint,
	const hx_os_kcp_session_options *options, hx_os_deadline deadline,
	hx_os_kcp_session **out, hx_os_error *error)
{
	return hx_os_kcp_session_open(
		endpoint, options, 0, deadline, out, error);
}

hx_os_status hx_os_kcp_session_wait_until(hx_os_kcp_session *session,
	hx_os_deadline deadline, uint64_t *peer, hx_os_error *error)
{
	return hx_ipc_wait_connected_until(
		session ? session->endpoint : NULL, deadline, peer, error);
}

hx_os_status hx_os_kcp_session_address(
	hx_os_kcp_session *session, hx_string *out, hx_os_error *error)
{
	return hx_ipc_address(session ? session->endpoint : NULL, out, error);
}

hx_os_status hx_os_kcp_session_send_until(hx_os_kcp_session *session,
	uint64_t peer, uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_ipc_send_until(session ? session->endpoint : NULL, peer,
		channel, data, bytes, deadline, error);
}

hx_os_status hx_os_kcp_session_receive_until(hx_os_kcp_session *session,
	hx_os_deadline deadline, hx_os_kcp_message **message,
	hx_os_error *error)
{
	return hx_ipc_receive_until(
		session ? session->endpoint : NULL, deadline, message, error);
}

void hx_os_kcp_message_release(hx_os_kcp_message *message)
{
	hx_ipc_message_release(message);
}

hx_os_status hx_os_kcp_session_request_until(hx_os_kcp_session *session,
	uint64_t peer, uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_kcp_message **reply, hx_os_error *error)
{
	return hx_ipc_request_until(session ? session->endpoint : NULL, peer,
		channel, data, bytes, deadline, reply, error);
}

hx_os_status hx_os_kcp_session_reply_until(hx_os_kcp_session *session,
	const hx_os_kcp_message *request, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error)
{
	return hx_ipc_reply_until(session ? session->endpoint : NULL, request,
		data, bytes, deadline, error);
}

hx_os_status hx_os_kcp_session_close_until(
	hx_os_kcp_session **owner, hx_os_deadline deadline, hx_os_error *error)
{
	hx_os_status status;

	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*owner)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	status = hx_ipc_close_until(&(*owner)->endpoint, deadline, error);
	if (status == HX_OS_OK) {
		hx_os_memory_free(*owner);
		*owner = NULL;
	}
	return status;
}
