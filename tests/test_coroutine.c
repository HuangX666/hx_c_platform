#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct test_state {
	hx_os_coro_runtime *runtime;
	hx_os_coro *self;
	hx_os_coro_channel *channel;
	hx_os_event *entered;
	hx_os_event *release;
	hx_os_atomic cleanups;
	int step;
	int value;
	int count;
	int total;
} test_state;

static hx_os_deadline test_deadline(void)
{
	return hx_os_deadline_after_ms(5000);
}

static void test_cleanup(void *user)
{
	test_state *state = user;

	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELAXED);
}

static void test_cleanup_wait(void *user)
{
	test_state *state = user;

	CHECK(hx_os_coro_wait_until(state->self, HX_OS_DEADLINE_NOW, NULL,
		      NULL) == HX_OS_DEADLOCK);

	test_cleanup(user);
}

static void test_wait(hx_os_coro *task, hx_os_status expected)
{
	hx_os_error result;

	CHECK(hx_os_coro_wait_until(task, test_deadline(), &result, NULL) ==
		HX_OS_OK);
	CHECK(result.status == expected);
	CHECK(hx_os_coro_ready(task));

	hx_os_coro_release(task);
}

static void test_close(hx_os_coro_runtime **runtime)
{
	hx_os_deadline deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(hx_os_coro_runtime_drain_until(*runtime, deadline, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_destroy(runtime, NULL) == HX_OS_OK);
}

static hx_os_status test_produce(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	int i;

	uint64_t thread = hx_os_thread_id();

	double floating = 1.25;

	CHECK(hx_os_coro_current() == self);
	CHECK(hx_os_coro_wait_until(self, HX_OS_DEADLINE_NOW, NULL, NULL) ==
		HX_OS_DEADLOCK);
	CHECK(hx_os_coro_runtime_close_until(state->runtime, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_DEADLOCK);

	for (i = 1; i <= 1000; ++i) {
		CHECK(hx_os_coro_channel_send_until(state->channel, &i,
			      test_deadline(), error) == HX_OS_OK);
		if (i % 25 == 0)
			CHECK(hx_os_coro_sleep_ms(1, error) == HX_OS_OK);

		CHECK(hx_os_coro_yield(error) == HX_OS_OK);

		floating += 0.5;
		CHECK(floating == 1.25 + 0.5 * i);
		CHECK(thread == hx_os_thread_id());
	}

	return hx_os_coro_channel_close(state->channel, error);
}

static hx_os_status test_consume(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	hx_os_status status;

	(void)self;

	for (;;) {
		status = hx_os_coro_channel_receive_until(
			state->channel, &state->value, test_deadline(), error);
		if (status == HX_OS_EOF)
			return HX_OS_OK;

		if (status != HX_OS_OK)
			return status;

		CHECK(state->value == ++state->count);

		state->total += state->value;
	}
}

static hx_os_status test_state_machine(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	hx_os_status status;

	(void)self;
	if (state->step == 0) {
		status = hx_os_coro_sleep_ms(1, error);
		if (status != HX_OS_OK)
			return status;

		state->step = 1;
	}

	status = hx_os_coro_yield(error);
	if (status != HX_OS_OK)
		return status;

	CHECK(++state->count <= 300);
	if (state->count == 300)
		return HX_OS_OK;

	state->step = 0;

	return HX_OS_WOULD_BLOCK;
}

static void test_channels(size_t workers, size_t capacity, int stackful)
{
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_container_options values;
	hx_os_coro *producer, *consumer;
	test_state send, receive;

	memset(&send, 0, sizeof(send));
	memset(&receive, 0, sizeof(receive));
	options.workers = workers;
	CHECK(hx_os_coro_runtime_create(NULL, &options, &send.runtime, NULL) ==
		HX_OS_OK);

	hx_os_container_options_init(&values, sizeof(int));
	CHECK(hx_os_coro_channel_create(send.runtime, &values, capacity,
		      &send.channel, NULL) == HX_OS_OK);

	receive.runtime = send.runtime;
	receive.channel = send.channel;
	CHECK(hx_os_coro_go(send.runtime, HX_OS_CORO_STACKLESS, test_consume,
		      &receive, test_cleanup, &consumer, NULL) == HX_OS_OK);
	if (stackful) {
		CHECK(hx_os_coro_go(send.runtime, HX_OS_CORO_STACKFUL,
			      test_produce, &send, test_cleanup, &producer,
			      NULL) == HX_OS_OK);

		test_wait(producer, HX_OS_OK);
		test_wait(consumer, HX_OS_OK);
		CHECK(receive.count == 1000 && receive.total == 500500);
	}
	else {
		hx_os_coro_cancel(consumer);
		test_wait(consumer, HX_OS_CANCELLED);
	}

	CHECK(hx_os_atomic_load(&receive.cleanups, HX_OS_RELAXED) == 1);

	hx_os_coro_channel_release(send.channel);
	CHECK(hx_os_coro_go(send.runtime, HX_OS_CORO_STACKLESS,
		      test_state_machine, &send, test_cleanup, &producer,
		      NULL) == HX_OS_OK);

	test_wait(producer, HX_OS_OK);
	CHECK(send.count == 300);

	test_close(&send.runtime);
}

typedef struct test_owned_channel {
	hx_os_coro_channel *channel;
	size_t copies;
	size_t drops;
	int fail;
} test_owned_channel;

static hx_os_status test_value_copy(void *out, const void *in, void *user)
{
	test_owned_channel *state = user;
	int *value;

	if (state->fail)
		return HX_OS_NO_MEMORY;

	value = malloc(sizeof(*value));
	if (!value)
		return HX_OS_NO_MEMORY;

	*value = **(int *const *)in;
	*(int **)out = value;
	++state->copies;
	/* A value callback must not suspend or recursively operate on channels.
	 */
	CHECK(hx_os_coro_yield(NULL) == HX_OS_BUSY);

	return HX_OS_OK;
}

static void test_value_drop(void *value, void *user)
{
	test_owned_channel *state = user;

	free(*(int **)value);
	++state->drops;
}

static hx_os_status test_owned_body(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_owned_channel *state = user;
	int number = 42, *source = &number, *received = NULL;
	hx_os_coro_case cases[2] = {{state->channel, &source, HX_OS_CORO_SEND},
		{state->channel, &source, HX_OS_CORO_SEND}};
	size_t selected = SIZE_MAX;

	(void)self;
	state->fail = 1;
	CHECK(hx_os_coro_select(cases, 2, HX_OS_DEADLINE_NOW, &selected,
		      error) == HX_OS_NO_MEMORY);
	CHECK(selected == SIZE_MAX && !state->copies && !state->drops);

	state->fail = 0;
	CHECK(hx_os_coro_select(cases, 2, HX_OS_DEADLINE_NOW, &selected,
		      error) == HX_OS_OK);
	CHECK(selected < 2 && state->copies == 2 && state->drops == 1);
	CHECK(hx_os_coro_channel_send_until(state->channel, &source,
		      HX_OS_DEADLINE_NOW, error) == HX_OS_WOULD_BLOCK);
	CHECK(!hx_os_coro_wait_pending());
	CHECK(state->copies == 3 && state->drops == 2);

	number = 99;
	CHECK(hx_os_coro_channel_receive_until(state->channel, &received,
		      HX_OS_DEADLINE_NOW, error) == HX_OS_OK);
	CHECK(received != source && *received == 42);

	test_value_drop(&received, state);
	CHECK(hx_os_coro_channel_send_until(state->channel, &source,
		      HX_OS_DEADLINE_NOW, error) == HX_OS_OK);
	CHECK(hx_os_coro_channel_close(state->channel, error) == HX_OS_OK);
	CHECK(state->copies == 4 && state->drops == 3);

	/* Closing keeps buffered owned values alive until drain/final release.
	 */
	return HX_OS_OK;
}

static void test_channel_ownership(void)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_context *context;
	hx_os_container_options values;
	hx_os_coro *task;
	test_owned_channel state = {0};

	options.max_io_handles = 0;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_runtime_create(context, &options, &runtime, NULL) ==
		HX_OS_OK);

	hx_os_container_options_init(&values, sizeof(int *));
	values.copy = test_value_copy;
	values.drop = test_value_drop;
	values.user = &state;
	CHECK(hx_os_coro_channel_create(
		      runtime, &values, 1, &state.channel, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS, test_owned_body,
		      &state, NULL, &task, NULL) == HX_OS_OK);

	test_wait(task, HX_OS_OK);

	hx_os_coro_channel_release(state.channel);
	CHECK(state.copies == state.drops);

	test_close(&runtime);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
}

static hx_os_status test_select_body(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	int send = 29, receive = 0;
	hx_os_coro_case cases[2] = {{state->channel, &send, HX_OS_CORO_SEND},
		{state->channel, &receive, HX_OS_CORO_RECEIVE}};
	size_t selected = SIZE_MAX;

	(void)self;
	CHECK(hx_os_coro_select(cases, 2, HX_OS_DEADLINE_NOW, &selected,
		      error) == HX_OS_WOULD_BLOCK);
	CHECK(selected == SIZE_MAX);
	CHECK(hx_os_coro_select(cases, 2, hx_os_deadline_after_ms(5), &selected,
		      error) == HX_OS_TIMEOUT);
	CHECK(hx_os_coro_channel_close(state->channel, error) == HX_OS_OK);
	CHECK(hx_os_coro_select(cases + 1, 1, HX_OS_DEADLINE_NOW, &selected,
		      error) == HX_OS_EOF &&
		selected == 0);

	return HX_OS_OK;
}

static hx_os_status test_blocked(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;

	state->self = self;
	hx_os_event_set(state->entered);
	CHECK(hx_os_event_wait_until(state->release, test_deadline(), error) ==
		HX_OS_OK);

	return HX_OS_OK;
}

static void test_shutdown(int stackful)
{
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_container_options values;
	hx_os_coro *task, *queued;
	test_state state;

	memset(&state, 0, sizeof(state));
	options.workers = 1;
	options.max_coroutines = 2;
	CHECK(hx_os_coro_runtime_create(NULL, &options, &state.runtime, NULL) ==
		HX_OS_OK);

	hx_os_container_options_init(&values, sizeof(int));
	CHECK(hx_os_coro_channel_create(state.runtime, &values, 0,
		      &state.channel, NULL) == HX_OS_OK);
	if (stackful) {
		CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKFUL,
			      test_select_body, &state, NULL, &task,
			      NULL) == HX_OS_OK);

		test_wait(task, HX_OS_OK);
	}

	hx_os_coro_channel_release(state.channel);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKLESS, test_blocked,
		      &state, test_cleanup_wait, &task, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKLESS,
		      test_state_machine, &state, test_cleanup, &queued,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_coro_runtime_close_until(state.runtime,
		      hx_os_deadline_after_ms(5), NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_coro_runtime_destroy(&state.runtime, NULL) == HX_OS_BUSY);

	hx_os_event_set(state.release);
	test_wait(task, HX_OS_OK);
	test_wait(queued, HX_OS_CANCELLED);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_RELAXED) == 2);

	test_close(&state.runtime);
	hx_os_event_destroy(state.release);
	hx_os_event_destroy(state.entered);
}

static hx_os_status test_float_state(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	int i;

	(void)self;
	CHECK(hx_os_event_wait_until(state->release, test_deadline(), error) ==
		HX_OS_OK);
	CHECK(fesetround(state->value) == 0);

	for (i = 0; i < 100; ++i) {
		CHECK(hx_os_coro_yield(error) == HX_OS_OK);
		CHECK(fegetround() == state->value);
	}

	return HX_OS_OK;
}

static void test_float_switch(void)
{
	test_state state[2];
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_coro *tasks[2];

	memset(state, 0, sizeof(state));
	options.workers = 1;
	options.max_io_handles = 0;
	CHECK(hx_os_coro_runtime_create(
		      NULL, &options, &state[0].runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state[0].release, NULL) ==
		HX_OS_OK);

	state[1] = state[0];
	state[0].value = FE_DOWNWARD;
	state[1].value = FE_UPWARD;
	CHECK(hx_os_coro_go(state[0].runtime, HX_OS_CORO_STACKFUL,
		      test_float_state, &state[0], NULL, &tasks[0],
		      NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(state[0].runtime, HX_OS_CORO_STACKFUL,
		      test_float_state, &state[1], NULL, &tasks[1],
		      NULL) == HX_OS_OK);

	hx_os_event_set(state[0].release);
	test_wait(tasks[0], HX_OS_OK);
	test_wait(tasks[1], HX_OS_OK);

	test_close(&state[0].runtime);
	hx_os_event_destroy(state[0].release);
}

typedef struct test_allocator {
	hx_os_atomic calls;
	hx_os_atomic live;
	int64_t fail_at;
} test_allocator;

typedef struct test_profile {
	hx_os_profiler *profiler;
	hx_os_event *start;
	const char *name;
	int step;
} test_profile;

static hx_os_status test_profile_body(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_profile *state = user;
	hx_os_status status;

	(void)self;
	if (!state->step)
		CHECK(hx_os_event_wait_until(state->start, test_deadline(),
			      error) == HX_OS_OK);

	HX_OS_CORO_BEGIN(state->step);
	CHECK(hx_os_profiler_start_root(state->profiler, state->name, error) ==
		HX_OS_OK);

	HX_OS_CORO_AWAIT(state->step, status, hx_os_coro_yield(error));
	CHECK(status == HX_OS_OK);
	CHECK(hx_os_profiler_reach(state->profiler, "child", error) ==
		HX_OS_OK);
	CHECK(hx_os_profiler_leave(state->profiler, error) == HX_OS_OK);
	CHECK(hx_os_profiler_leave(state->profiler, error) == HX_OS_OK);

	HX_OS_CORO_END(state->step);

	return HX_OS_OK;
}

static void test_profile_scopes(int kind)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_coro *tasks[2];
	test_profile state[2];

	memset(state, 0, sizeof(state));
	options.workers = 1;
	options.max_io_handles = 0;
	CHECK(hx_os_profiler_create(NULL, &state[0].profiler, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state[0].start, NULL) ==
		HX_OS_OK);

	state[1] = state[0];
	state[0].name = "first";
	state[1].name = "second";
	CHECK(hx_os_coro_runtime_create(NULL, &options, &runtime, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(runtime, kind, test_profile_body, &state[0], NULL,
		      &tasks[0], NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(runtime, kind, test_profile_body, &state[1], NULL,
		      &tasks[1], NULL) == HX_OS_OK);

	hx_os_event_set(state[0].start);
	test_wait(tasks[0], HX_OS_OK);
	test_wait(tasks[1], HX_OS_OK);
	/* Finished coroutines release their profiler caches before runtime
	 * close. */
	CHECK(hx_os_profiler_destroy(&state[0].profiler, NULL) == HX_OS_OK);

	test_close(&runtime);
	hx_os_event_destroy(state[0].start);
}

static hx_os_status test_profile_limit_body(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	hx_os_profiler **profilers = user;
	size_t i;

	(void)self;

	for (i = 0; i < 64; ++i)
		CHECK(hx_os_profiler_start_root(
			      profilers[i], "bounded", error) == HX_OS_OK);

	CHECK(hx_os_profiler_start_root(profilers[64], "full", error) ==
		HX_OS_LIMIT);

	/* All unfinished frames are aborted by coroutine retirement. */
	return HX_OS_OK;
}

static void test_profile_limit(void)
{
	hx_os_coro_runtime *runtime;
	hx_os_coro *task;
	hx_os_profiler *profilers[65];
	size_t i;

	for (i = 0; i < 65; ++i)
		CHECK(hx_os_profiler_create(NULL, &profilers[i], NULL) ==
			HX_OS_OK);

	CHECK(hx_os_coro_runtime_create(NULL, NULL, &runtime, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS,
		      test_profile_limit_body, profilers, NULL, &task,
		      NULL) == HX_OS_OK);

	test_wait(task, HX_OS_OK);

	for (i = 0; i < 65; ++i)
		CHECK(hx_os_profiler_destroy(&profilers[i], NULL) == HX_OS_OK);

	test_close(&runtime);
}

typedef struct test_preparing {
	hx_os_coro_runtime *runtime;
	hx_os_event *prepared;
	hx_os_event *proceed;
	hx_os_event *cleaning;
	hx_os_event *finish;
} test_preparing;

static hx_os_status test_prepare(void *user, void **out, hx_os_error *error)
{
	test_preparing *state = user;

	*out = state;
	hx_os_event_set(state->prepared);

	return hx_os_event_wait_until(state->proceed, test_deadline(), error);
}

static void test_prepare_cleanup(void *user)
{
	test_preparing *state = user;

	hx_os_event_set(state->cleaning);
	CHECK(hx_os_event_wait_until(state->finish, test_deadline(), NULL) ==
		HX_OS_OK);
}

static hx_os_status test_unused(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	(void)self;
	(void)user;
	(void)error;
	CHECK(0);

	return HX_OS_OK;
}

static void *test_submit_prepared(void *user)
{
	test_preparing *state = user;
	hx_os_coro *task;

	CHECK(hx_os_coro_go_prepared(state->runtime, HX_OS_CORO_STACKLESS,
		      test_prepare, state, test_unused, test_prepare_cleanup,
		      &task, NULL) == HX_OS_CLOSED);
	CHECK(!task);

	return NULL;
}

static void test_prepare_close(void)
{
	test_preparing state;
	hx_os_thread *thread;
	hx_os_deadline deadline;

	CHECK(hx_os_coro_runtime_create(NULL, NULL, &state.runtime, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.prepared, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.proceed, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.cleaning, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.finish, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, test_submit_prepared, &state, &thread,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.prepared, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_close_until(
		      state.runtime, HX_OS_DEADLINE_NOW, NULL) == HX_OS_BUSY);

	hx_os_event_set(state.proceed);
	CHECK(hx_os_event_wait_until(state.cleaning, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_close_until(
		      state.runtime, HX_OS_DEADLINE_NOW, NULL) == HX_OS_BUSY);

	hx_os_event_set(state.finish);
	deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(hx_os_thread_join_until(thread, deadline, NULL, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);

	test_close(&state.runtime);
	hx_os_event_destroy(state.prepared);
	hx_os_event_destroy(state.proceed);
	hx_os_event_destroy(state.cleaning);
	hx_os_event_destroy(state.finish);
}

static void *test_allocate(size_t bytes, void *user)
{
	test_allocator *allocator = user;
	void *memory;
	int64_t call =
		hx_os_atomic_fetch_add(&allocator->calls, 1, HX_OS_RELAXED) + 1;

	if (call == allocator->fail_at)
		return NULL;

	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(
			&allocator->live, 1, HX_OS_RELAXED);

	return memory;
}

static void test_free(void *memory, void *user)
{
	test_allocator *allocator = user;

	(void)hx_os_atomic_fetch_sub(&allocator->live, 1, HX_OS_RELAXED);

	free(memory);
}

static void test_create_failure(void)
{
	int64_t fail;

	for (fail = 1; fail <= 64; ++fail) {
		test_allocator allocator = {{0}, {0}, 0};
		hx_os_context_options options;
		hx_os_context *context;
		hx_os_coro_runtime *runtime = NULL;
		hx_os_status status;
		hx_os_error error;
		int64_t calls;

		hx_os_context_options_init(&options);
		options.allocator =
			(hx_os_allocator){test_allocate, test_free, &allocator};
		CHECK(hx_os_context_create(&options, &context, NULL) ==
			HX_OS_OK);

		hx_os_atomic_store(&allocator.calls, 0, HX_OS_RELAXED);
		allocator.fail_at = fail;

		status = hx_os_coro_runtime_create(
			context, NULL, &runtime, &error);
		calls = hx_os_atomic_load(&allocator.calls, HX_OS_RELAXED);
		if (status == HX_OS_OK)
			test_close(&runtime);
		else
			CHECK(status == HX_OS_NO_MEMORY &&
				error.status == status && !runtime);

		CHECK(hx_os_context_allocations(context) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
		CHECK(hx_os_atomic_load(&allocator.live, HX_OS_RELAXED) == 0);
		if (fail > calls)
			break;
	}

	CHECK(fail <= 64);
}

int main(void)
{
	int stackful =
		(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES) != 0;

	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINES)) {
		hx_os_coro_runtime *runtime;

		CHECK(hx_os_coro_runtime_create(NULL, NULL, &runtime, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return 0;
	}

	test_channels(1, 0, stackful);
	test_channels(2, 7, stackful);
	test_channel_ownership();
	test_shutdown(stackful);
	test_create_failure();

	test_prepare_close();
	if (stackful)
		test_float_switch();

	if (hx_os_capabilities() & HX_OS_CAP_PROFILER) {
		test_profile_scopes(HX_OS_CORO_STACKLESS);
		if (stackful)
			test_profile_scopes(HX_OS_CORO_STACKFUL);

		test_profile_limit();
	}

	puts("coroutine scheduling, channels and shutdown passed");

	return 0;
}
