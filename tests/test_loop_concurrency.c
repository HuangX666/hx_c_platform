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

typedef struct post_state {
	hx_os_loop *loop;
	hx_os_atomic completed;
	hx_os_atomic workers_done;
	hx_os_deadline limit;
} post_state;

static void complete(
	hx_os_request *request, const hx_os_completion *result, void *user)
{
	post_state *state = user;
	(void)request;
	CHECK(result->error.status == HX_OS_OK);
	(void)hx_os_atomic_fetch_add(&state->completed, 1, HX_OS_RELAXED);
}

static void *worker(void *user)
{
	post_state *state = user;
	size_t i;
	for (i = 0; i < 500; ++i) {
		hx_os_operation op;
		hx_os_request *request;
		hx_os_status status;
		hx_os_operation_init(&op, HX_OS_OP_POST);
		do {
			status = hx_os_loop_submit(state->loop, &op, complete,
				state, &request, NULL);
			CHECK(status == HX_OS_OK || status == HX_OS_LIMIT);
			CHECK(!hx_os_deadline_expired(state->limit));
			if (status == HX_OS_LIMIT)
				(void)hx_os_sleep_until(
					hx_os_deadline_after_ms(1), NULL);
		} while (status == HX_OS_LIMIT);
		hx_os_request_release(request);
	}
	(void)hx_os_atomic_fetch_add(&state->workers_done, 1, HX_OS_RELEASE);
	return NULL;
}

static void cancelled(
	hx_os_request *request, const hx_os_completion *result, void *user)
{
	size_t *calls = user;
	(void)request;
	CHECK(result->error.status == HX_OS_CANCELLED);
	++*calls;
}

int main(void)
{
	hx_os_context *context;
	hx_os_network *network;
	hx_os_loop_options options;
	hx_os_thread *threads[4];
	post_state state;
	hx_os_loop *bounded;
	hx_os_operation op;
	hx_os_request *first;
	hx_os_request *second;
	hx_os_request *rejected;
	size_t calls = 0;
	size_t i;
	memset(&state, 0, sizeof(state));
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_network_create(context, &network, NULL) == HX_OS_OK);
	hx_os_loop_options_init(&options);
	options.max_requests = 32;
	options.dns_threads = 0;
	CHECK(hx_os_loop_create(network, &options, &state.loop, NULL) ==
		HX_OS_OK);
	state.limit = hx_os_deadline_after_ms(10000);
	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(context, worker, &state, &threads[i],
			      NULL) == HX_OS_OK);
	while (hx_os_atomic_load(&state.workers_done, HX_OS_ACQUIRE) < 4 ||
		hx_os_loop_pending(state.loop)) {
		hx_os_status status = hx_os_loop_poll_until(
			state.loop, hx_os_deadline_after_ms(10), NULL);
		CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);
		CHECK(!hx_os_deadline_expired(state.limit));
		CHECK(hx_os_loop_pending(state.loop) <= 32);
	}
	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(
			      threads[i], state.limit, NULL, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}
	CHECK(hx_os_atomic_load(&state.completed, HX_OS_ACQUIRE) == 2000);
	CHECK(hx_os_loop_close_until(state.loop, state.limit, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_loop_destroy(&state.loop, NULL) == HX_OS_OK);

	options.max_requests = 2;
	CHECK(hx_os_loop_create(network, &options, &bounded, NULL) == HX_OS_OK);
	hx_os_operation_init(&op, HX_OS_OP_TIMER);
	CHECK(hx_os_loop_submit(bounded, &op, cancelled, &calls, &first,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_loop_submit(bounded, &op, cancelled, &calls, &second,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_loop_submit(bounded, &op, cancelled, &calls, &rejected,
		      NULL) == HX_OS_LIMIT &&
		!rejected);
	CHECK(hx_os_loop_run_until(bounded, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_TIMEOUT);
	CHECK(calls == 0);
	CHECK(hx_os_loop_close_until(bounded, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(calls == 2);
	CHECK(hx_os_loop_submit(bounded, &op, cancelled, &calls, &rejected,
		      NULL) == HX_OS_CLOSED);
	CHECK(hx_os_loop_destroy(&bounded, NULL) == HX_OS_OK);
	hx_os_request_release(first);
	hx_os_request_release(second);
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("bounded concurrent loop posting passed");
	return 0;
}
