#include "tcp_business.h"

typedef struct order_job {
	tcp_stream *stream;
	int ok;
} order_job;

static void *HX_OS_CALL send_orders(void *user)
{
	order_job *job = user;
	const char *orders[] = {"ORDER id=1 sku=101 qty=2",
		"ORDER id=2 sku=102 qty=1", "ORDER id=3 sku=101 qty=0"};
	size_t index;
	for (index = 0; index < 3; ++index) {
		if (!tcp_send_text(job->stream, orders[index]))
			return NULL;
		printf("[TCP send thread] %s\n", orders[index]);
	}
	job->ok = 1;
	return NULL;
}

static void *HX_OS_CALL receive_receipts(void *user)
{
	order_job *job = user;
	const char *expected[] = {"RECEIPT id=1 total_cents=2500",
		"SHIPPED id=1", "RECEIPT id=2 total_cents=9000", "SHIPPED id=2",
		"REJECTED id=3 reason=invalid_order"};
	size_t index;
	for (index = 0; index < 5; ++index) {
		char reply[TCP_TEXT_CAPACITY];
		if (!tcp_receive_text(job->stream, reply))
			return NULL;
		printf("[TCP receive thread] %s\n", reply);
		if (strcmp(reply, expected[index])) {
			fprintf(stderr, "Expected: %s\n", expected[index]);
			return NULL;
		}
	}
	job->ok = 1;
	return NULL;
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39001;
	hx_os_network *network = NULL;
	tcp_stream stream = {0};
	hx_os_address address;
	hx_os_thread *threads[2] = {NULL};
	order_job sender = {&stream, 0}, receiver = {&stream, 0};
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	unsigned int started = 0, index;
	int ok = net_demo_open(&network, &stream.socket, HX_OS_TCP, 0);
	stream.deadline = hx_os_deadline_after_ms(10000);
	if (ok) {
		status = hx_os_address_parse(
			"127.0.0.1", port, &address, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_connect_until(stream.socket,
				&address, stream.deadline, &error);
		if (status == HX_OS_OK)
			status = hx_os_mutex_create(
				NULL, 0, &stream.access, &error);
		ok = net_demo_check(status, &error, "connect TCP");
	}
	if (ok) {
		status = hx_os_thread_create(
			NULL, receive_receipts, &receiver, &threads[0], &error);
		if (status == HX_OS_OK) {
			started = 1;
			status = hx_os_thread_create(NULL, send_orders, &sender,
				&threads[1], &error);
			if (status == HX_OS_OK)
				started = 2;
		}
		ok = net_demo_check(status, &error, "start TCP threads");
	}
	for (index = 0; index < started; ++index)
		net_demo_join(&threads[index], hx_os_deadline_after_ms(12000));
	ok = ok && sender.ok && receiver.ok;
	hx_os_mutex_destroy(stream.access);
	if (!net_demo_close(&stream.socket, &network))
		ok = 0;
	if (ok)
		puts("TCP CLIENT PASS: 3 orders, 2 receipts, 2 notices, 1 "
		     "rejection");
	return ok ? 0 : 1;
}
