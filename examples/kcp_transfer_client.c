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
		char block[KCP_TEXT_CAPACITY];
		(void)snprintf(block, sizeof(block), "BLOCK id=%u value=%u", id,
			id * 10);
		if (!kcp_send_text(job->session, block))
			return NULL;
		printf("[KCP producer thread] %s\n", block);
	}
	job->ok = 1;
	return NULL;
}

static void *HX_OS_CALL receive_confirmations(void *user)
{
	transfer_job *job = user;
	const char *expected[] = {"APPLIED id=1", "APPLIED id=2",
		"APPLIED id=3", "COMMITTED blocks=3 total=60"};
	unsigned int received = 0;
	hx_os_deadline linger = HX_OS_DEADLINE_INFINITE;
	for (;;) {
		char reply[KCP_TEXT_CAPACITY];
		hx_os_status status;
		if (!kcp_pump(job->session))
			return NULL;
		while ((status = kcp_receive_text(job->session, reply)) ==
			HX_OS_OK) {
			if (received >= 4 ||
				strcmp(reply, expected[received])) {
				fputs("Unexpected KCP business reply.\n",
					stderr);
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
