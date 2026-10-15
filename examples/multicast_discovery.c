#include <hx_os.h>
#include "network_demo.h"

static int parse_index(const char *text, uint32_t *index)
{
	char *end;
	unsigned long value;

	if (*text < '0' || *text > '9')
		return 0;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || *end || value > UINT32_MAX)
		return 0;

	*index = (uint32_t)value;
	return 1;
}

static hx_os_status send_discovery(hx_os_socket *socket,
	const hx_os_address *group, hx_os_error *error)
{
	const char message[] =
		"{\"type\":\"discover\",\"request_id\":\"demo-1\"}";
	hx_os_deadline deadline = hx_os_deadline_after_ms(3000);
	size_t sent = 0;
	hx_os_status status;

	for (;;) {
		status = hx_os_socket_send_to(socket, group, message,
			sizeof(message) - 1, &sent, error);
		if (status != HX_OS_WOULD_BLOCK)
			break;

		status = hx_os_socket_wait_until(
			socket, HX_OS_WAIT_WRITE, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	if (status == HX_OS_OK)
		printf("Sent %zu bytes: %s\n", sent, message);

	return status;
}

static hx_os_status receive_discovery(hx_os_socket *socket,
	hx_os_error *error)
{
	char message[1024], host[64];
	hx_os_address sender;
	hx_os_deadline deadline = hx_os_deadline_after_ms(10000);
	size_t received = 0;
	hx_os_status status;

	puts("Listening for one discovery datagram (10 seconds)...");
	fflush(stdout);
	for (;;) {
		status = hx_os_socket_receive_from(socket, &sender, message,
			sizeof(message), &received, error);
		if (status != HX_OS_WOULD_BLOCK)
			break;

		status = hx_os_socket_wait_until(
			socket, HX_OS_WAIT_READ, deadline, error);
		if (status != HX_OS_OK)
			break;
	}
	if (status != HX_OS_OK)
		return status;

	status = hx_os_address_format(&sender, host, sizeof(host), error);
	if (status == HX_OS_OK) {
		printf("Received from %s port %u: ", host,
			(unsigned int)sender.port);
		(void)fwrite(message, 1, received, stdout);
		putchar('\n');
	}

	return status;
}

int main(int argc, char **argv)
{
	hx_os_network *network = NULL;
	hx_os_socket *socket = NULL;
	hx_os_error error = {0};
	hx_os_address group, local;
	uint32_t index;
	uint16_t port;
	hx_os_status status;
	int listening, joined = 0, ok;

	if ((argc != 4 && argc != 5) ||
		(strcmp(argv[1], "listen") && strcmp(argv[1], "send")) ||
		!parse_index(argv[3], &index)) {
		fputs("Usage: multicast_discovery listen|send GROUP INDEX "
		      "[PORT]\n", stderr);
		return 2;
	}

	listening = !strcmp(argv[1], "listen");
	port = argc == 5 ? net_demo_port(argv[4]) : 39021;
	status = hx_os_address_parse(argv[2], port, &group, &error);
	if (!net_demo_check(status, &error, "parse group"))
		return 2;

	if (!port || (group.family == HX_OS_IPV4
			     ? (group.bytes[0] & 0xf0) != 0xe0
			     : group.bytes[0] != 0xff)) {
		fputs("Use a multicast address and a nonzero port.\n", stderr);
		return 2;
	}
	if (group.family == HX_OS_IPV6)
		group.scope_id = index;

	status = hx_os_network_create(NULL, &network, &error);
	if (status == HX_OS_OK)
		status = hx_os_socket_create(
			network, group.family, HX_OS_UDP, &socket, &error);
	if (status == HX_OS_OK)
		status = hx_os_socket_set_multicast_interface(
			socket, index, &error);
	if (status == HX_OS_OK)
		status = hx_os_socket_set_multicast_hops(socket, 1, &error);
	// Windows configures receiving loopback; POSIX configures sending.
	if (status == HX_OS_OK)
		status = hx_os_socket_set_multicast_loopback(socket, 1, &error);

	if (status == HX_OS_OK && listening) {
		status = hx_os_address_parse(
			group.family == HX_OS_IPV4 ? "0.0.0.0" : "::",
			port, &local, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_bind(socket, &local, &error);
		if (status == HX_OS_OK)
			status = hx_os_socket_join_multicast(
				socket, &group, index, &error);
		joined = status == HX_OS_OK;
	}
	if (status == HX_OS_OK)
		status = listening ? receive_discovery(socket, &error)
				   : send_discovery(socket, &group, &error);
	ok = net_demo_check(status, &error, "multicast discovery");

	if (joined &&
		!net_demo_check(hx_os_socket_leave_multicast(
				       socket, &group, index, &error),
			&error, "leave multicast group"))
		ok = 0;
	if (!net_demo_close(&socket, &network))
		ok = 0;

	return ok ? 0 : 1;
}
