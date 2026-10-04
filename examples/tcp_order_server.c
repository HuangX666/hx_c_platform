#include "tcp_business.h"

static int process_orders(tcp_stream *stream)
{
	unsigned int received, seen = 0, accepted = 0, total = 0;
	for (received = 0; received < 3; ++received) {
		char request[TCP_TEXT_CAPACITY], reply[TCP_TEXT_CAPACITY];
		unsigned int id, sku, quantity;
		int consumed = -1;
		if (!tcp_receive_text(stream, request))
			return 0;
		if (sscanf(request, "ORDER id=%9u sku=%9u qty=%9u%n", &id, &sku,
			    &quantity, &consumed) != 3 ||
			consumed < 0 || (size_t)consumed != strlen(request) ||
			id < 1 || id > 3 || (seen & (1u << id)))
			return net_demo_check(HX_OS_SYNTAX, NULL, "order");
		seen |= 1u << id;
		printf("[TCP server] %s\n", request);
		if ((sku != 101 && sku != 102) || !quantity || quantity > 10)
			(void)snprintf(reply, sizeof(reply),
				"REJECTED id=%u reason=invalid_order", id);
		else {
			unsigned int amount =
				quantity * (sku == 101 ? 1250 : 9000);
			total += amount;
			++accepted;
			(void)snprintf(reply, sizeof(reply),
				"RECEIPT id=%u total_cents=%u", id, amount);
		}
		if (!tcp_send_text(stream, reply))
			return 0;
		if (strncmp(reply, "RECEIPT ", 8))
			continue;
		(void)snprintf(reply, sizeof(reply), "SHIPPED id=%u", id);
		if (!tcp_send_text(stream, reply))
			return 0;
	}
	printf("TCP SERVER PASS: accepted=%u total_cents=%u\n", accepted,
		total);
	return accepted == 2 && total == 11500;
}

int main(int argc, char **argv)
{
	uint16_t port = argc > 1 ? net_demo_port(argv[1]) : 39001;
	hx_os_network *network = NULL;
	hx_os_socket *listener = NULL;
	tcp_stream stream = {0};
	hx_os_error error = {0};
	hx_os_status status = HX_OS_OK;
	int ok = net_demo_open(&network, &listener, HX_OS_TCP, port);
	if (ok)
		ok = net_demo_check(hx_os_socket_listen(listener, 1, &error),
			     &error, "listen") &&
			net_demo_listening(listener);
	stream.deadline = hx_os_deadline_after_ms(15000);
	if (ok) {
		status = hx_os_socket_wait_until(
			listener, HX_OS_WAIT_READ, stream.deadline, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_accept(
				listener, &stream.socket, NULL, &error);
		ok = net_demo_check(status, &error, "accept");
	}
	if (ok)
		ok = net_demo_check(
			hx_os_mutex_create(NULL, 0, &stream.access, &error),
			&error, "socket mutex");
	if (ok)
		ok = process_orders(&stream);
	hx_os_mutex_destroy(stream.access);
	if (!net_demo_check(hx_os_socket_close(&stream.socket, &error), &error,
		    "client close"))
		ok = 0;
	if (!net_demo_close(&listener, &network))
		ok = 0;
	return ok ? 0 : 1;
}
