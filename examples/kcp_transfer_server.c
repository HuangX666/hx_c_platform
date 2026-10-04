#include "kcp_business.h"

static int send_confirmation(
	kcp_session *session, const json_field *fields, size_t count)
{
	hx_string wire = HX_STRING_INIT;

	int ok = json_encode(fields, count, &wire) &&
		kcp_send_text(session, hx_string_c_str(&wire));

	hx_string_destroy(&wire);

	return ok;
}

static int receive_blocks(kcp_session *session)
{
	unsigned int received = 0, total = 0;

	for (;;) {
		char block[KCP_TEXT_CAPACITY];
		hx_os_status status;

		if (!kcp_pump(session))
			return 0;

		while ((status = kcp_receive_text(session, block)) ==
			HX_OS_OK) {
			hx_os_config_document *document =
				json_decode(block, strlen(block));
			const hx_os_config_node *root =
				hx_os_config_root(document);
			int64_t id, value;
			int ok = document &&
				json_has_text(root, "type", "block") &&
				json_get_int(root, "id", &id) &&
				id == received + 1 && id <= 3 &&
				json_get_int(root, "value", &value) &&
				value == id * 10;

			hx_os_config_release(document);
			if (!ok)
				return net_demo_check(
					HX_OS_SYNTAX, NULL, "JSON block");

			++received;
			total += (unsigned int)value;
			printf("[KCP server] %s\n", block);
			{
				json_field fields[] = {
					json_text("type", "applied"),
					json_int("id", id), json_bool("ok", 1)};

				if (!send_confirmation(session, fields, 3))
					return 0;
			}

			if (received == 3) {
				json_field fields[] = {
					json_text("type", "committed"),
					json_int("blocks", received),
					json_int("total", total),
					json_bool("ok", 1)};

				if (!send_confirmation(session, fields, 4))
					return 0;
			}
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
