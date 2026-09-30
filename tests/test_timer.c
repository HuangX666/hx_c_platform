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

typedef struct timer_test {
	hx_os_timer *timer;
	hx_os_atomic stop_ready;
	int ticks;
	int cleanup;
	int limit;
	int slow;
} timer_test;

typedef struct allocator_test {
	int fail;
	int calls;
	int live;
} allocator_test;

static void *allocate(size_t bytes, void *user)
{
	allocator_test *state = user;
	void *memory;
	++state->calls;
	if (state->fail)
		return NULL;
	memory = malloc(bytes);
	if (memory)
		++state->live;
	return memory;
}

static void deallocate(void *memory, void *user)
{
	allocator_test *state = user;
	--state->live;
	free(memory);
}

static void tick(hx_os_timer *timer, const hx_os_error *error, void *user)
{
	timer_test *state = user;
	CHECK(error->status == HX_OS_OK);
	CHECK(timer == state->timer);
	CHECK(hx_os_timer_wait_until(timer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_DEADLOCK);
	++state->ticks;
	if (state->slow)
		CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(20), NULL) ==
			HX_OS_OK);
	if (state->ticks == state->limit)
		hx_os_timer_stop(timer);
}

static void cleanup(void *user)
{
	timer_test *state = user;
	++state->cleanup;
	CHECK(hx_os_timer_close_until(&state->timer, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_DEADLOCK);
}

static void posted(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	int *count = user;
	(void)request;
	CHECK(completion->error.status == HX_OS_OK);
	++*count;
}

static void *stop_thread(void *user)
{
	timer_test *state = user;
	CHECK(hx_os_timer_wait_until(state->timer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_TIMEOUT);
	hx_os_timer_stop(state->timer);
	hx_os_atomic_store(&state->stop_ready, 1, HX_OS_RELEASE);
	CHECK(hx_os_timer_wait_until(state->timer,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(state->cleanup == 1);
	return NULL;
}

int main(void)
{
	hx_os_loop *loop = NULL;
	hx_os_context *context = NULL;
	hx_os_loop_options options;
	hx_os_context_options context_options;
	hx_os_operation operation;
	hx_os_request *request = NULL;
	hx_os_thread *thread = NULL;
	timer_test state;
	allocator_test allocations = {0, 0, 0};
	int calls, count = 0;
	memset(&state, 0, sizeof(state));
	if (!(hx_os_capabilities() & HX_OS_CAP_TIMER)) {
		CHECK(hx_os_timer_create(NULL, 1, tick, &state, &state.timer,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(!state.timer);
		return 0;
	}
	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = allocate;
	context_options.allocator.deallocate = deallocate;
	context_options.allocator.user = &allocations;
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	hx_os_loop_options_init(&options);
	options.max_requests = 1;
	CHECK(hx_os_loop_create_local(context, &options, &loop, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_timer_create(loop, 0, tick, &state, &state.timer, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_timer_create(loop, UINT64_MAX, tick, &state, &state.timer,
		      NULL) == HX_OS_RANGE);
	allocations.fail = 1;
	CHECK(hx_os_timer_create(loop, 1, tick, &state, &state.timer, NULL) ==
			HX_OS_NO_MEMORY &&
		!state.timer);
	allocations.fail = 0;
	state.limit = 3;
	CHECK(hx_os_timer_create_with_cleanup(loop, 1000000, tick, cleanup,
		      &state, &state.timer, NULL) == HX_OS_OK);
	CHECK(!state.ticks && !state.cleanup && hx_os_loop_pending(loop) == 1);
	hx_os_operation_init(&operation, HX_OS_OP_POST);
	CHECK(hx_os_loop_submit(loop, &operation, posted, &count, &request,
		      NULL) == HX_OS_LIMIT &&
		!request);
	calls = allocations.calls;
	allocations.fail = 1;
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(state.ticks == 3 && state.cleanup == 1);
	CHECK(allocations.calls == calls);
	CHECK(hx_os_timer_wait_until(state.timer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_timer_close_until(&state.timer, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK &&
		!state.timer);
	allocations.fail = 0;
	/* The old one-shot timer remains a single completion. */
	hx_os_operation_init(&operation, HX_OS_OP_TIMER);
	operation.deadline = HX_OS_DEADLINE_NOW;
	CHECK(hx_os_loop_submit(loop, &operation, posted, &count, &request,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
			HX_OS_OK &&
		count == 1);
	hx_os_request_release(request);
	memset(&state, 0, sizeof(state));
	state.slow = 1;
	CHECK(hx_os_timer_create_with_cleanup(loop, 1000000, tick, cleanup,
		      &state, &state.timer, NULL) == HX_OS_OK);
	CHECK(hx_os_sleep_until(hx_os_deadline_after_ms(20), NULL) == HX_OS_OK);
	CHECK(hx_os_loop_poll_until(
		      loop, hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK &&
		state.ticks == 1);
	CHECK(hx_os_timer_close_until(&state.timer, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK &&
		state.cleanup == 1);
	memset(&state, 0, sizeof(state));
	CHECK(hx_os_timer_create_with_cleanup(loop, UINT64_C(10000000000), tick,
		      cleanup, &state, &state.timer, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, stop_thread, &state, &thread, NULL) ==
		HX_OS_OK);
	while (!hx_os_atomic_load(&state.stop_ready, HX_OS_ACQUIRE))
		hx_os_cpu_relax();
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(!state.ticks && state.cleanup == 1);
	CHECK(hx_os_timer_close_until(&state.timer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	memset(&state, 0, sizeof(state));
	CHECK(hx_os_timer_create_with_cleanup(loop, UINT64_C(10000000000), tick,
		      cleanup, &state, &state.timer, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_BUSY && loop);
	CHECK(state.cleanup == 1 && !state.ticks);
	CHECK(hx_os_timer_close_until(&state.timer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!allocations.live);
	puts("periodic timer contracts passed");
	return 0;
}
