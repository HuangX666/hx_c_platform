#include <hx_os.h>
#include <stdio.h>

typedef struct coordination_state {
	hx_os_cancel_token *cancel;
	hx_os_wait_group *group;
	hx_os_rwlock *lock;
	hx_os_thread *threads[2];
	hx_os_atomic failure;
	unsigned completed;
} coordination_state;

static void *coordination_worker(void *user)
{
	coordination_state *state = user;
	hx_os_status status = HX_OS_OK;
	unsigned i;

	for (i = 0; i < 50 && !hx_os_cancel_requested(state->cancel); ++i) {
		status = hx_os_rwlock_write_until(state->lock,
			hx_os_deadline_after_ms(1000), state->cancel, NULL);
		if (status != HX_OS_OK)
			break;

		++state->completed;
		status = hx_os_rwlock_write_unlock(state->lock, NULL);
		if (status != HX_OS_OK)
			break;

		status = hx_os_sleep_cancellable_until(
			hx_os_deadline_after_ms(2), state->cancel, NULL);
		if (status != HX_OS_OK)
			break;
	}
	if (status != HX_OS_OK && status != HX_OS_CANCELLED)
		hx_os_atomic_store(&state->failure, status, HX_OS_RELAXED);

	status = hx_os_wait_group_done(state->group, 1, NULL);
	if (status != HX_OS_OK)
		hx_os_atomic_store(&state->failure, status, HX_OS_RELAXED);

	return NULL;
}

int main(void)
{
	static coordination_state state;
	hx_os_status status;
	hx_os_deadline deadline;
	unsigned i;

	if (!(hx_os_capabilities() & HX_OS_CAP_THREAD_EX)) {
		puts("Thread coordination is disabled.");

		return 0;
	}

	status = hx_os_cancel_create(NULL, 0, &state.cancel, NULL);
	if (status == HX_OS_OK)
		status = hx_os_wait_group_create(NULL, 0, &state.group, NULL);
	if (status == HX_OS_OK)
		status = hx_os_rwlock_create(NULL, &state.lock, NULL);

	for (i = 0; status == HX_OS_OK && i < 2; ++i) {
		status = hx_os_wait_group_add(state.group, 1, NULL);
		if (status != HX_OS_OK)
			break;

		status = hx_os_thread_create(NULL, coordination_worker, &state,
			&state.threads[i], NULL);
		if (status != HX_OS_OK)
			(void)hx_os_wait_group_done(state.group, 1, NULL);
	}
	if (status == HX_OS_OK)
		status = hx_os_sleep_until(hx_os_deadline_after_ms(10), NULL);
	if (state.cancel) {
		hx_os_status requested =
			hx_os_cancel_request(state.cancel, NULL);

		if (status == HX_OS_OK)
			status = requested;
	}

	// Cancellation requests cooperation. Completion still needs a wait.
	deadline = hx_os_deadline_after_ms(2000);
	if (state.group) {
		hx_os_status waited = hx_os_wait_group_wait_until(
			state.group, deadline, NULL, NULL);

		if (waited != HX_OS_OK) {
			fprintf(stderr, "Completion pending: %s\n",
				hx_os_status_name(waited));

			// A service retains state and retries its shutdown
			// later. This command-line demo reports failure without
			// freeing resources that unfinished workers could still
			// access.
			return 1;
		}
	}
	for (i = 0; i < 2; ++i) {
		if (!state.threads[i])
			continue;

		hx_os_status joined = hx_os_thread_join_until(
			state.threads[i], deadline, NULL, NULL);
		if (joined != HX_OS_OK) {
			fprintf(stderr, "Thread exit pending: %s\n",
				hx_os_status_name(joined));

			return 1;
		}
		(void)hx_os_thread_destroy(&state.threads[i], NULL);
	}
	if (status == HX_OS_OK)
		status = (hx_os_status)hx_os_atomic_load(
			&state.failure, HX_OS_RELAXED);
	if (status == HX_OS_OK) {
		status = hx_os_rwlock_read_until(
			state.lock, HX_OS_DEADLINE_NOW, NULL, NULL);
		if (status == HX_OS_OK) {
			printf("Processed %u records, then stopped "
			       "cooperatively.\n",
				state.completed);
			status = hx_os_rwlock_read_unlock(state.lock, NULL);
		}
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "Coordination failed: %s\n",
			hx_os_status_name(status));

	{
		hx_os_status lock_closed =
			hx_os_rwlock_destroy(&state.lock, NULL);
		hx_os_status group_closed =
			hx_os_wait_group_destroy(&state.group, NULL);
		hx_os_status token_closed =
			hx_os_cancel_destroy(&state.cancel, NULL);

		if (status == HX_OS_OK &&
			(lock_closed != HX_OS_OK || group_closed != HX_OS_OK ||
				token_closed != HX_OS_OK))
			status = HX_OS_BUSY;
	}

	return status == HX_OS_OK ? 0 : 1;
}
