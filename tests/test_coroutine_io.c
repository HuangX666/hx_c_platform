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

typedef struct test_io {
	hx_os_coro_runtime *runtime;
	hx_os_coro_stream *listener;
	hx_os_coro_stream *reader;
	hx_os_coro_stream *writer;
	hx_os_event *entered;
	hx_os_atomic probe;
	hx_os_atomic probes;
	uint64_t main_thread;
} test_io;

static void *test_allocate(size_t bytes, void *user)
{
	test_io *state = user;

	if (hx_os_atomic_load(&state->probe, HX_OS_ACQUIRE) &&
		!hx_os_coro_current() &&
		hx_os_thread_id() != state->main_thread) {
		CHECK(hx_os_coro_runtime_close_until(state->runtime,
			      HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);

		(void)hx_os_atomic_fetch_add(&state->probes, 1, HX_OS_RELAXED);
	}

	return malloc(bytes);
}

static void test_free(void *memory, void *user)
{
	(void)user;

	free(memory);
}

static hx_os_deadline test_deadline(void)
{
	return hx_os_deadline_after_ms(5000);
}

static hx_os_status test_echo(hx_os_coro *self, void *user, hx_os_error *error)
{
	test_io *state = user;
	hx_os_coro_stream *peer;
	char bytes[31];
	size_t count, sent, total = 0;

	(void)self;
	CHECK(hx_os_coro_accept(state->listener, test_deadline(), &peer, NULL,
		      error) == HX_OS_OK);

	while (total < 300) {
		CHECK(hx_os_coro_read(peer, bytes, sizeof(bytes),
			      test_deadline(), &count, error) == HX_OS_OK &&
			count);
		CHECK(hx_os_coro_write(peer, bytes, count, test_deadline(),
			      &sent, error) == HX_OS_OK &&
			sent == count);

		total += count;
	}

	hx_os_coro_stream_release(peer);

	return HX_OS_OK;
}

static hx_os_status test_pipe_writer(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_io *state = user;
	size_t sent;

	(void)self;
	CHECK(hx_os_coro_sleep_ms(2, error) == HX_OS_OK);
	CHECK(hx_os_coro_write(state->writer, "pipe", 4, test_deadline(), &sent,
		      error) == HX_OS_OK &&
		sent == 4);

	hx_os_coro_stream_release(state->writer);
	state->writer = NULL;

	return HX_OS_OK;
}

static hx_os_status test_client(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_io *state = user;
	hx_os_address address;
	hx_os_address_list *addresses;
	hx_os_coro_stream *peer;
	hx_os_coro *child;
	hx_os_error work;
	char buffer[300], output[300];
	size_t count, total;

	(void)self;
	/* No resolver workers exist yet; off-worker allocation is on I/O. */
	if (hx_os_capabilities() & HX_OS_CAP_PIPE) {
		hx_os_atomic_store(&state->probe, 1, HX_OS_RELEASE);
		CHECK(hx_os_coro_pipe_pair(&state->reader, &state->writer,
			      error) == HX_OS_OK);

		hx_os_atomic_store(&state->probe, 0, HX_OS_RELEASE);

		hx_os_coro_stream_release(state->reader);
		hx_os_coro_stream_release(state->writer);
		state->reader = state->writer = NULL;
	}

	CHECK(hx_os_coro_resolve("localhost", 80, test_deadline(), &addresses,
		      error) == HX_OS_OK);
	CHECK(hx_os_address_list_count(addresses) > 0);

	hx_os_address_list_destroy(addresses);
	CHECK(hx_os_address_parse("127.0.0.1", 0, &address, error) == HX_OS_OK);
	CHECK(hx_os_coro_listen(&address, 8, 0, &state->listener, &address,
		      error) == HX_OS_OK);
	CHECK(address.port != 0);
	CHECK(hx_os_coro_go(state->runtime, HX_OS_CORO_STACKFUL, test_echo,
		      state, NULL, &child, error) == HX_OS_OK);
	CHECK(hx_os_coro_dial(&address, test_deadline(), &peer, error) ==
		HX_OS_OK);

	memset(buffer, 'x', sizeof(buffer));
	CHECK(hx_os_coro_write(peer, buffer, sizeof(buffer), test_deadline(),
		      &count, error) == HX_OS_OK &&
		count == sizeof(buffer));

	total = 0;

	while (total < sizeof(output)) {
		CHECK(hx_os_coro_read(peer, output + total,
			      sizeof(output) - total, test_deadline(), &count,
			      error) == HX_OS_OK &&
			count);

		total += count;
	}

	CHECK(memcmp(buffer, output, sizeof(buffer)) == 0);
	CHECK(hx_os_coro_wait_until(child, test_deadline(), &work, error) ==
			HX_OS_OK &&
		work.status == HX_OS_OK);

	hx_os_coro_release(child);
	CHECK(hx_os_coro_read(peer, output, 1, test_deadline(), &count,
		      error) == HX_OS_EOF &&
		count == 0);

	hx_os_coro_stream_release(peer);
	hx_os_coro_stream_release(state->listener);
	state->listener = NULL;
	if (!(hx_os_capabilities() & HX_OS_CAP_PIPE)) {
		hx_os_event_set(state->entered);

		return hx_os_coro_sleep_until(HX_OS_DEADLINE_INFINITE, error);
	}

	CHECK(hx_os_coro_pipe_pair(&state->reader, &state->writer, error) ==
		HX_OS_OK);
	CHECK(hx_os_coro_read(state->reader, buffer, sizeof(buffer),
		      hx_os_deadline_after_ms(2), &count,
		      error) == HX_OS_TIMEOUT &&
		count == 0);
	CHECK(hx_os_coro_go(state->runtime, HX_OS_CORO_STACKFUL,
		      test_pipe_writer, state, NULL, &child,
		      error) == HX_OS_OK);
	CHECK(hx_os_coro_read(state->reader, buffer, sizeof(buffer),
		      test_deadline(), &count, error) == HX_OS_OK &&
		count == 4);
	CHECK(memcmp(buffer, "pipe", 4) == 0);
	CHECK(hx_os_coro_wait_until(child, test_deadline(), &work, error) ==
			HX_OS_OK &&
		work.status == HX_OS_OK);

	hx_os_coro_release(child);
	CHECK(hx_os_coro_read(state->reader, buffer, sizeof(buffer),
		      test_deadline(), &count, error) == HX_OS_EOF);

	hx_os_coro_stream_release(state->reader);
	/* Keep native resources open across runtime shutdown; handles may
	 * outlive it. */
	CHECK(hx_os_coro_pipe_pair(&state->reader, &state->writer, error) ==
		HX_OS_OK);

	hx_os_event_set(state->entered);

	return hx_os_coro_read(state->reader, buffer, sizeof(buffer),
		HX_OS_DEADLINE_INFINITE, &count, error);
}

int main(void)
{
	hx_os_context *context;
	hx_os_context_options context_options;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	test_io state;
	hx_os_coro *task;
	hx_os_error work;
	hx_os_deadline deadline;

	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINE_IO) ||
		!(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES))
		return 0;
	memset(&state, 0, sizeof(state));
	state.main_thread = hx_os_thread_id();
	options.workers = 1;
	hx_os_context_options_init(&context_options);
	context_options.allocator =
		(hx_os_allocator){test_allocate, test_free, &state};
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_create(
		      context, &options, &state.runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKFUL, test_client,
		      &state, NULL, &task, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered, test_deadline(), NULL) ==
		HX_OS_OK);

	hx_os_coro_cancel(task);
	CHECK(hx_os_coro_wait_until(task, test_deadline(), &work, NULL) ==
			HX_OS_OK &&
		work.status == HX_OS_CANCELLED);

	hx_os_coro_release(task);
	deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(hx_os_coro_runtime_close_until(state.runtime, deadline, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_destroy(&state.runtime, NULL) == HX_OS_OK);

	hx_os_coro_stream_release(state.reader);
	hx_os_coro_stream_release(state.writer);
	hx_os_event_destroy(state.entered);
	if (hx_os_capabilities() & HX_OS_CAP_PIPE)
		CHECK(hx_os_atomic_load(&state.probes, HX_OS_RELAXED) > 0);

	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	puts("coroutine TCP, DNS, pipes and cancellation passed");

	return 0;
}
