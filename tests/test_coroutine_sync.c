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

typedef struct test_state {
	hx_os_coro_runtime *runtime;
	hx_os_coro_mutex *mutex;
	hx_os_coro_condition *condition;
	hx_os_coro_event *event;
	hx_os_coro_semaphore *semaphore;
	hx_os_coro_rwlock *rwlock;
	hx_os_coro_local_key *key;
	hx_os_event *entered;
	hx_os_event *holding;
	hx_os_event *release;
	hx_os_atomic drops;
	int value;
	int step;
} test_state;

static hx_os_deadline test_deadline(void)
{
	return hx_os_deadline_after_ms(5000);
}

static void test_local_drop(void *value, void *user)
{
	test_state *state = user;

	CHECK(value == &state->value);

	(void)hx_os_atomic_fetch_add(&state->drops, 1, HX_OS_RELAXED);
}

static hx_os_status test_reader(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;
	hx_os_status status;

	(void)self;
	HX_OS_CORO_BEGIN(state->step);
	CHECK(hx_os_coro_local_get(state->key) == NULL);
	CHECK(hx_os_coro_local_set(state->key, &state->value, error) ==
		HX_OS_OK);

	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_mutex_lock_until(
			state->mutex, test_deadline(), error));
	CHECK(status == HX_OS_OK);

	while (!state->value) {
		HX_OS_CORO_AWAIT(state->step, status,
			hx_os_coro_condition_wait_until(state->condition,
				state->mutex, test_deadline(), error));
		CHECK(status == HX_OS_OK);
	}

	CHECK(hx_os_coro_mutex_unlock(state->mutex, error) == HX_OS_OK);

	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_semaphore_wait_until(
			state->semaphore, test_deadline(), error));
	CHECK(status == HX_OS_OK);

	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_event_wait_until(
			state->event, test_deadline(), error));
	CHECK(status == HX_OS_OK);
	CHECK(hx_os_coro_local_get(state->key) == &state->value);

	HX_OS_CORO_END(state->step);

	return HX_OS_OK;
}

static hx_os_status test_writer(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;

	(void)self;
	CHECK(hx_os_coro_local_get(state->key) == NULL);
	CHECK(hx_os_coro_mutex_lock_until(
		      state->mutex, test_deadline(), error) == HX_OS_OK);
	CHECK(hx_os_coro_mutex_lock_until(state->mutex, HX_OS_DEADLINE_NOW,
		      error) == HX_OS_DEADLOCK);

	state->value = 42;
	CHECK(hx_os_coro_condition_notify_all(state->condition, error) ==
		HX_OS_OK);
	CHECK(hx_os_coro_sleep_ms(2, error) == HX_OS_OK);
	CHECK(hx_os_coro_mutex_unlock(state->mutex, error) == HX_OS_OK);
	CHECK(hx_os_coro_semaphore_post(state->semaphore, 1, error) ==
		HX_OS_OK);
	CHECK(hx_os_coro_event_set(state->event, error) == HX_OS_OK);

	return HX_OS_OK;
}

static hx_os_status test_cancelled(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;

	(void)self;
	CHECK(hx_os_coro_mutex_lock_until(
		      state->mutex, test_deadline(), error) == HX_OS_OK);

	hx_os_event_set(state->entered);
	CHECK(hx_os_coro_condition_wait_until(state->condition, state->mutex,
		      HX_OS_DEADLINE_INFINITE, error) == HX_OS_CANCELLED);
	CHECK(hx_os_coro_mutex_unlock(state->mutex, error) == HX_OS_OK);

	return HX_OS_OK;
}

static hx_os_status test_holder(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	test_state *state = user;

	(void)self;
	CHECK(hx_os_coro_mutex_lock_until(
		      state->mutex, test_deadline(), error) == HX_OS_OK);

	hx_os_event_set(state->holding);
	CHECK(hx_os_event_wait_until(state->release, test_deadline(), error) ==
		HX_OS_OK);
	CHECK(hx_os_coro_mutex_unlock(state->mutex, error) == HX_OS_OK);

	return HX_OS_OK;
}

static hx_os_status test_failure(
	hx_os_coro *self, void *user, hx_os_error *error)
{
	(void)self;
	(void)user;
	*error = (hx_os_error){HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 13, 7};

	return HX_OS_IO_ERROR;
}

static void test_wait(hx_os_coro *task)
{
	hx_os_error result;

	CHECK(hx_os_coro_wait_until(task, test_deadline(), &result, NULL) ==
		HX_OS_OK);
	CHECK(result.status == HX_OS_OK);

	hx_os_coro_release(task);
}

static void *test_native_waiter(void *user)
{
	test_state *state = user;

	CHECK(hx_os_coro_mutex_lock_until(
		      state->mutex, test_deadline(), NULL) == HX_OS_OK);
	hx_os_event_set(state->entered);
	state->value = hx_os_coro_condition_wait_until(state->condition,
		state->mutex, hx_os_deadline_after_ms(500), NULL);
	CHECK(hx_os_coro_mutex_unlock(state->mutex, NULL) == HX_OS_OK);
	hx_os_event_set(state->holding);

	return NULL;
}

static void test_native_relock(void)
{
	test_state state = {0};
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_context *context = NULL;
	hx_os_thread *thread = NULL;
	hx_os_deadline close_deadline;

	options.workers = 1;
	options.max_io_handles = 0;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_runtime_create(
		      context, &options, &state.runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_mutex_create(state.runtime, &state.mutex, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_condition_create(
		      state.runtime, &state.condition, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.holding, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(context, test_native_waiter, &state, &thread,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_mutex_lock_until(state.mutex, test_deadline(), NULL) ==
		HX_OS_OK);

	/* The condition deadline expires while this host still owns the lock.
	 */
	CHECK(hx_os_event_wait_until(state.holding,
		      hx_os_deadline_after_ms(600), NULL) == HX_OS_TIMEOUT);
	close_deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(hx_os_coro_runtime_close_until(
		      state.runtime, close_deadline, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_sync_close(state.condition, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_sync_close(state.mutex, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.holding, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_TIMEOUT);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);

	CHECK(hx_os_coro_mutex_unlock(state.mutex, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, test_deadline(), NULL, NULL) ==
		HX_OS_OK);
	CHECK(state.value == HX_OS_TIMEOUT);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	hx_os_coro_sync_release(state.mutex);
	hx_os_coro_sync_release(state.condition);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.holding);
	CHECK(hx_os_coro_runtime_destroy(&state.runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
}

int main(void)
{
	test_state state;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;
	hx_os_context *context;
	hx_os_coro *reader, *writer;
	hx_os_coro_errors errors;
	hx_os_deadline deadline;

	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINES)) {
		hx_os_coro_mutex *mutex = NULL;
		hx_os_coro_local_key *key = NULL;

		CHECK(hx_os_coro_mutex_create(NULL, &mutex, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!mutex);
		CHECK(hx_os_coro_local_key_create(NULL, NULL, NULL, &key,
			      NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(!key);
		CHECK(hx_os_coro_spawn(NULL, HX_OS_CORO_STACKLESS, test_failure,
			      NULL, NULL, NULL) == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	test_native_relock();
	if (!(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES))
		return 0;

	memset(&state, 0, sizeof(state));
	options.max_io_handles = 0;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_runtime_create(
		      context, &options, &state.runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_mutex_create(state.runtime, &state.mutex, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_condition_create(
		      state.runtime, &state.condition, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_event_create(
		      state.runtime, 1, 0, &state.event, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_semaphore_create(
		      state.runtime, 0, 2, &state.semaphore, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_rwlock_create(state.runtime, &state.rwlock, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_local_key_create(context, test_local_drop, &state,
		      &state.key, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKLESS, test_reader,
		      &state, NULL, &reader, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKFUL, test_writer,
		      &state, NULL, &writer, NULL) == HX_OS_OK);

	test_wait(reader);
	test_wait(writer);
	CHECK(hx_os_atomic_load(&state.drops, HX_OS_RELAXED) == 1);
	CHECK(hx_os_coro_rwlock_read_until(
		      state.rwlock, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_coro_rwlock_write_until(state.rwlock, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_WOULD_BLOCK);
	CHECK(hx_os_coro_rwlock_read_unlock(state.rwlock, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_rwlock_write_until(
		      state.rwlock, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_coro_rwlock_write_unlock(state.rwlock, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_semaphore_post(state.semaphore, 3, NULL) ==
		HX_OS_LIMIT);
	CHECK(hx_os_coro_event_wait_until(
		      state.event, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_event_reset(state.event, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_event_wait_until(state.event, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_WOULD_BLOCK);
	CHECK(hx_os_coro_mutex_lock_until(state.mutex, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_condition_wait_until(state.condition, state.mutex,
		      hx_os_deadline_after_ms(1), NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_coro_mutex_unlock(state.mutex, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.holding, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &state.release, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKFUL, test_cancelled,
		      &state, NULL, &reader, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.entered, test_deadline(), NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_go(state.runtime, HX_OS_CORO_STACKFUL, test_holder,
		      &state, NULL, &writer, NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.holding, test_deadline(), NULL) ==
		HX_OS_OK);

	hx_os_coro_cancel(reader);
	CHECK(hx_os_coro_wait_until(reader, HX_OS_DEADLINE_NOW, NULL, NULL) ==
		HX_OS_TIMEOUT);

	hx_os_event_set(state.release);
	test_wait(reader);
	test_wait(writer);
	CHECK(hx_os_coro_spawn(state.runtime, HX_OS_CORO_STACKLESS,
		      test_failure, NULL, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_coro_runtime_wait_until(
		      state.runtime, test_deadline(), NULL) == HX_OS_OK);
	CHECK(hx_os_coro_errors_read(state.runtime, 1, &errors, NULL) ==
		HX_OS_OK);
	CHECK(errors.count == 1 && errors.first.native_code == 13 &&
		errors.first.cause_code == 7);
	CHECK(hx_os_coro_errors_read(state.runtime, 0, &errors, NULL) ==
			HX_OS_OK &&
		errors.count == 0);
	CHECK(hx_os_coro_spawn_prepared(state.runtime, HX_OS_CORO_STACKLESS,
		      NULL, NULL, test_failure, NULL, NULL) == HX_OS_INVALID);
	CHECK(hx_os_coro_errors_read(state.runtime, 1, &errors, NULL) ==
		HX_OS_OK);
	CHECK(errors.count == 1 && errors.first.status == HX_OS_INVALID);

	hx_os_coro_sync_release(state.mutex);
	hx_os_coro_sync_release(state.condition);
	hx_os_coro_sync_release(state.event);
	hx_os_coro_sync_release(state.semaphore);
	hx_os_coro_sync_release(state.rwlock);
	hx_os_coro_local_key_release(state.key);
	hx_os_event_destroy(state.entered);
	hx_os_event_destroy(state.holding);
	hx_os_event_destroy(state.release);
	deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(hx_os_coro_runtime_close_until(state.runtime, deadline, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_coro_runtime_destroy(&state.runtime, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	puts("coroutine sync, CLS and managed task errors passed");

	return 0;
}
