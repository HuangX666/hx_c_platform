#include <hx_os.h>
#include <stdio.h>
#include <string.h>

static hx_os_status serve(hx_os_kcp_session *session)
{
	const char reply[] = "{\"accepted\":true,\"device_id\":7}";
	hx_os_deadline deadline = hx_os_deadline_after_ms(30000);
	hx_os_status status;
	hx_os_kcp_message *message = NULL;

	for (;;) {
		status = hx_os_kcp_session_receive_until(
			session, deadline, &message, NULL);
		if (status != HX_OS_OK)
			return status;
		if (message->kind == HX_OS_KCP_REQUEST) {
			printf("Request: %.*s\n", (int)message->bytes,
				(const char *)message->data);
			status = hx_os_kcp_session_reply_until(session, message,
				reply, sizeof(reply) - 1,
				hx_os_deadline_after_ms(3000), NULL);
			hx_os_kcp_message_release(message);
			return status;
		}
		hx_os_kcp_message_release(message);
	}
}

static hx_os_status request(hx_os_kcp_session *session)
{
	const char json[] = "{\"type\":\"status\",\"device_id\":7}";
	hx_os_kcp_message *reply = NULL;
	hx_os_status status = hx_os_kcp_session_request_until(session, 0, 7,
		json, sizeof(json) - 1, hx_os_deadline_after_ms(3000), &reply,
		NULL);

	if (status == HX_OS_OK)
		printf("Reply: %.*s\n", (int)reply->bytes,
			(const char *)reply->data);
	hx_os_kcp_message_release(reply);
	return status;
}

int main(int argc, char **argv)
{
	hx_os_kcp_session *session = NULL;
	hx_os_status status, closed;
	int server;

	if (argc != 3 ||
		(strcmp(argv[1], "server") && strcmp(argv[1], "client"))) {
		fprintf(stderr, "Usage: %s server|client IP:PORT\n", argv[0]);
		return 2;
	}
	server = !strcmp(argv[1], "server");
	status = server
		? hx_os_kcp_session_listen(argv[2], NULL, &session, NULL)
		: hx_os_kcp_session_connect_until(argv[2], NULL,
			  hx_os_deadline_after_ms(3000), &session, NULL);
	if (status == HX_OS_OK)
		status = server ? serve(session) : request(session);
	if (status != HX_OS_OK)
		fprintf(stderr, "KCP: %s\n", hx_os_status_name(status));

	closed = hx_os_kcp_session_close_until(
		&session, hx_os_deadline_after_ms(3000), NULL);
	if (closed != HX_OS_OK) {
		fprintf(stderr, "Close: %s; keep owner to retry\n",
			hx_os_status_name(closed));
		return 1;
	}
	return status == HX_OS_OK ? 0 : 1;
}
