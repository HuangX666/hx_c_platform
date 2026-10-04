#include "kcp_business.h"

static int receive_blocks(kcp_session *session)
{
	unsigned int received = 0, total = 0;
	for (;;) {
		char block[KCP_TEXT_CAPACITY], reply[KCP_TEXT_CAPACITY];
		hx_os_status status;
		if (!kcp_pump(session))
			return 0;
		while ((status = kcp_receive_text(session, block)) ==
			HX_OS_OK) {
			unsigned int id, value;
			int consumed = -1;
			if (sscanf(block, "BLOCK id=%9u value=%9u%n", &id,
				    &value, &consumed) != 2 ||
				consumed < 0 ||
				(size_t)consumed != strlen(block) ||
				id != received + 1 || id > 3 ||
				value != id * 10)
				return net_demo_check(
					HX_OS_SYNTAX, NULL, "block");
			++received;
			total += value;
			printf("[KCP server] apply %s\n", block);
			(void)snprintf(
				reply, sizeof(reply), "APPLIED id=%u", id);
			if (!kcp_send_text(session, reply))
				return 0;
			if (received == 3 &&
				!kcp_send_text(
					session, "COMMITTED blocks=3 total=60"))
				return 0;
		}
		if (status != HX_OS_WOULD_BLOCK)
			return net_demo_check(status, NULL, "KCP receive");
		if (received == 3 && kcp_drained(session)) {
			printf("KCP SERVER PASS: blocks=3 total=%u "
			       "dropped=%u\n",
				total, session->dropped);
			return total == 60;
		}
		(void)hx_os_sleep_until(hx_os_deadline_after_ms(5), NULL);
	}
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39003;
	hx_os_network *network = NULL;
	kcp_session session = {0};
	int ok;
	if (argc > 3 || (argc == 3 && strcmp(argv[2], "--drop-first")))
		return 2;
	session.drop_first = argc == 3;
	ok = net_demo_open(&network, &session.socket, HX_OS_UDP, port);
	if (ok)
		ok = kcp_start(&session) && net_demo_listening(session.socket);
	if (ok)
		ok = receive_blocks(&session);
	hx_os_kcp_destroy(session.engine);
	hx_os_mutex_destroy(session.access);
	if (!net_demo_close(&session.socket, &network))
		ok = 0;
	return ok ? 0 : 1;
}
