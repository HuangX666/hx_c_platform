#include "thread_pool_private.h"
#include <stdio.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct gated_work {
	hx_os_thread_pool *pool;
	hx_os_event *work_entered;
	hx_os_event *work_release;
	hx_os_event *cleanup_entered;
	hx_os_event *cleanup_release;
	hx_os_atomic cleanups;
} gated_work;

typedef struct pool_waiter {
	hx_os_thread_pool *pool;
	hx_os_atomic returned;
	int idle;
} pool_waiter;

static void gate_init(
	gated_work *gate, hx_os_context *context, hx_os_thread_pool *pool)
{
	memset(gate, 0, sizeof(*gate));
	gate->pool = pool;
	CHECK(hx_os_event_create(context, 1, 0, &gate->work_entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &gate->work_release, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &gate->cleanup_entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &gate->cleanup_release, NULL) ==
		HX_OS_OK);
}

static void await_event(hx_os_event *event)
{
	CHECK(hx_os_event_wait_until(
		      event, hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
}

static void gate_destroy(gated_work *gate)
{
	CHECK(hx_os_atomic_load(&gate->cleanups, HX_OS_ACQUIRE) == 1);
	hx_os_event_destroy(gate->work_entered);
	hx_os_event_destroy(gate->work_release);
	hx_os_event_destroy(gate->cleanup_entered);
	hx_os_event_destroy(gate->cleanup_release);
}

static hx_os_status gated_task(hx_os_task *task, void *user)
{
	gated_work *gate = user;

	(void)task;
	CHECK(hx_os_thread_pool_wait_until(
		      gate->pool, HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);
	CHECK(hx_os_thread_pool_wait_idle_until(
		      gate->pool, HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);
	hx_os_event_set(gate->work_entered);
	await_event(gate->work_release);

	return HX_OS_IO_ERROR;
}

static void gated_cleanup(void *user)
{
	gated_work *gate = user;

	CHECK(hx_os_thread_pool_wait_until(
		      gate->pool, HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);
	hx_os_event_set(gate->cleanup_entered);
	await_event(gate->cleanup_release);
	hx_os_atomic_fetch_add(&gate->cleanups, 1, HX_OS_RELEASE);
}

static hx_os_status immediate_task(hx_os_task *task, void *user)
{
	(void)task;
	(void)user;

	return HX_OS_OK;
}

static void *wait_thread(void *user)
{
	pool_waiter *waiter = user;
	hx_os_deadline deadline = hx_os_deadline_after_ms(3000);
	hx_os_status status = waiter->idle
		? hx_os_thread_pool_wait_idle_until(
			  waiter->pool, deadline, NULL)
		: hx_os_thread_pool_wait_until(waiter->pool, deadline, NULL);

	hx_os_atomic_store(&waiter->returned, status, HX_OS_RELEASE);

	return NULL;
}

/* Observe registration under the real mutex: no timing-only race fixture. */
static void await_waiters(hx_os_thread_pool *pool, size_t count)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(3000);

	for (;;) {
		size_t waiters;
		uint64_t now;

		CHECK(hx_os_mutex_lock(pool->mutex, NULL) == HX_OS_OK);
		waiters = pool->waiters;
		CHECK(hx_os_mutex_unlock(pool->mutex, NULL) == HX_OS_OK);
		if (waiters == count)
			return;

		CHECK(hx_os_monotonic_read(&now, NULL) == HX_OS_OK);
		CHECK(now < deadline);
		hx_os_cpu_relax();
	}
}

static void join_thread(hx_os_thread **thread)
{
	CHECK(hx_os_thread_join_until(*thread, hx_os_deadline_after_ms(3000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(thread, NULL) == HX_OS_OK);
}

int main(void)
{
	hx_os_context *context = NULL;
	hx_os_thread_pool *pool = NULL;
	hx_os_thread_pool_options options;
	hx_os_task *first, *second, *late, *again;
	hx_os_thread *snapshot_thread, *idle_thread;
	gated_work earlier, later;
	pool_waiter snapshot, idle;
	hx_os_status work_status;

	CHECK(hx_os_thread_pool_wait_until(NULL, 0, NULL) == HX_OS_INVALID);
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	hx_os_thread_pool_options_init(&options);
	options.threads = 2;
	CHECK(hx_os_thread_pool_create(context, &options, &pool, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_pool_wait_until(pool, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_wait_idle_until(pool, 0, NULL) == HX_OS_OK);

	gate_init(&earlier, context, pool);
	gate_init(&later, context, pool);
	CHECK(hx_os_thread_pool_submit(pool, gated_task, &earlier,
		      gated_cleanup, &first, NULL) == HX_OS_OK);
	await_event(earlier.work_entered);
	CHECK(hx_os_thread_pool_submit(pool, immediate_task, NULL, NULL,
		      &second, NULL) == HX_OS_OK);
	CHECK(hx_os_task_wait_until(second, hx_os_deadline_after_ms(3000), NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_wait_until(pool, 0, NULL) == HX_OS_TIMEOUT);

	memset(&snapshot, 0, sizeof(snapshot));
	memset(&idle, 0, sizeof(idle));
	snapshot.pool = idle.pool = pool;
	idle.idle = 1;
	hx_os_atomic_store(&snapshot.returned, -1, HX_OS_RELAXED);
	hx_os_atomic_store(&idle.returned, -1, HX_OS_RELAXED);
	CHECK(hx_os_thread_create(context, wait_thread, &snapshot,
		      &snapshot_thread, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(context, wait_thread, &idle, &idle_thread,
		      NULL) == HX_OS_OK);
	await_waiters(pool, 2);

	CHECK(hx_os_thread_pool_submit(pool, gated_task, &later, gated_cleanup,
		      &late, NULL) == HX_OS_OK);
	await_event(later.work_entered);
	hx_os_event_set(earlier.work_release);
	await_event(earlier.cleanup_entered);
	CHECK(hx_os_atomic_load(&snapshot.returned, HX_OS_ACQUIRE) == -1);
	CHECK(hx_os_atomic_load(&idle.returned, HX_OS_ACQUIRE) == -1);
	CHECK(hx_os_thread_pool_wait_until(pool, 0, NULL) == HX_OS_TIMEOUT);

	hx_os_event_set(earlier.cleanup_release);
	join_thread(&snapshot_thread);
	CHECK(hx_os_atomic_load(&snapshot.returned, HX_OS_ACQUIRE) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&idle.returned, HX_OS_ACQUIRE) == -1);
	CHECK(hx_os_thread_pool_wait_idle_until(pool, 0, NULL) ==
		HX_OS_TIMEOUT);
	CHECK(hx_os_task_wait_until(first, 0, &work_status, NULL) == HX_OS_OK);
	CHECK(work_status == HX_OS_IO_ERROR);

	hx_os_event_set(later.work_release);
	hx_os_event_set(later.cleanup_release);
	join_thread(&idle_thread);
	CHECK(hx_os_atomic_load(&idle.returned, HX_OS_ACQUIRE) == HX_OS_OK);
	CHECK(hx_os_thread_pool_submit(pool, immediate_task, NULL, NULL, &again,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_wait_until(
		      pool, hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	hx_os_task_release(again);

#if HX_OS_ENABLE_DELAYED_TASKS
	CHECK(hx_os_thread_pool_submit_delayed(pool,
		      hx_os_deadline_after_ms(60000), immediate_task, NULL,
		      NULL, &again, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_pool_wait_until(pool, 0, NULL) == HX_OS_TIMEOUT);
	hx_os_task_cancel(again);
	CHECK(hx_os_thread_pool_wait_idle_until(
		      pool, hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	CHECK(hx_os_task_wait_until(again, 0, &work_status, NULL) == HX_OS_OK);
	CHECK(work_status == HX_OS_CANCELLED);
	hx_os_task_release(again);
#endif

	CHECK(hx_os_thread_pool_shutdown(
		      &pool, hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
	hx_os_task_release(first);
	hx_os_task_release(second);
	hx_os_task_release(late);
	gate_destroy(&earlier);
	gate_destroy(&later);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	puts("pool snapshot and idle waits passed");

	return 0;
}
