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

typedef struct work_data {
	hx_os_event *entered;
	hx_os_event *release;
	hx_os_atomic cleanups;
	hx_os_atomic calls;
} work_data;

static hx_os_status work(hx_os_task *task, void *user)
{
	work_data *data = user;
	hx_os_status result;

	CHECK(hx_os_task_wait_until(task, HX_OS_DEADLINE_INFINITE, &result,
		      NULL) == HX_OS_DEADLOCK);

	(void)hx_os_atomic_fetch_add(&data->calls, 1, HX_OS_RELAXED);
	hx_os_event_set(data->entered);

	return hx_os_event_wait_until(
		data->release, HX_OS_DEADLINE_INFINITE, NULL);
}

static void cleanup(void *user)
{
	work_data *data = user;

	(void)hx_os_atomic_fetch_add(&data->cleanups, 1, HX_OS_RELAXED);
}

typedef struct identity_test {
	hx_os_thread_pool *own;
	hx_os_thread_pool *other;
	hx_os_task *finished;
	int cleanups;
} identity_test;

static hx_os_status finished_work(hx_os_task *task, void *user)
{
	(void)task;
	(void)user;

	return HX_OS_OK;
}

static hx_os_status identity_work(hx_os_task *task, void *user)
{
	identity_test *state = user;
	int inside = -1;

	CHECK(hx_os_thread_pool_inside(state->own, &inside) == HX_OS_OK &&
		inside == 1);
	CHECK(hx_os_thread_pool_inside(state->other, &inside) == HX_OS_OK &&
		inside == 0);
	CHECK(hx_os_task_wait_until(task, HX_OS_DEADLINE_NOW, NULL, NULL) ==
		HX_OS_DEADLOCK);
	CHECK(hx_os_task_wait_until(state->finished, HX_OS_DEADLINE_NOW, NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_close_until(state->own, HX_OS_POOL_DRAIN,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);

	return HX_OS_OK;
}

static void identity_cleanup(void *user)
{
	identity_test *state = user;
	int inside = 0;

	CHECK(hx_os_thread_pool_inside(state->own, &inside) == HX_OS_OK &&
		inside == 1);

	++state->cleanups;
}

static void test_identity(void)
{
	identity_test state = {NULL, NULL, NULL, 0};
	hx_os_thread_pool_options options;
	hx_os_task *task = NULL;
	int inside = -1;

	CHECK(hx_os_thread_pool_inside(NULL, &inside) == HX_OS_INVALID);
	CHECK(inside == -1);

	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	CHECK(hx_os_thread_pool_create(NULL, &options, &state.own, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_pool_create(NULL, &options, &state.other, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_pool_inside(state.own, NULL) == HX_OS_INVALID);
	CHECK(hx_os_thread_pool_inside(state.own, &inside) == HX_OS_OK &&
		inside == 0);
	CHECK(hx_os_thread_pool_submit(state.own, finished_work, NULL, NULL,
		      &state.finished, NULL) == HX_OS_OK);
	CHECK(hx_os_task_wait_until(state.finished,
		      hx_os_deadline_after_ms(2000), NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(state.own, identity_work, &state,
		      identity_cleanup, &task, NULL) == HX_OS_OK);
	CHECK(hx_os_task_wait_until(task, hx_os_deadline_after_ms(2000), NULL,
		      NULL) == HX_OS_OK &&
		state.cleanups == 1);
	CHECK(hx_os_thread_pool_close_until(state.own, HX_OS_POOL_DRAIN,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_inside(state.own, &inside) == HX_OS_OK &&
		inside == 0);
	CHECK(hx_os_thread_pool_destroy(&state.own, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_close_until(state.other, HX_OS_POOL_DRAIN,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_destroy(&state.other, NULL) == HX_OS_OK);

	hx_os_task_release(state.finished);
	hx_os_task_release(task);
}

int main(void)
{
	hx_os_context *context;
	hx_os_thread_pool_options options;
	hx_os_thread_pool *pool;
	hx_os_task *first;
	hx_os_task *second;
	hx_os_task *third;
	hx_os_task *rejected;
	hx_os_status result;
	work_data data;

	test_identity();
	memset(&data, 0, sizeof(data));
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &data.entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &data.release, NULL) ==
		HX_OS_OK);

	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	options.queue_capacity = 2;
	CHECK(hx_os_thread_pool_create(context, &options, &pool, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(
		      pool, work, &data, cleanup, &first, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(data.entered,
		      hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(
		      pool, work, &data, cleanup, &second, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(
		      pool, work, &data, cleanup, &third, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(pool, work, &data, cleanup, &rejected,
		      NULL) == HX_OS_LIMIT);
	CHECK(hx_os_thread_pool_close_until(pool, HX_OS_POOL_CANCEL_PENDING,
		      hx_os_deadline_after_ms(5), NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_thread_pool_destroy(&pool, NULL) == HX_OS_BUSY);
	CHECK(hx_os_thread_pool_submit(pool, work, &data, cleanup, &rejected,
		      NULL) == HX_OS_CLOSED);

	hx_os_event_set(data.release);
	CHECK(hx_os_thread_pool_close_until(pool, HX_OS_POOL_CANCEL_PENDING,
		      hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_destroy(&pool, NULL) == HX_OS_OK);
	CHECK(hx_os_task_wait_until(first, 0, &result, NULL) == HX_OS_OK);
	CHECK(result == HX_OS_OK);
	CHECK(hx_os_task_wait_until(second, 0, &result, NULL) == HX_OS_OK);
	CHECK(result == HX_OS_CANCELLED);
	CHECK(hx_os_atomic_load(&data.calls, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_atomic_load(&data.cleanups, HX_OS_ACQUIRE) == 3);

	hx_os_task_release(first);
	hx_os_task_release(second);
	hx_os_task_release(third);
	hx_os_event_destroy(data.entered);
	hx_os_event_destroy(data.release);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	puts("bounded task pool contracts passed");

	return 0;
}
