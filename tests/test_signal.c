#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct signal_state {
	hx_os_signal_handler *handler;
	hx_os_signal_connection first, second, third;
	hx_os_event *entered, *release;
	hx_os_atomic calls, cleanups;
	int phase, trace;
} signal_state;

static hx_os_status HX_OS_CALL count_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	(void)data;
	(void)hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED);

	return HX_OS_OK;
}

static void HX_OS_CALL count_cleanup(void *user)
{
	signal_state *state = user;

	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELAXED);
}

static hx_os_status HX_OS_CALL third_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	(void)data;
	state->trace = state->trace * 10 + 3;

	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL first_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;
	int64_t value;

	CHECK(hx_os_calldata_get_i64(data, "number", &value, NULL) ==
			HX_OS_OK &&
		value == 42);

	state->trace = state->trace * 10 + 1;
	CHECK(hx_os_signal_disconnect_until(state->handler, state->second,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	if (!state->phase++) {
		CHECK(hx_os_signal_connect(state->handler, "event", third_call,
			      state, NULL, &state->third, NULL) == HX_OS_OK);
	}

	error->status = HX_OS_IO_ERROR;
	error->domain = HX_OS_ERROR_ERRNO;
	error->native_code = EIO;
	error->cause_code = 7;

	return HX_OS_IO_ERROR;
}

static hx_os_status HX_OS_CALL self_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;
	hx_os_signal_handler *same = state->handler;

	(void)data;
	CHECK(hx_os_signal_disconnect_until(state->handler, state->first,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_signal_handler_close_until(
		      &same, HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_DEADLOCK &&
		same == state->handler);

	return HX_OS_OK;
}

static void HX_OS_CALL self_cleanup(void *user)
{
	signal_state *state = user;

	CHECK(hx_os_signal_disconnect_until(state->handler, state->first,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_DEADLOCK);

	count_cleanup(user);
}

static hx_os_status HX_OS_CALL recursive_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	(void)data;
	(void)hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED);

	return hx_os_signal_emit(state->handler, "recursive", NULL, NULL, NULL);
}

static hx_os_status HX_OS_CALL blocking_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	(void)data;
	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	return count_call(data, user, error);
}

static hx_os_status HX_OS_CALL disconnect_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	(void)data;
	CHECK(hx_os_signal_disconnect(state->handler, state->first, NULL) ==
		HX_OS_OK);

	return HX_OS_OK;
}

static void HX_OS_CALL blocking_cleanup(void *user)
{
	signal_state *state = user;

	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	count_cleanup(user);
}

static void *emit_thread(void *user)
{
	signal_state *state = user;

	CHECK(hx_os_signal_emit(state->handler, "blocked", NULL, NULL, NULL) ==
		HX_OS_OK);

	return NULL;
}

static hx_os_status HX_OS_CALL ancestor_wait(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	signal_state *state = user;

	(void)data;
	(void)error;
	CHECK(hx_os_signal_disconnect_until(state->handler, state->first,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_DEADLOCK);

	return HX_OS_OK;
}

static hx_os_status HX_OS_CALL ancestor_emit(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	signal_state *state = user;

	return hx_os_signal_emit(state->handler, "inner", data, NULL, error);
}

static void ancestor_test(void)
{
	signal_state state;
	size_t called;

	memset(&state, 0, sizeof(state));
	CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "outer", ancestor_emit,
		      &state, count_cleanup, &state.first, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "inner", ancestor_wait,
		      &state, NULL, &state.second, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(state.handler, "outer", NULL, &called, NULL) ==
			HX_OS_OK &&
		called == 1);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);
}

static void cleanup_wait_test(void)
{
	signal_state state;
	hx_os_thread *thread = NULL;

	memset(&state, 0, sizeof(state));
	CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "blocked", disconnect_call,
		      &state, blocking_cleanup, &state.first,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, emit_thread, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_signal_handler_close_until(&state.handler,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		state.handler);

	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);

	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}

static hx_os_status HX_OS_CALL concurrent_call(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)error;

	signal_state *state = user;

	int64_t count = hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_ACQ_REL);
	(void)data;
	if (count == 3)
		hx_os_event_set(state->entered);

	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);

	return HX_OS_OK;
}

static void *race_emit(void *user)
{
	signal_state *state = user;
	int i;

	for (i = 0; i < 1000; ++i)
		CHECK(hx_os_signal_emit(state->handler, "race", NULL, NULL,
			      NULL) == HX_OS_OK);

	return NULL;
}

static void concurrent_test(void)
{
	signal_state state;
	hx_os_thread *threads[4];
	int i;

	memset(&state, 0, sizeof(state));
	CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "race", concurrent_call,
		      &state, count_cleanup, &state.first, NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(NULL, race_emit, &state, &threads[i],
			      NULL) == HX_OS_OK);

	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);

	hx_os_event_set(state.release);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) == 4 &&
		hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(3000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	CHECK(hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) == 4);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);

	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}

typedef struct allocation_state {
	size_t calls, fail, live;
	signal_state *reenter;
} allocation_state;

static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *result;

	if (state->reenter) {
		signal_state *signal = state->reenter;

		state->reenter = NULL;
		CHECK(hx_os_signal_disconnect(signal->handler, signal->first,
			      NULL) == HX_OS_OK);
	}

	if (++state->calls == state->fail)
		return NULL;

	result = malloc(bytes);
	if (result)
		++state->live;

	return result;
}

static void HX_OS_CALL deallocate(void *memory, void *user)
{
	allocation_state *state = user;

	if (memory) {
		--state->live;

		free(memory);
	}
}

static void allocation_test(void)
{
	hx_os_context *context = NULL;
	hx_os_context_options options;
	allocation_state allocation = {0, 0, 0, NULL};
	signal_state state;
	size_t i, called = 99;

	memset(&state, 0, sizeof(state));
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &allocation;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	for (i = 1; i < 7; ++i) {
		hx_os_status status;

		allocation.calls = 0;
		allocation.fail = i;

		status = hx_os_signal_handler_create(
			context, &state.handler, NULL);
		allocation.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (state.handler)
			CHECK(hx_os_signal_handler_destroy(
				      &state.handler, NULL) == HX_OS_OK);

		CHECK(hx_os_context_allocations(context) == 0 &&
			allocation.live == 1);
	}

	CHECK(hx_os_signal_handler_create(context, &state.handler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "value", count_call, &state,
		      count_cleanup, &state.first, NULL) == HX_OS_OK);

	allocation.calls = 0;
	allocation.fail = 1;
	CHECK(hx_os_signal_emit(state.handler, "value", NULL, &called, NULL) ==
			HX_OS_OK &&
		called == 1 && allocation.calls == 0);
	allocation.fail = 0;
	for (i = 1; i < 16; ++i)
		CHECK(hx_os_signal_connect(state.handler, "value", count_call,
			      &state, NULL, NULL, NULL) == HX_OS_OK);

	allocation.calls = 0;
	allocation.fail = 1;
	CHECK(hx_os_signal_emit(state.handler, "value", NULL, &called, NULL) ==
			HX_OS_OK &&
		called == 16 && allocation.calls == 0);
	allocation.fail = 0;
	CHECK(hx_os_signal_connect(state.handler, "value", count_call, &state,
		      NULL, NULL, NULL) == HX_OS_OK);

	/* The heap path must still reject cleanly and permit allocator reentry.
	 */
	allocation.calls = 0;
	allocation.fail = 1;
	CHECK(hx_os_signal_emit(state.handler, "value", NULL, &called, NULL) ==
			HX_OS_NO_MEMORY &&
		called == 0);

	allocation.fail = 0;
	allocation.reenter = &state;
	CHECK(hx_os_signal_emit(state.handler, "value", NULL, &called, NULL) ==
			HX_OS_OK &&
		called == 16);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!allocation.live);
}

int main(void)
{
	signal_state state;
	hx_os_calldata data = HX_OS_CALLDATA_INIT;
	hx_os_signal_options options = HX_OS_SIGNAL_OPTIONS_INIT;
	hx_os_thread *thread = NULL;
	hx_os_signal_connection stale;
	hx_os_error error;
	size_t called;

	memset(&state, 0, sizeof(state));
	if (!(hx_os_capabilities() & HX_OS_CAP_SIGNALS)) {
		CHECK(hx_os_signal_handler_create(NULL, &state.handler, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!state.handler);

		return 0;
	}

	CHECK(hx_os_signal_options_init(&options, sizeof(options)) == HX_OS_OK);

	options.max_connections = 2;
	options.max_depth = 2;
	CHECK(hx_os_signal_handler_create_with_options(
		      NULL, &options, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "event", first_call, &state,
		      NULL, &state.first, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "event", count_call, &state,
		      count_cleanup, &state.second, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "event", count_call, &state,
		      count_cleanup, NULL, NULL) == HX_OS_LIMIT);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 0);
	CHECK(hx_os_calldata_set_i64(&data, "number", 42, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(state.handler, "event", &data, &called,
		      &error) == HX_OS_IO_ERROR &&
		called == 1 && state.trace == 1);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(error.status == HX_OS_IO_ERROR &&
		error.domain == HX_OS_ERROR_ERRNO && error.native_code == EIO &&
		error.cause_code == 7);

	state.trace = 0;
	CHECK(hx_os_signal_emit(state.handler, "event", &data, &called,
		      &error) == HX_OS_IO_ERROR &&
		called == 2 && state.trace == 13);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.third,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);

	stale = state.first;
	CHECK(hx_os_signal_connect(state.handler, "self", self_call, &state,
		      self_cleanup, &state.first, NULL) == HX_OS_OK &&
		state.first != stale);
	CHECK(hx_os_signal_disconnect(state.handler, stale, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(state.handler, "self", NULL, &called, NULL) ==
			HX_OS_OK &&
		called == 1);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 2);

	memset(&state, 0, sizeof(state));
	CHECK(hx_os_signal_handler_create_with_options(
		      NULL, &options, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "recursive", recursive_call,
		      &state, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(state.handler, "recursive", NULL, NULL, NULL) ==
			HX_OS_LIMIT &&
		hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) == 2);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);

	memset(&state, 0, sizeof(state));
	options.max_parallel_emits = 1;
	CHECK(hx_os_signal_handler_create_with_options(
		      NULL, &options, &state.handler, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_connect(state.handler, "blocked", blocking_call,
		      &state, count_cleanup, &state.first, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, emit_thread, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_signal_emit(state.handler, "blocked", NULL, NULL, NULL) ==
		HX_OS_LIMIT);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_signal_handler_close_until(&state.handler,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		state.handler);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 0);
	CHECK(hx_os_signal_emit(state.handler, "blocked", NULL, NULL, NULL) ==
		HX_OS_CLOSED);

	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_signal_disconnect_until(state.handler, state.first,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) == 1 &&
		hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_signal_handler_destroy(&state.handler, NULL) == HX_OS_OK);

	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
	cleanup_wait_test();
	ancestor_test();
	concurrent_test();
	allocation_test();

	return 0;
}
