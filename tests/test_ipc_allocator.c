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

enum { TEST_QUERY = 1, TEST_FAIL, TEST_PAUSE, TEST_PAUSE_FAIL };

typedef struct fixture {
	hx_os_atomic owner;
	hx_os_atomic live;
	hx_os_atomic queries;
	hx_os_context *context;
	hx_ipc *endpoint;
	hx_ipc *client;
	hx_os_event *entered;
	hx_os_event *release;
	hx_ipc_message *message;
	hx_os_error error;
	hx_os_status status;
	int action;
} fixture;

static void query_statistics(fixture *state)
{
	hx_ipc_statistics statistics;
	hx_os_status status = hx_ipc_statistics_read(
		state->endpoint, &statistics, sizeof(statistics), NULL);

	CHECK(status == HX_OS_OK || status == HX_OS_CLOSED);
	(void)hx_os_atomic_fetch_add(&state->queries, 1, HX_OS_RELAXED);
}

static int on_receiver(fixture *state)
{
	return (uint64_t)hx_os_atomic_load(&state->owner, HX_OS_ACQUIRE) ==
		hx_os_thread_id();
}

static void *allocate(size_t bytes, void *user)
{
	fixture *state = user;
	void *memory;

	if (on_receiver(state)) {
		int action = state->action;
		state->action = 0;
		query_statistics(state);
		if (action == TEST_FAIL)
			return NULL;

		if (action == TEST_PAUSE || action == TEST_PAUSE_FAIL) {
			hx_os_event_set(state->entered);
			CHECK(hx_os_event_wait_until(state->release,
				      hx_os_deadline_after_ms(5000),
				      NULL) == HX_OS_OK);
		}
		if (action == TEST_PAUSE_FAIL)
			return NULL;
	}

	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(&state->live, 1, HX_OS_RELAXED);

	return memory;
}

static void deallocate(void *memory, void *user)
{
	fixture *state = user;

	if (on_receiver(state))
		query_statistics(state);

	CHECK(hx_os_atomic_fetch_sub(&state->live, 1, HX_OS_RELAXED) > 0);
	free(memory);
}

static void connect_peer(fixture *state)
{
	hx_string address = HX_STRING_INIT;

	CHECK(hx_ipc_address(state->endpoint, &address, NULL) == HX_OS_OK);
	CHECK(hx_ipc_connect_until(NULL, hx_string_c_str(&address), NULL,
		      hx_os_deadline_after_ms(5000), &state->client,
		      NULL) == HX_OS_OK);

	hx_string_destroy(&address);
}

static void setup(fixture *state)
{
	hx_os_context_options context_options;
	hx_ipc_options options;

	memset(state, 0, sizeof(*state));
	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = allocate;
	context_options.allocator.deallocate = deallocate;
	context_options.allocator.user = state;
	CHECK(hx_os_context_create(&context_options, &state->context, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state->entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state->release, NULL) ==
		HX_OS_OK);
	CHECK(hx_ipc_options_init(&options, sizeof(options)) == HX_OS_OK);
	options.max_peers = 1;
	CHECK(hx_ipc_listen_with_options(state->context, "tcp://127.0.0.1:0",
		      &options, &state->endpoint, NULL) == HX_OS_OK);

	connect_peer(state);
}

static void teardown(fixture *state)
{
	hx_os_atomic_store(&state->owner, 0, HX_OS_RELEASE);
	CHECK(hx_ipc_close_until(&state->client, hx_os_deadline_after_ms(5000),
		      NULL) == HX_OS_OK);
	CHECK(hx_ipc_close_until(&state->endpoint,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	hx_os_event_destroy(state->entered);
	hx_os_event_destroy(state->release);
	CHECK(hx_os_context_allocations(state->context) == 0);
	CHECK(hx_os_context_destroy(&state->context, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state->live, HX_OS_RELAXED) == 0);
}

static void arm(fixture *state, int action)
{
	state->action = action;
	hx_os_atomic_store(
		&state->owner, (int64_t)hx_os_thread_id(), HX_OS_RELEASE);
}

static void test_retry(void)
{
	fixture state;
	hx_ipc_message *message = NULL;
	hx_os_error error;

	setup(&state);
	arm(&state, TEST_FAIL);
	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      &error) == HX_OS_NO_MEMORY);
	CHECK(!message && error.status == HX_OS_NO_MEMORY);
	CHECK(hx_os_atomic_load(&state.queries, HX_OS_RELAXED) == 1);

	arm(&state, TEST_QUERY);
	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      &error) == HX_OS_OK);
	CHECK(message->kind == HX_IPC_CONNECTED && message->peer);
	hx_ipc_message_release(message);
	CHECK(hx_os_atomic_load(&state.queries, HX_OS_RELAXED) == 3);

	CHECK(hx_ipc_send_text(state.client, "{\"v\":1}", NULL) == HX_OS_OK);
	arm(&state, TEST_FAIL);
	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      NULL) == HX_OS_OK);
	CHECK(message->kind == HX_IPC_DATA && message->bytes == 7);
	CHECK(hx_os_atomic_load(&state.queries, HX_OS_RELAXED) == 3);
	hx_ipc_message_release(message);

	teardown(&state);
}

static void *receive_paused(void *user)
{
	fixture *state = user;

	arm(state, state->action);
	state->status = hx_ipc_receive_until(state->endpoint,
		HX_OS_DEADLINE_NOW, &state->message, &state->error);

	return NULL;
}

static void test_race(int close, int fail)
{
	fixture state;
	hx_os_thread *thread = NULL;
	hx_ipc_message *message = NULL;
	hx_ipc *saved;

	setup(&state);
	saved = state.endpoint;
	state.action = fail ? TEST_PAUSE_FAIL : TEST_PAUSE;
	CHECK(hx_os_thread_create(
		      NULL, receive_paused, &state, &thread, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	if (close) {
		CHECK(hx_ipc_close_until(&state.endpoint, HX_OS_DEADLINE_NOW,
			      NULL) == HX_OS_TIMEOUT);
		CHECK(state.endpoint == saved);
		CHECK(hx_os_context_destroy(&state.context, NULL) ==
			HX_OS_BUSY);
	}
	else {
		CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW,
			      &message, NULL) == HX_OS_OK);
		CHECK(message->kind == HX_IPC_CONNECTED);
		hx_ipc_message_release(message);
	}

	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(state.status == (close ? HX_OS_CLOSED : HX_OS_TIMEOUT));
	CHECK(state.error.status == state.status && !state.message);
	CHECK(hx_os_atomic_load(&state.queries, HX_OS_RELAXED) ==
		(fail ? 1 : 2));
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);

	teardown(&state);
}

static void test_overflow(void)
{
	fixture state;
	hx_ipc_statistics statistics;
	hx_ipc_message *message = NULL;
	size_t i;

	setup(&state);
	for (i = 0; i < 6; ++i) {
		hx_os_deadline deadline = hx_os_deadline_after_ms(5000);

		CHECK(hx_ipc_close_until(&state.client, deadline, NULL) ==
			HX_OS_OK);
		do {
			CHECK(hx_ipc_statistics_read(state.endpoint,
				      &statistics, sizeof(statistics),
				      NULL) == HX_OS_OK);
			if (!statistics.peers)
				break;

			CHECK(!hx_os_deadline_expired(deadline));
			CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(1),
				      NULL) == HX_OS_OK);
		} while (1);

		if (i != 5)
			connect_peer(&state);
	}

	CHECK(statistics.lost_events >= 2);
	arm(&state, TEST_FAIL);
	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      NULL) == HX_OS_NO_MEMORY);
	CHECK(!message);
	arm(&state, TEST_QUERY);
	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      NULL) == HX_OS_OK);
	CHECK(message->kind == HX_IPC_OVERFLOW &&
		message->error.status == HX_OS_LIMIT);
	hx_ipc_message_release(message);

	for (i = 0; i < 10; ++i) {
		CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW,
			      &message, NULL) == HX_OS_OK);
		CHECK(message->kind ==
			(i % 2 ? HX_IPC_DISCONNECTED : HX_IPC_CONNECTED));
		hx_ipc_message_release(message);
	}

	CHECK(hx_ipc_receive_until(state.endpoint, HX_OS_DEADLINE_NOW, &message,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(!message);

	teardown(&state);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_IPC))
		return 0;

	test_retry();
	test_race(0, 0);
	test_race(1, 0);
	test_race(1, 1);
	test_overflow();

	puts("IPC allocator reentry, receive races and retry passed");

	return 0;
}
