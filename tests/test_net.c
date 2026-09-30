#include "hx_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at line %d: %s\n",       \
				__LINE__, #value);                             \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct result_data {
	hx_os_loop *loop;
	hx_os_completion result;
	size_t calls;
} result_data;

static void completed(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	result_data *data = user;
	++data->calls;
	data->result = *completion;
	CHECK(hx_os_request_wait_until(request, HX_OS_DEADLINE_NOW, NULL,
		      NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_loop_poll_until(data->loop, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_DEADLOCK);
}

static hx_os_request *submit(
	hx_os_loop *loop, hx_os_operation *op, result_data *result)
{
	hx_os_request *request;
	hx_os_error error;
	hx_os_status status;
	result->loop = loop;
	result->calls = 0;
	status = hx_os_loop_submit(
		loop, op, completed, result, &request, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "submit failed: %s native=%lld\n",
			hx_os_status_name(status),
			(long long)error.native_code);
	CHECK(status == HX_OS_OK);
	return request;
}

static void test_tcp(hx_os_network *network, hx_os_loop *loop)
{
	hx_os_socket *listener;
	hx_os_socket *client;
	hx_os_socket *server;
	hx_os_address address;
	hx_os_operation op;
	hx_os_request *accept_request;
	hx_os_request *connect_request;
	hx_os_request *read_request;
	hx_os_request *write_request;
	result_data result[4];
	char send_data[] = "hello \xe4\xb8\xad\xe6\x96\x87";
	char buffer[64];
	CHECK(hx_os_address_parse("127.0.0.1", 0, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP, &listener,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_socket_bind(listener, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_listen(listener, 8, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_local_address(listener, &address, NULL) == HX_OS_OK);
	CHECK(address.port != 0);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP, &client,
		      NULL) == HX_OS_OK);
	hx_os_operation_init(&op, HX_OS_OP_ACCEPT);
	op.socket = listener;
	op.deadline = hx_os_deadline_after_ms(2000);
	accept_request = submit(loop, &op, &result[0]);
	hx_os_operation_init(&op, HX_OS_OP_CONNECT);
	op.socket = client;
	op.address = address;
	op.deadline = hx_os_deadline_after_ms(2000);
	connect_request = submit(loop, &op, &result[1]);
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_BUSY);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(3000), NULL) ==
		HX_OS_OK);
	CHECK(result[0].calls == 1 && result[1].calls == 1);
	CHECK(result[0].result.error.status == HX_OS_OK);
	CHECK(result[1].result.error.status == HX_OS_OK);
	server = hx_os_request_take_socket(accept_request);
	CHECK(server != NULL);
	CHECK(hx_os_request_take_socket(accept_request) == NULL);
	hx_os_request_release(accept_request);
	hx_os_request_release(connect_request);
	hx_os_operation_init(&op, HX_OS_OP_WRITE);
	op.socket = client;
	op.data = send_data;
	op.bytes = sizeof(send_data);
	write_request = submit(loop, &op, &result[0]);
	memset(send_data, 'x', sizeof(send_data));
	hx_os_operation_init(&op, HX_OS_OP_READ);
	op.socket = server;
	op.data = buffer;
	op.bytes = sizeof(buffer);
	op.deadline = hx_os_deadline_after_ms(2000);
	read_request = submit(loop, &op, &result[1]);
	CHECK(hx_os_socket_close(&server, NULL) == HX_OS_BUSY);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(3000), NULL) ==
		HX_OS_OK);
	CHECK(result[0].result.error.status == HX_OS_OK);
	CHECK(result[1].result.error.status == HX_OS_OK);
	CHECK(result[1].result.bytes == sizeof(send_data));
	CHECK(!memcmp(
		buffer, "hello \xe4\xb8\xad\xe6\x96\x87", sizeof(send_data)));
	hx_os_request_release(write_request);
	hx_os_request_release(read_request);

	/* A loop wait deadline does not cancel an infinite read. */
	hx_os_operation_init(&op, HX_OS_OP_READ);
	op.socket = server;
	op.data = buffer;
	op.bytes = sizeof(buffer);
	read_request = submit(loop, &op, &result[2]);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(5), NULL) ==
		HX_OS_TIMEOUT);
	CHECK(result[2].calls == 0);
	CHECK(hx_os_socket_close(&server, NULL) == HX_OS_BUSY);
	hx_os_request_cancel(read_request);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(1000), NULL) ==
		HX_OS_OK);
	CHECK(result[2].calls == 1);
	CHECK(result[2].result.error.status == HX_OS_CANCELLED);
	hx_os_request_release(read_request);

	op.deadline = hx_os_deadline_after_ms(5);
	read_request = submit(loop, &op, &result[3]);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(1000), NULL) ==
		HX_OS_OK);
	CHECK(result[3].calls == 1);
	CHECK(result[3].result.error.status == HX_OS_TIMEOUT);
	hx_os_request_release(read_request);
	CHECK(hx_os_socket_close(&server, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&client, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&listener, NULL) == HX_OS_OK);
}

static void test_udp(hx_os_network *network, hx_os_loop *loop)
{
	hx_os_socket *sender;
	hx_os_socket *receiver;
	hx_os_address address;
	hx_os_operation op;
	hx_os_request *requests[2];
	result_data result[2];
	char data[2];
	size_t count;
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_UDP, &sender,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_UDP, &receiver,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_address_parse("127.0.0.1", 0, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_bind(receiver, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_local_address(receiver, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_send_to(sender, &address, "abcdef", 6, &count,
		      NULL) == HX_OS_OK &&
		count == 6);
	CHECK(hx_os_socket_wait_until(receiver, HX_OS_WAIT_READ,
		      hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_socket_receive_from(receiver, NULL, data, sizeof(data),
		      &count, NULL) == HX_OS_LIMIT);
	CHECK(count == 2 && !memcmp(data, "ab", 2));

	hx_os_operation_init(&op, HX_OS_OP_RECEIVE_FROM);
	op.socket = receiver;
	op.data = data;
	op.bytes = sizeof(data);
	op.deadline = hx_os_deadline_after_ms(1000);
	requests[0] = submit(loop, &op, &result[0]);
	hx_os_operation_init(&op, HX_OS_OP_SEND_TO);
	op.socket = sender;
	op.address = address;
	op.data = NULL;
	op.bytes = 0;
	requests[1] = submit(loop, &op, &result[1]);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(result[0].result.error.status == HX_OS_OK);
	CHECK(result[0].result.bytes == 0 && result[0].calls == 1);
	CHECK(result[1].result.error.status == HX_OS_OK);
	hx_os_request_release(requests[0]);
	hx_os_request_release(requests[1]);
	hx_os_operation_init(&op, HX_OS_OP_RECEIVE_FROM);
	op.socket = receiver;
	op.data = data;
	op.bytes = sizeof(data);
	op.deadline = hx_os_deadline_after_ms(1000);
	requests[0] = submit(loop, &op, &result[0]);
	hx_os_operation_init(&op, HX_OS_OP_SEND_TO);
	op.socket = sender;
	op.address = address;
	op.data = (void *)"abcdef";
	op.bytes = 6;
	requests[1] = submit(loop, &op, &result[1]);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(result[0].result.error.status == HX_OS_LIMIT);
	CHECK(result[0].result.bytes == 2 && !memcmp(data, "ab", 2));
	CHECK(result[1].result.error.status == HX_OS_OK);
	hx_os_request_release(requests[0]);
	hx_os_request_release(requests[1]);
	CHECK(hx_os_socket_close(&receiver, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&sender, NULL) == HX_OS_OK);
}

typedef struct post_data {
	hx_os_loop *loop;
	hx_os_event *submitted;
	result_data result;
} post_data;

static void *post_worker(void *user)
{
	post_data *data = user;
	hx_os_operation op;
	hx_os_request *request;
	hx_os_operation_init(&op, HX_OS_OP_POST);
	request = submit(data->loop, &op, &data->result);
	CHECK(hx_os_request_wait_until(request, HX_OS_DEADLINE_NOW, NULL,
		      NULL) == HX_OS_TIMEOUT);
	hx_os_event_set(data->submitted);
	CHECK(hx_os_request_wait_until(request, hx_os_deadline_after_ms(1000),
		      NULL, NULL) == HX_OS_OK);
	hx_os_request_release(request);
	return NULL;
}

int main(void)
{
	hx_os_context *context;
	hx_os_network *network;
	hx_os_loop *loop;
	hx_os_operation op;
	hx_os_request *request;
	hx_os_address_list *addresses;
	hx_os_thread *thread;
	result_data result;
	post_data post;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_network_create(context, &network, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_create(network, NULL, &loop, NULL) == HX_OS_OK);
	test_tcp(network, loop);
	test_udp(network, loop);
	post.loop = loop;
	CHECK(hx_os_event_create(context, 1, 0, &post.submitted, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(context, post_worker, &post, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(post.submitted,
		      hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(1000), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(1000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	hx_os_event_destroy(post.submitted);
	CHECK(post.result.calls == 1);

	if (hx_os_capabilities() & HX_OS_CAP_THREAD_POOL) {
		hx_os_operation_init(&op, HX_OS_OP_RESOLVE);
		op.host = "localhost";
		op.address.port = 80;
		request = submit(loop, &op, &result);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(3000),
			      NULL) == HX_OS_OK);
		CHECK(result.result.error.status == HX_OS_OK);
		addresses = hx_os_request_take_addresses(request);
		CHECK(hx_os_address_list_count(addresses) > 0);
		hx_os_address_list_destroy(addresses);
		hx_os_request_release(request);
	}

	hx_os_operation_init(&op, HX_OS_OP_TIMER);
	op.deadline = hx_os_deadline_after_ms(5);
	request = submit(loop, &op, &result);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(1000), NULL) ==
		HX_OS_OK);
	CHECK(result.calls == 1 && result.result.error.status == HX_OS_OK);
	CHECK(hx_os_request_wait_until(
		      request, HX_OS_DEADLINE_NOW, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_BUSY);
	CHECK(hx_os_loop_close_until(
		      loop, hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
	/* Completed request handles may outlive their loop. */
	hx_os_request_cancel(request);
	CHECK(hx_os_request_wait_until(
		      request, HX_OS_DEADLINE_NOW, NULL, NULL) == HX_OS_OK);
	hx_os_request_release(request);
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("network and loop contracts passed");
	return 0;
}
