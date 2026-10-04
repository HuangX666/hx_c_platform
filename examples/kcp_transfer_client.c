#include "kcp_business.h"

typedef struct transfer_job {
	kcp_session *session;
	int ok;
} transfer_job;

static void *HX_OS_CALL send_blocks(void *user)
{
	transfer_job *job = user;
	unsigned int id;

	for (id = 1; id <= 3; ++id) {
		json_field fields[] = {json_text("type", "block"),
			json_int("id", id), json_int("value", id * 10)};

		hx_string wire = HX_STRING_INIT;

		int ok = json_encode(fields, 3, &wire) &&
			kcp_send_text(job->session, hx_string_c_str(&wire));
		if (ok)
			printf("[KCP producer thread] %s\n",
				hx_string_c_str(&wire));

		hx_string_destroy(&wire);
		if (!ok)
			return NULL;
	}

	job->ok = 1;

	return NULL;
}

static void *HX_OS_CALL receive_confirmations(void *user)
{
	transfer_job *job = user;
	unsigned int received = 0;
	hx_os_deadline linger = HX_OS_DEADLINE_INFINITE;

	for (;;) {
		char reply[KCP_TEXT_CAPACITY];
		hx_os_status status;

		if (!kcp_pump(job->session))
			return NULL;

		while ((status = kcp_receive_text(job->session, reply)) ==
			HX_OS_OK) {
			hx_os_config_document *document =
				json_decode(reply, strlen(reply));
			const hx_os_config_node *root =
				hx_os_config_root(document);

			int ok = document && json_has_bool(root, "ok", 1);
			if (received < 3)
				ok = ok &&
					json_has_text(
						root, "type", "applied") &&
					json_has_int(root, "id", received + 1);
			else
				ok = ok && received == 3 &&
					json_has_text(
						root, "type", "committed") &&
					json_has_int(root, "blocks", 3) &&
					json_has_int(root, "total", 60);

			hx_os_config_release(document);
			if (!ok) {
				fputs("Unexpected JSON KCP result.\n", stderr);

				return NULL;
			}

			++received;
			printf("[KCP I/O thread] %s\n", reply);
		}

		if (status != HX_OS_WOULD_BLOCK) {
			(void)net_demo_check(status, NULL, "KCP receive");

			return NULL;
		}

		if (received == 4 && kcp_drained(job->session)) {
			/* Keep servicing duplicate packets briefly so lost
			 * final ACKs can be sent again. This is a finite demo
			 * policy. */
			if (linger == HX_OS_DEADLINE_INFINITE)
				linger = hx_os_deadline_after_ms(500);

			if (hx_os_deadline_expired(linger))
				break;
		}

		(void)hx_os_sleep_until(hx_os_deadline_after_ms(5), NULL);
	}

	job->ok = 1;

	return NULL;
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39003;

	hx_os_network *network = NULL;
	kcp_session session = {0};
	hx_os_thread *threads[2] = {NULL};
	transfer_job sender = {&session, 0}, receiver = {&session, 0};
	hx_os_error error = {0};
	hx_os_status status;
	unsigned int started = 0, index;
	int ok;

	if (argc > 3 || (argc == 3 && strcmp(argv[2], "--drop-first")))
		return 2;

	session.drop_first = argc == 3;
	ok = net_demo_open(&network, &session.socket, HX_OS_UDP, 0);
	if (ok)
		ok = net_demo_check(hx_os_address_parse("127.0.0.1", port,
					    &session.peer, &error),
			&error, "server address");

	if (ok) {
		session.peer_known = 1;
		ok = kcp_start(&session);
	}

	if (ok) {
		status = hx_os_thread_create(NULL, receive_confirmations,
			&receiver, &threads[0], &error);
		if (status == HX_OS_OK) {
			started = 1;

			status = hx_os_thread_create(NULL, send_blocks, &sender,
				&threads[1], &error);
			if (status == HX_OS_OK)
				started = 2;
		}

		ok = net_demo_check(status, &error, "start KCP threads");
	}

	for (index = 0; index < started; ++index)
		net_demo_join(&threads[index], hx_os_deadline_after_ms(12000));

	ok = ok && sender.ok && receiver.ok;

	hx_os_kcp_destroy(session.engine);
	hx_os_mutex_destroy(session.access);
	if (!net_demo_close(&session.socket, &network))
		ok = 0;

	if (ok)
		printf("KCP CLIENT PASS: 3 blocks committed; dropped=%u\n",
			session.dropped);

	return ok ? 0 : 1;
}
