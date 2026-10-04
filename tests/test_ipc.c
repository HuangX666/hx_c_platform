#include <hx_os.h>
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

typedef struct sender {
	hx_ipc *endpoint;
	hx_ipc_peer peer;
	uint64_t channel;
	unsigned int count;
	hx_os_status status;
} sender;

typedef struct allocator {
	hx_os_atomic calls;
	hx_os_atomic live;
	hx_os_atomic fail;
} allocator;

static void *allocate(size_t bytes, void *user)
{
	allocator *state = user;

	int64_t call = hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED);

	void *memory;

	if (call == hx_os_atomic_load(&state->fail, HX_OS_RELAXED))
		return NULL;

	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(&state->live, 1, HX_OS_RELAXED);

	return memory;
}

static void deallocate(void *memory, void *user)
{
	allocator *state = user;

	CHECK(hx_os_atomic_fetch_sub(&state->live, 1, HX_OS_RELAXED) > 0);

	free(memory);
}

static void close_endpoint(hx_ipc **endpoint)
{
	hx_os_status status =
		hx_ipc_close_until(endpoint, HX_OS_DEADLINE_NOW, NULL);

	CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);
	if (status == HX_OS_TIMEOUT) {
		CHECK(*endpoint);
		CHECK(hx_ipc_close_until(endpoint,
			      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	}

	CHECK(!*endpoint);
}

static hx_ipc_message *next_data(hx_ipc *endpoint)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(5000);

	for (;;) {
		hx_ipc_message *message = NULL;

		CHECK(hx_ipc_receive_until(
			      endpoint, deadline, &message, NULL) == HX_OS_OK);
		if (message->kind == HX_IPC_DATA ||
			message->kind == HX_IPC_REQUEST)
			return message;
		hx_ipc_message_release(message);
	}
}

static void *send_requests(void *user)
{
	sender *state = user;
	unsigned int index;

	state->status = HX_OS_OK;

	for (index = 0; index < state->count; ++index) {
		hx_ipc_message *reply = NULL;
		unsigned char data[128];

		memset(data, (int)(state->channel + index), sizeof(data));
		data[0] = (unsigned char)state->channel;
		data[1] = (unsigned char)index;
		state->status = hx_ipc_request_until(state->endpoint,
			state->peer, state->channel, data, sizeof(data),
			hx_os_deadline_after_ms(5000), &reply, NULL);
		if (state->status != HX_OS_OK)
			break;

		CHECK(reply && reply->bytes == sizeof(data));
		CHECK(reply->channel == state->channel);
		CHECK(!memcmp(reply->data, data, sizeof(data)));

		hx_ipc_message_release(reply);
	}

	return NULL;
}

static int test_transport(const char *scheme, int unreliable)
{
	hx_os_context *context = NULL;
	hx_ipc *server = NULL, *client = NULL;
	hx_string address = HX_STRING_INIT;
	hx_ipc_options options;
	hx_ipc_message *message;
	hx_ipc_peer remote;
	char name[128];
	unsigned char binary[40000];
	hx_os_error error = {0};
	hx_os_status status;
	hx_ipc_statistics statistics;
	size_t index;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_ipc_options_init(&options, sizeof(options)) == HX_OS_OK);

	options.reconnect_attempts = 0;
	options.idle_timeout_ms = 5000;
	if (!strcmp(scheme, "local"))
		(void)snprintf(name, sizeof(name), "local://test-%llx",
			(unsigned long long)hx_os_monotonic_ns());
	else
		(void)snprintf(name, sizeof(name), "%s://127.0.0.1:0", scheme);

	status = hx_ipc_listen_with_options(
		context, name, &options, &server, &error);
#if defined(__ANDROID__)
	if (!strcmp(scheme, "local") && status == HX_OS_ACCESS_DENIED) {
		CHECK(!server && hx_os_context_allocations(context) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

		puts("SKIP local: Android sandbox denied Unix socket creation");

		return 0;
	}
#endif
	if (status != HX_OS_OK)
		fprintf(stderr, "%s listen: %d native %lld\n", scheme,
			(int)status, (long long)error.native_code);

	CHECK(status == HX_OS_OK);
	CHECK(hx_ipc_address(server, &address, NULL) == HX_OS_OK);

	status = hx_ipc_connect_until(context, hx_string_c_str(&address),
		&options, hx_os_deadline_after_ms(5000), &client, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s connect %s: %d native %lld, owner %p\n",
			scheme, hx_string_c_str(&address), (int)status,
			(long long)error.native_code, (void *)client);

	CHECK(status == HX_OS_OK);

	status = hx_ipc_send_text_until(client, 0, 7, "你好，IPC 😀",
		hx_os_deadline_after_ms(5000), &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s send: %d domain %u native %lld\n", scheme,
			(int)status, error.domain,
			(long long)error.native_code);

	CHECK(status == HX_OS_OK);

	message = next_data(server);
	CHECK(message->channel == 7 && message->encoding == HX_IPC_UTF8);
	CHECK(message->bytes == strlen("你好，IPC 😀"));
	CHECK(!memcmp(message->data, "你好，IPC 😀", message->bytes));

	remote = message->peer;

	hx_ipc_message_release(message);

	for (index = 0; index < sizeof(binary); ++index)
		binary[index] = (unsigned char)index;

	CHECK(hx_ipc_send_until(server, remote, 8, binary, sizeof(binary),
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	message = next_data(client);
	CHECK(message->bytes == sizeof(binary));
	CHECK(!memcmp(message->data, binary, sizeof(binary)));

	hx_ipc_message_release(message);
	CHECK(hx_ipc_send_until(client, 0, 0, NULL, 0,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	message = next_data(server);
	CHECK(message->bytes == 0);

	hx_ipc_message_release(message);
	if (!unreliable) {
		hx_os_thread *threads[4] = {NULL};
		sender senders[4];

		for (index = 0; index < 4; ++index) {
			senders[index].endpoint = client;
			senders[index].peer = 0;
			senders[index].channel = index + 1;
			senders[index].count = 20;
			CHECK(hx_os_thread_create(context, send_requests,
				      &senders[index], &threads[index],
				      NULL) == HX_OS_OK);
		}

		for (index = 0; index < 20; ++index) {
			hx_ipc_message *batch[4];
			size_t item;

			for (item = 0; item < 4; ++item)
				batch[item] = next_data(server);

			for (item = 4; item; --item) {
				message = batch[item - 1];
				CHECK(message->kind == HX_IPC_REQUEST);
				CHECK(hx_ipc_reply_until(server, message,
					      message->data, message->bytes,
					      hx_os_deadline_after_ms(5000),
					      NULL) == HX_OS_OK);

				hx_ipc_message_release(message);
			}
		}

		for (index = 0; index < 4; ++index) {
			CHECK(hx_os_thread_join_until(threads[index],
				      hx_os_deadline_after_ms(5000), NULL,
				      NULL) == HX_OS_OK);
			CHECK(senders[index].status == HX_OS_OK);
			CHECK(hx_os_thread_destroy(&threads[index], NULL) ==
				HX_OS_OK);
		}
	}

	if (!unreliable) {
		hx_os_thread *threads[2] = {NULL};
		sender senders[2] = {{0}, {0}};

		senders[0].endpoint = client;
		senders[1].endpoint = server;
		senders[1].peer = remote;

		for (index = 0; index < 2; ++index) {
			senders[index].channel = 100 + index;
			senders[index].count = 1;
			CHECK(hx_os_thread_create(context, send_requests,
				      &senders[index], &threads[index],
				      NULL) == HX_OS_OK);
		}

		for (index = 0; index < 2; ++index) {
			hx_ipc *destination = index ? server : client;

			message = next_data(destination);
			CHECK(hx_ipc_reply_until(destination, message,
				      message->data, message->bytes,
				      hx_os_deadline_after_ms(5000),
				      NULL) == HX_OS_OK);

			hx_ipc_message_release(message);
		}

		for (index = 0; index < 2; ++index) {
			CHECK(hx_os_thread_join_until(threads[index],
				      hx_os_deadline_after_ms(5000), NULL,
				      NULL) == HX_OS_OK &&
				senders[index].status == HX_OS_OK);
			CHECK(hx_os_thread_destroy(&threads[index], NULL) ==
				HX_OS_OK);
		}
	}

	CHECK(hx_ipc_statistics_read(server, &statistics, sizeof(statistics),
		      NULL) == HX_OS_OK);
	CHECK(statistics.peers == 1);

	close_endpoint(&client);
	close_endpoint(&server);

	hx_string_destroy(&address);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	printf("%s: duplex, messages, threads and lifetime passed\n", scheme);

	return 1;
}

static void test_capacity(void)
{
	hx_ipc_options options;
	hx_ipc *server = NULL, *client = NULL;
	hx_ipc_message *message;
	hx_string address = HX_STRING_INIT;

	CHECK(hx_ipc_options_init(&options, sizeof(options)) == HX_OS_OK);

	options.max_messages = 1;
	CHECK(hx_ipc_listen_with_options(NULL, "tcp://127.0.0.1:0", &options,
		      &server, NULL) == HX_OS_OK);
	CHECK(hx_ipc_address(server, &address, NULL) == HX_OS_OK);
	CHECK(hx_ipc_connect_until(NULL, hx_string_c_str(&address), NULL,
		      hx_os_deadline_after_ms(5000), &client,
		      NULL) == HX_OS_OK);
	CHECK(hx_ipc_send_until(client, 0, 1, "one", 3,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_ipc_send_until(client, 0, 2, "two", 3,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_LIMIT);

	message = next_data(server);
	CHECK(message->channel == 1);

	hx_ipc_message_release(message);
	CHECK(hx_ipc_send_until(client, 0, 2, "two", 3,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);

	message = next_data(server);
	CHECK(message->channel == 2);

	hx_ipc_message_release(message);
	close_endpoint(&client);
	close_endpoint(&server);

	hx_string_destroy(&address);
}

static hx_ipc_message *next_event(hx_ipc *endpoint, int kind)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(5000);

	for (;;) {
		hx_ipc_message *message = NULL;

		CHECK(hx_ipc_receive_until(
			      endpoint, deadline, &message, NULL) == HX_OS_OK);
		if (message->kind == kind)
			return message;

		hx_ipc_message_release(message);
	}
}

static void test_restart(const char *scheme)
{
	hx_os_context *context = NULL;
	hx_ipc_options options;
	hx_ipc *server = NULL, *client = NULL, *other = NULL;
	hx_ipc_message *old, *message;
	hx_ipc_peer previous, current;
	hx_string address = HX_STRING_INIT;
	char name[128];
	hx_os_error error;
	hx_os_status status;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_ipc_options_init(&options, sizeof(options)) == HX_OS_OK);

	options.idle_timeout_ms = 300;
	options.handshake_timeout_ms = 500;
	options.reconnect_attempts = 50;
	options.reconnect_delay_ms = 20;
	if (!strcmp(scheme, "local"))
		(void)snprintf(name, sizeof(name), "local://restart-%llx",
			(unsigned long long)hx_os_monotonic_ns());
	else
		(void)snprintf(name, sizeof(name), "%s://127.0.0.1:0", scheme);

	CHECK(hx_ipc_listen_with_options(
		      context, name, &options, &server, NULL) == HX_OS_OK);
	CHECK(hx_ipc_address(server, &address, NULL) == HX_OS_OK);
	CHECK(hx_ipc_connect_until(context, hx_string_c_str(&address), &options,
		      hx_os_deadline_after_ms(5000), &client,
		      NULL) == HX_OS_OK);
	CHECK(hx_ipc_connect_until(context, hx_string_c_str(&address), &options,
		      hx_os_deadline_after_ms(5000), &other, NULL) == HX_OS_OK);
	CHECK(hx_ipc_send_text(client, "first peer", NULL) == HX_OS_OK);

	old = next_data(server);
	CHECK(hx_ipc_send_text(other, "second peer", NULL) == HX_OS_OK);

	message = next_data(server);
	CHECK(old->peer != message->peer);

	hx_ipc_message_release(message);
	close_endpoint(&other);
	CHECK(hx_ipc_send_text(client, "still alive", NULL) == HX_OS_OK);

	message = next_data(server);
	CHECK(old->peer == message->peer);

	hx_ipc_message_release(message);
	CHECK(hx_ipc_wait_connected_until(
		      client, HX_OS_DEADLINE_NOW, &previous, NULL) == HX_OS_OK);

	close_endpoint(&server);
	message = next_event(client, HX_IPC_DISCONNECTED);
	CHECK(message->peer == previous);

	hx_ipc_message_release(message);

	status = hx_ipc_listen_with_options(
		context, hx_string_c_str(&address), &options, &server, &error);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s restart: %d native %lld\n", scheme,
			(int)status, (long long)error.native_code);

	CHECK(status == HX_OS_OK);
	CHECK(hx_ipc_wait_connected_until(client, hx_os_deadline_after_ms(5000),
		      &current, NULL) == HX_OS_OK &&
		current != previous);

	message = next_event(client, HX_IPC_CONNECTED);
	CHECK(message->peer == current && message->error.status == HX_OS_OK);

	hx_ipc_message_release(message);
	CHECK(hx_ipc_send_text(client, "new session", NULL) == HX_OS_OK);

	message = next_data(server);
	CHECK(old->peer != message->peer);
	CHECK(hx_ipc_send_until(server, old->peer, 0, "stale", 5,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_CLOSED);

	hx_ipc_message_release(message);
	close_endpoint(&client);
	close_endpoint(&server);
	/* Retained messages have their own allocation after endpoint closure.
	 */
	CHECK(!memcmp(old->data, "first peer", old->bytes));

	hx_ipc_message_release(old);
	hx_string_destroy(&address);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	printf("%s: multiple peers, restart and stale session passed\n",
		scheme);
}

static void test_timeout_and_close(void)
{
	hx_os_context *context = NULL;
	hx_ipc *server = NULL, *client = NULL;
	hx_ipc_message *request, *reply = NULL;
	hx_string address = HX_STRING_INIT;
	hx_os_thread *thread = NULL;
	sender state;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_ipc_listen_with_options(context, "tcp://127.0.0.1:0", NULL,
		      &server, NULL) == HX_OS_OK);
	CHECK(hx_ipc_address(server, &address, NULL) == HX_OS_OK);
	CHECK(hx_ipc_connect_until(context, hx_string_c_str(&address), NULL,
		      hx_os_deadline_after_ms(5000), &client,
		      NULL) == HX_OS_OK);
	CHECK(hx_ipc_request_text_until(client, 0, 1, "late",
		      hx_os_deadline_after_ms(100), &reply,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(!reply);

	request = next_data(server);
	CHECK(hx_ipc_reply_text(server, request, "too late", NULL) == HX_OS_OK);

	hx_ipc_message_release(request);
	state.endpoint = client;
	state.peer = 0;
	state.channel = 2;
	state.count = 1;
	CHECK(hx_os_thread_create(context, send_requests, &state, &thread,
		      NULL) == HX_OS_OK);

	request = next_data(server);
	CHECK(hx_ipc_reply_until(server, request, request->data, request->bytes,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	hx_ipc_message_release(request);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK &&
		state.status == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(context, send_requests, &state, &thread,
		      NULL) == HX_OS_OK);

	request = next_data(server);
	close_endpoint(&client);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(state.status == HX_OS_CANCELLED || state.status == HX_OS_CLOSED);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);

	hx_ipc_message_release(request);
	close_endpoint(&server);

	hx_string_destroy(&address);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
}

static void test_creation_failures(void)
{
	int64_t index;
	hx_ipc *invalid = NULL;

	CHECK(hx_ipc_listen("local://\xff", &invalid, NULL) ==
			HX_OS_INVALID_ENCODING &&
		!invalid);

	for (index = 0; index < 32; ++index) {
		allocator memory = {0};
		hx_os_context_options options;
		hx_os_context *context = NULL;
		hx_ipc *endpoint = NULL;
		hx_os_status status;

		hx_os_context_options_init(&options);
		options.allocator.allocate = allocate;
		options.allocator.deallocate = deallocate;
		options.allocator.user = &memory;
		hx_os_atomic_store(&memory.fail, -1, HX_OS_RELAXED);
		CHECK(hx_os_context_create(&options, &context, NULL) ==
			HX_OS_OK);

		hx_os_atomic_store(&memory.calls, 0, HX_OS_RELAXED);
		hx_os_atomic_store(&memory.fail, index, HX_OS_RELAXED);

		status = hx_ipc_listen_with_options(
			context, "tcp://127.0.0.1:0", NULL, &endpoint, NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);

		hx_os_atomic_store(&memory.fail, -1, HX_OS_RELAXED);
		if (endpoint)
			close_endpoint(&endpoint);

		CHECK(hx_os_context_allocations(context) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(hx_os_atomic_load(&memory.live, HX_OS_RELAXED) == 0);
	}
}

int main(int argc, char **argv)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_IPC)) {
		hx_ipc *endpoint = NULL;

		CHECK(hx_ipc_listen("tcp://127.0.0.1:0", &endpoint, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!endpoint);

		return 0;
	}

	if (argc > 1) {
		return test_transport(argv[1], !strcmp(argv[1], "udp")) ? 0
									: 77;
	}

	test_transport("tcp", 0);
	test_transport("udp", 1);
	test_restart("tcp");
	test_restart("udp");
	if (hx_os_capabilities() & HX_OS_CAP_KCP) {
		test_transport("kcp", 0);
		test_restart("kcp");
	}
#if defined(_WIN32)
	if (hx_os_capabilities() & HX_OS_CAP_ASYNC_PIPE) {
#else
	if ((hx_os_capabilities() & HX_OS_CAP_LOOP_FD) &&
		(hx_os_capabilities() & HX_OS_CAP_LOCAL_SOCKETS)) {
#endif
		if (test_transport("local", 0))
			test_restart("local");
	}

	test_capacity();

	test_timeout_and_close();
	test_creation_failures();

	return 0;
}
