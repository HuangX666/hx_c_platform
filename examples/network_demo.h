#ifndef HX_EXAMPLE_NETWORK_DEMO_H
#define HX_EXAMPLE_NETWORK_DEMO_H

#include <hx_os_base.h>
#include <hx_os_net.h>
#include <hx_os_thread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only setup, diagnostics and thread lifetime are shared by socket demos. */
static inline int net_demo_check(
	hx_os_status status, const hx_os_error *error, const char *operation)
{
	char text[256] = {0};
	size_t required;
	hx_os_error detail = {0};
	if (status == HX_OS_OK)
		return 1;
	if (error)
		detail = *error;
	detail.status = status;
	(void)hx_os_error_message(&detail, text, sizeof(text), &required);
	fprintf(stderr, "%s: status=%d %s\n", operation, (int)status, text);
	return 0;
}

static inline uint16_t net_demo_port(const char *text)
{
	char *end;
	unsigned long value;
	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || end == text || *end || value > 65535) {
		fputs("Port must be in 0..65535.\n", stderr);
		exit(2);
	}
	return (uint16_t)value;
}

static inline int net_demo_open(hx_os_network **network, hx_os_socket **socket,
	int protocol, uint16_t port)
{
	hx_os_address address;
	hx_os_error error = {0};
	hx_os_status status = hx_os_network_create(NULL, network, &error);
	if (status == HX_OS_OK)
		status = hx_os_socket_create(
			*network, HX_OS_IPV4, protocol, socket, &error);
	if (status == HX_OS_OK)
		status = hx_os_address_parse(
			"127.0.0.1", port, &address, &error);
	if (status == HX_OS_OK)
		status = hx_os_socket_bind(*socket, &address, &error);
	return net_demo_check(status, &error, "open loopback socket");
}

static inline int net_demo_listening(hx_os_socket *socket)
{
	hx_os_address address;
	hx_os_error error = {0};
	if (!net_demo_check(
		    hx_os_socket_local_address(socket, &address, &error),
		    &error, "local address"))
		return 0;
	printf("Listening: %u\n", (unsigned int)address.port);
	fflush(stdout);
	return 1;
}

static inline int net_demo_same_peer(
	const hx_os_address *left, const hx_os_address *right)
{
	return left->family == right->family && left->port == right->port &&
		left->scope_id == right->scope_id &&
		!memcmp(left->bytes, right->bytes, sizeof(left->bytes));
}

static inline void net_demo_join(hx_os_thread **thread, hx_os_deadline deadline)
{
	hx_os_error error = {0};
	if (!net_demo_check(
		    hx_os_thread_join_until(*thread, deadline, NULL, &error),
		    &error, "join")) {
		/* CLI-only fatal failure. Borrowed task storage is still alive;
		 * a persistent host must retain its owner and retry instead. */
		exit(EXIT_FAILURE);
	}
	if (!net_demo_check(hx_os_thread_destroy(thread, &error), &error,
		    "destroy thread"))
		exit(EXIT_FAILURE);
}

static inline int net_demo_close(hx_os_socket **socket, hx_os_network **network)
{
	hx_os_error error = {0};
	int ok = net_demo_check(
		hx_os_socket_close(socket, &error), &error, "socket close");
	if (!net_demo_check(hx_os_network_destroy(network, &error), &error,
		    "network destroy"))
		ok = 0;
	return ok;
}

#endif
