#include <hx_os.h>
#include <stdio.h>
#include <string.h>

typedef struct request_state {
	hx_os_coro_http *service;
	hx_os_coro_runtime *runtime;
	hx_os_coro *task;
	const char *url;
	hx_os_http_response response;
	int step;
} request_state;

static hx_os_status fetch(hx_os_coro *self, void *user, hx_os_error *error)
{
	request_state *state = user;
	hx_os_status status = HX_OS_OK;

	(void)self;
	HX_OS_CORO_BEGIN(state->step);
	HX_OS_CORO_AWAIT(state->step, status,
		hx_os_coro_http_get(state->service, state->url, NULL,
			&state->response, error));
	HX_OS_CORO_END(state->step);
	if (status == HX_OS_OK) {
		printf("HTTP %u\n", state->response.status_code);
		(void)fwrite(state->response.body.data, 1,
			state->response.body.size, stdout);
		putchar('\n');
	}
	return hx_os_coro_complete(status, error);
}

static hx_os_status finish(request_state *state, hx_os_deadline deadline)
{
	hx_os_status status;

	if (state->task) {
		hx_os_coro_cancel(state->task);
		status = hx_os_coro_wait_until(state->task, deadline, NULL,
			NULL);
		if (status != HX_OS_OK)
			return status;
		hx_os_coro_release(state->task);
		state->task = NULL;
	}
	if (state->service) {
		status = hx_os_coro_http_close_until(&state->service, deadline,
			NULL);
		if (status != HX_OS_OK)
			return status;
	}
	if (state->runtime) {
		status = hx_os_coro_runtime_close_until(state->runtime,
			deadline, NULL);
		if (status != HX_OS_OK)
			return status;
		status = hx_os_coro_runtime_destroy(&state->runtime, NULL);
		if (status != HX_OS_OK)
			return status;
	}
	hx_os_http_response_destroy(&state->response);
	return HX_OS_OK;
}

int main(int argc, char **argv)
{
	hx_os_error work = {0};
	hx_os_status status, closed;
	static request_state state;

	if ((argc != 2 && argc != 3) ||
		(argc == 3 && strcmp(argv[2], "--cancellable-dns"))) {
		fprintf(stderr, "Usage: %s URL [--cancellable-dns]\n", argv[0]);
		return 2;
	}
	state.url = argv[1];
	hx_os_http_response_init(&state.response, NULL);
	status = hx_os_coro_runtime_create(NULL, NULL, &state.runtime, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_http_create(
			state.runtime, 0, 0, 0, &state.service, NULL);
	if (status == HX_OS_OK && argc == 3)
		status = hx_os_coro_http_use_cancellable_dns(
			state.service, NULL, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_go(state.runtime, HX_OS_CORO_STACKLESS,
			fetch, &state, NULL, &state.task, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_wait_until(
			state.task, hx_os_deadline_after_ms(6000), &work, NULL);
	if (status != HX_OS_OK || work.status != HX_OS_OK) {
		fprintf(stderr, "HTTP coroutine: %s\n",
			hx_os_status_name(
				status != HX_OS_OK ? status : work.status));
	}

	closed = finish(&state, hx_os_deadline_after_ms(500));
	if (closed != HX_OS_OK) {
		fprintf(stderr, "Cleanup pending: %s\n",
			hx_os_status_name(closed));
		return 1;
	}
	return status == HX_OS_OK && work.status == HX_OS_OK ? 0 : 1;
}
