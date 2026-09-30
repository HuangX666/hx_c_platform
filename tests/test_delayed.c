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
typedef struct schedule_state {
	hx_os_thread_pool *pool;
	hx_os_event *entered;
	hx_os_event *release;
	hx_os_atomic calls;
	hx_os_atomic cleanups;
	int order[4];
} schedule_state;
typedef struct scheduled {
	schedule_state *state;
	hx_os_deadline due;
	int identity;
} scheduled;
static hx_os_status HX_OS_CALL gate(hx_os_task *task, void *user)
{
	schedule_state *state = user;
	(void)task;
	hx_os_event_set(state->entered);
	return hx_os_event_wait_until(
		state->release, HX_OS_DEADLINE_INFINITE, NULL);
}
static hx_os_status HX_OS_CALL record(hx_os_task *task, void *user)
{
	scheduled *item = user;
	int64_t index;
	CHECK(hx_os_monotonic_ns() >= item->due);
	CHECK(hx_os_task_wait_until(task, HX_OS_DEADLINE_NOW, NULL, NULL) ==
		HX_OS_DEADLOCK);
	index = hx_os_atomic_fetch_add(&item->state->calls, 1, HX_OS_RELAXED);
	CHECK(index < 4);
	item->state->order[index] = item->identity;
	return HX_OS_OK;
}
static void HX_OS_CALL cleanup(void *user)
{
	scheduled *item = user;
	int inside;
	CHECK(hx_os_thread_pool_inside(item->state->pool, &inside) ==
			HX_OS_OK &&
		inside);
	(void)hx_os_atomic_fetch_add(&item->state->cleanups, 1, HX_OS_RELAXED);
}
static hx_os_status HX_OS_CALL prepare(void *user, void **out)
{
	int *count = user;
	++*count;
	*out = NULL;
	return HX_OS_NO_MEMORY;
}
static void test_order_and_limits(void)
{
	schedule_state state;
	scheduled values[3];
	hx_os_task *blocked = NULL, *tasks[3], *rejected = NULL;
	hx_os_thread_pool_options options;
	hx_os_status result;
	int preparations = 0, i;
	memset(&state, 0, sizeof(state));
	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	options.queue_capacity = 3;
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_create(NULL, &options, &state.pool, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(state.pool, gate, &state, NULL, &blocked,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	for (i = 0; i < 3; ++i) {
		values[i].state = &state;
		values[i].due = i < 2 ? hx_os_deadline_after_ms(80) : 0;
		values[i].identity = i + 1;
	}
	values[1].due = values[0].due;
	CHECK(hx_os_thread_pool_submit_delayed(state.pool, values[0].due,
		      record, &values[0], cleanup, &tasks[0],
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit_delayed(state.pool, values[1].due,
		      record, &values[1], cleanup, &tasks[1],
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(state.pool, record, &values[2], cleanup,
		      &tasks[2], NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit_delayed_prepared(state.pool,
		      hx_os_deadline_after_ms(500), prepare, &preparations,
		      record, cleanup, &rejected, NULL) == HX_OS_LIMIT &&
		!rejected && !preparations);
	CHECK(hx_os_thread_pool_submit(state.pool, record, NULL, NULL,
		      &rejected, NULL) == HX_OS_LIMIT &&
		!rejected);
	CHECK(hx_os_thread_pool_submit_delayed(state.pool,
		      HX_OS_DEADLINE_INFINITE, record, NULL, NULL, &rejected,
		      NULL) == HX_OS_INVALID);
	hx_os_event_set(state.release);
	for (i = 0; i < 3; ++i) {
		CHECK(hx_os_task_wait_until(tasks[i],
			      hx_os_deadline_after_ms(2000), &result,
			      NULL) == HX_OS_OK &&
			result == HX_OS_OK);
		hx_os_task_release(tasks[i]);
	}
	CHECK(state.order[0] == 3 && state.order[1] == 1 &&
		state.order[2] == 2);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 3);
	CHECK(hx_os_task_wait_until(blocked, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	hx_os_task_release(blocked);
	CHECK(hx_os_thread_pool_submit_delayed(state.pool,
		      hx_os_deadline_after_ms(60000), record, &values[0],
		      cleanup, &tasks[0], NULL) == HX_OS_OK);
	hx_os_task_cancel(tasks[0]);
	CHECK(hx_os_task_wait_until(tasks[0], hx_os_deadline_after_ms(2000),
		      &result, NULL) == HX_OS_OK &&
		result == HX_OS_CANCELLED);
	CHECK(hx_os_atomic_load(&state.calls, HX_OS_ACQUIRE) == 3);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 4);
	CHECK(hx_os_thread_pool_submit_delayed(state.pool,
		      hx_os_deadline_after_ms(60000), record, &values[0],
		      cleanup, &tasks[1], NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_close_until(state.pool, HX_OS_POOL_DRAIN,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_task_state(tasks[1]) == HX_OS_TASK_QUEUED);
	CHECK(hx_os_thread_pool_close_until(state.pool,
		      HX_OS_POOL_CANCEL_PENDING, hx_os_deadline_after_ms(2000),
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_destroy(&state.pool, NULL) == HX_OS_OK);
	for (i = 0; i < 2; ++i) {
		/* Cancellation and waiting remain safe after pool destruction.
		 */
		hx_os_task_cancel(tasks[i]);
		CHECK(hx_os_task_wait_until(tasks[i], HX_OS_DEADLINE_NOW,
			      &result, NULL) == HX_OS_OK &&
			result == HX_OS_CANCELLED);
		hx_os_task_release(tasks[i]);
	}
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.release);
}
typedef struct cancellation_race {
	hx_os_task *tasks[64];
	hx_os_event *entered;
	hx_os_atomic stop;
	hx_os_atomic cleanups;
} cancellation_race;
static hx_os_status HX_OS_CALL never_run(hx_os_task *task, void *user)
{
	(void)task;
	(void)user;
	CHECK(0);
	return HX_OS_OK;
}
static void HX_OS_CALL race_cleanup(void *user)
{
	cancellation_race *state = user;
	(void)hx_os_atomic_fetch_add(&state->cleanups, 1, HX_OS_RELAXED);
}
static void *cancel_worker(void *user)
{
	cancellation_race *state = user;
	hx_os_event_set(state->entered);
	do {
		size_t i;
		for (i = 0; i < 64; ++i)
			hx_os_task_cancel(state->tasks[i]);
	} while (!hx_os_atomic_load(&state->stop, HX_OS_ACQUIRE));
	return NULL;
}
static void test_cancel_destroy_race(void)
{
	cancellation_race state;
	hx_os_thread_pool *pool = NULL;
	hx_os_thread *canceller = NULL;
	hx_os_thread_pool_options options;
	size_t i;
	memset(&state, 0, sizeof(state));
	hx_os_thread_pool_options_init(&options);
	options.threads = 4;
	options.queue_capacity = 64;
	CHECK(hx_os_event_create(NULL, 1, 0, &state.entered, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_create(NULL, &options, &pool, NULL) ==
		HX_OS_OK);
	for (i = 0; i < 64; ++i)
		CHECK(hx_os_thread_pool_submit_delayed(pool,
			      hx_os_deadline_after_ms(60000), never_run, &state,
			      race_cleanup, &state.tasks[i], NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, cancel_worker, &state, &canceller,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_close_until(pool, HX_OS_POOL_CANCEL_PENDING,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_destroy(&pool, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.cleanups, HX_OS_ACQUIRE) == 64);
	hx_os_atomic_store(&state.stop, 1, HX_OS_RELEASE);
	CHECK(hx_os_thread_join_until(canceller, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&canceller, NULL) == HX_OS_OK);
	for (i = 0; i < 64; ++i)
		hx_os_task_release(state.tasks[i]);
	hx_os_event_destroy(state.entered);
}
int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_DELAYED_TASKS)) {
		hx_os_task *task = NULL;
		CHECK(hx_os_thread_pool_submit_delayed(NULL, 0, record, NULL,
			      NULL, &task, NULL) == HX_OS_NOT_SUPPORTED &&
			!task);
		return 0;
	}
	test_order_and_limits();
	test_cancel_destroy_race();
	return 0;
}
