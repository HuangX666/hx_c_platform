#include <hx_os.h>
#include <stdio.h>

typedef struct counter_state {
	int step;
	int value;
} counter_state;

// C99 keeps values across suspension in an explicit user-owned state.
static hx_os_status count(hx_os_coro *self, void *user, hx_os_error *error)
{
	counter_state *state = user;
	hx_os_status status;

	(void)self;
	HX_OS_CORO_BEGIN(state->step);

	for (state->value = 1; state->value <= 3; ++state->value) {
		HX_OS_CORO_AWAIT(state->step, status, hx_os_coro_yield(error));
		if (status != HX_OS_OK)
			return hx_os_coro_complete(status, error);

		printf("Count: %d\n", state->value);
	}

	HX_OS_CORO_END(state->step);

	return HX_OS_OK;
}

int main(void)
{
	hx_os_coro_runtime *runtime = NULL;
	hx_os_coro *task = NULL;
	hx_os_error result = {HX_OS_OK, 0, 0, 0};
	hx_os_status status, closed;
	counter_state state = {0, 0};
	hx_os_deadline deadline;

	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINES)) {
		fputs("Coroutines are unavailable.\n", stderr);

		return 1;
	}

	status = hx_os_coro_runtime_create(NULL, NULL, &runtime, NULL);
	if (status != HX_OS_OK) {
		fprintf(stderr, "Create runtime: %s\n",
			hx_os_status_name(status));

		return 1;
	}

	status = hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS, count, &state,
		NULL, &task, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_wait_until(
			task, hx_os_deadline_after_ms(2000), &result, NULL);
	if (status != HX_OS_OK || result.status != HX_OS_OK)
		fprintf(stderr, "Task: %s\n", hx_os_status_name(
			status != HX_OS_OK ? status : result.status));

	hx_os_coro_release(task);

	// The host chooses its shutdown budget. If timed join is unavailable,
	// this finite demo explicitly joins its cooperative workers.
	deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? hx_os_deadline_after_ms(2000)
		: HX_OS_DEADLINE_INFINITE;
	closed = hx_os_coro_runtime_close_until(runtime, deadline, NULL);
	if (closed != HX_OS_OK) {
		fprintf(stderr, "Close: %s; keep owner to retry\n",
			hx_os_status_name(closed));

		return 1;
	}

	(void)hx_os_coro_runtime_destroy(&runtime, NULL);

	return status == HX_OS_OK && result.status == HX_OS_OK ? 0 : 1;
}
