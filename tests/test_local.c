#include <hx_os.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
typedef struct io_result {
	hx_os_completion value;
	int called;
} io_result;

static void receive_exact(
	hx_os_socket *value, const char *expected, size_t bytes)
{
	char buffer[32];
	size_t total = 0, got;

	hx_os_deadline deadline = hx_os_deadline_after_ms(2000);
	CHECK(bytes <= sizeof(buffer));

	while (total < bytes) {
		CHECK(hx_os_socket_receive_until(value, buffer + total,
			      bytes - total, deadline, &got,
			      NULL) == HX_OS_OK &&
			got);

		total += got;
	}

	CHECK(!memcmp(buffer, expected, bytes));
}

static void HX_OS_CALL completed(
	hx_os_request *request, const hx_os_completion *value, void *user)
{
	io_result *result = user;

	(void)request;
	result->value = *value;
	++result->called;
}

static void test_loop(hx_os_network *network, hx_os_socket *listener,
	hx_os_socket *client, hx_os_socket **server)
{
	hx_os_loop *loop = NULL;
	hx_os_request *request = NULL;
	hx_os_operation operation;
	io_result result;
	char buffer[32];
	size_t sent, total = 0;

	CHECK(hx_os_loop_create(network, NULL, &loop, NULL) == HX_OS_OK);

	hx_os_operation_init(&operation, HX_OS_OP_ACCEPT);
	operation.socket = listener;
	CHECK(hx_os_loop_submit(loop, &operation, completed, &result, &request,
		      NULL) == HX_OS_NOT_SUPPORTED &&
		!request);

	hx_os_operation_init(&operation, HX_OS_OP_READ);
	operation.socket = *server;
	operation.data = buffer;
	operation.bytes = sizeof(buffer);
	operation.deadline = hx_os_deadline_after_ms(2000);
	memset(&result, 0, sizeof(result));
	CHECK(hx_os_loop_submit(loop, &operation, completed, &result, &request,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_socket_send_until(client, "loop", 4,
		      hx_os_deadline_after_ms(2000), &sent, NULL) == HX_OS_OK &&
		sent == 4);

	for (;;) {
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);
		CHECK(result.called == 1 &&
			result.value.error.status == HX_OS_OK &&
			result.value.bytes && result.value.bytes <= 4 - total);

		total += result.value.bytes;

		hx_os_request_release(request);
		if (total == 4)
			break;

		memset(&result, 0, sizeof(result));
		operation.data = buffer + total;
		operation.bytes = 4 - total;
		CHECK(hx_os_loop_submit(loop, &operation, completed, &result,
			      &request, NULL) == HX_OS_OK);
	}

	CHECK(!memcmp(buffer, "loop", 4));

	hx_os_operation_init(&operation, HX_OS_OP_WRITE);
	operation.socket = *server;
	operation.data = (void *)"async";
	operation.bytes = 5;
	operation.deadline = hx_os_deadline_after_ms(2000);
	memset(&result, 0, sizeof(result));
	CHECK(hx_os_loop_submit(loop, &operation, completed, &result, &request,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(result.called == 1 && result.value.error.status == HX_OS_OK &&
		result.value.bytes == 5);

	hx_os_request_release(request);
	receive_exact(client, "async", 5);
	hx_os_operation_init(&operation, HX_OS_OP_READ);
	operation.socket = *server;
	operation.data = buffer;
	operation.bytes = sizeof(buffer);
	operation.deadline = hx_os_deadline_after_ms(2000);
	request = NULL;
	memset(&result, 0, sizeof(result));
	CHECK(hx_os_loop_submit(loop, &operation, completed, &result, &request,
		      NULL) == HX_OS_OK);

	(void)hx_os_loop_poll_until(loop, HX_OS_DEADLINE_NOW, NULL);
	hx_os_request_cancel(request);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(result.called == 1 &&
		result.value.error.status == HX_OS_CANCELLED);

	hx_os_request_release(request);
	CHECK(hx_os_socket_close(server, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_close_until(
		      loop, hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
}
#if defined(__linux__)
static void test_backlog(hx_os_network *network, const char *path)
{
	hx_os_socket *listener = NULL, *clients[8] = {NULL}, *accepted = NULL;
	hx_os_error error;
	size_t count;
	hx_os_status status = HX_OS_OK;

	CHECK(hx_os_local_listen(network, path, 1, &listener, NULL) ==
		HX_OS_OK);

	for (count = 0; count < 8; ++count) {
		status = hx_os_local_connect(network, path, HX_OS_DEADLINE_NOW,
			&clients[count], &error);
		if (status == HX_OS_TIMEOUT)
			break;

		CHECK(status == HX_OS_OK);
	}

	CHECK(count < 8 && status == HX_OS_TIMEOUT && clients[count]);
	CHECK(error.domain == HX_OS_ERROR_ERRNO && error.native_code == EAGAIN);
	CHECK(hx_os_local_socket_accept_until(listener,
		      hx_os_deadline_after_ms(2000), &accepted,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_local_socket_finish_connect(clients[count],
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&accepted, NULL) == HX_OS_OK);

	for (count = 0; count < 8; ++count)
		CHECK(hx_os_socket_close(&clients[count], NULL) == HX_OS_OK);

	CHECK(hx_os_socket_close(&listener, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
}
#endif
int main(void)
{
	hx_os_network *network = NULL;
	hx_os_socket *listener = NULL, *client = NULL, *server = NULL,
		     *duplicate = NULL;
	hx_os_local_address address, peer, canary;
	hx_os_error error;
	hx_os_status status;
	char path[108], too_long[128], buffer[32];
	size_t sent, received;
#if defined(_WIN32)
	const char *prefix = "";
#else
	const char *prefix = "/tmp/";
#endif
	memset(&canary, 0xa5, sizeof(canary));
	address = canary;
	memset(too_long, 'x', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = 0;
	CHECK(hx_os_local_address_init(&address, too_long, NULL) ==
		HX_OS_RANGE);
	CHECK(!memcmp(&address, &canary, sizeof(address)));
	CHECK(hx_os_local_address_init(&address, "\xff", NULL) ==
		HX_OS_INVALID_ENCODING);
	if (!(hx_os_capabilities() & HX_OS_CAP_LOCAL_SOCKETS)) {
		CHECK(hx_os_local_socket_create(NULL, &listener, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!listener);

		return 0;
	}

	CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);

	status = hx_os_socket_create(
		network, HX_OS_LOCAL, HX_OS_LOCAL_STREAM, &duplicate, &error);
	if (status == HX_OS_NOT_SUPPORTED) {
		CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);

		puts("SKIP: native AF_UNIX provider unavailable");

		return 77;
	}

	CHECK(status == HX_OS_OK);
	CHECK(hx_os_socket_close(&duplicate, NULL) == HX_OS_OK);

	(void)snprintf(path, sizeof(path), "%shx-local-%s-%llx.sock", prefix,
		"中文😀", (unsigned long long)hx_os_monotonic_ns());
	CHECK(hx_os_local_listen(network, path, 2, &listener, &error) ==
		HX_OS_OK);
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_BUSY);
	CHECK(hx_os_local_listen(network, path, 2, &duplicate, NULL) !=
			HX_OS_OK &&
		!duplicate);
	CHECK(hx_os_local_socket_accept_until(listener, HX_OS_DEADLINE_NOW,
		      &server, NULL) == HX_OS_TIMEOUT &&
		!server);

	status = hx_os_local_connect(
		network, path, HX_OS_DEADLINE_NOW, &client, NULL);
	CHECK((status == HX_OS_OK || status == HX_OS_TIMEOUT) && client);
	CHECK(hx_os_local_socket_finish_connect(
		      client, hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_local_socket_accept_until(listener,
		      hx_os_deadline_after_ms(2000), &server,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_local_socket_address(client, 1, &peer, NULL) == HX_OS_OK);
	CHECK(strstr(peer.path, "中文😀") != NULL);
	CHECK(hx_os_local_socket_address(server, 1, &peer, NULL) == HX_OS_OK &&
		!peer.path[0]);
	CHECK(hx_os_socket_receive_until(server, buffer, sizeof(buffer),
		      HX_OS_DEADLINE_NOW, &received, NULL) == HX_OS_TIMEOUT &&
		!received);
	CHECK(hx_os_socket_send_until(client, "request", 7,
		      hx_os_deadline_after_ms(2000), &sent, NULL) == HX_OS_OK &&
		sent == 7);

	receive_exact(server, "request", 7);
	CHECK(hx_os_socket_send_until(server, "reply", 5,
		      hx_os_deadline_after_ms(2000), &sent, NULL) == HX_OS_OK);

	receive_exact(client, "reply", 5);
	if (hx_os_capabilities() & HX_OS_CAP_LOOP)
		test_loop(network, listener, client, &server);
	else {
		CHECK(hx_os_socket_shutdown(client, HX_OS_SHUT_WRITE, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_socket_receive_until(server, buffer, sizeof(buffer),
			      hx_os_deadline_after_ms(2000), &received,
			      NULL) == HX_OS_EOF);
		CHECK(hx_os_socket_close(&server, NULL) == HX_OS_OK);
	}

	CHECK(hx_os_socket_close(&client, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&listener, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
#if defined(__linux__)
	test_backlog(network, path);
#endif
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);

	return 0;
}
