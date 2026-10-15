#include <hx_os.h>
#include <stdio.h>

typedef struct request_state {
	hx_os_coro_http *service;
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

int main(int argc, char **argv)
{
	hx_os_coro_runtime *runtime = NULL;
	hx_os_coro *task = NULL;
	hx_os_error work = {0};
	hx_os_status status, closed;
	request_state state = {0};

	if (argc != 2) {
		fprintf(stderr, "Usage: %s URL\n", argv[0]);
		return 2;
	}
	state.url = argv[1];
	hx_os_http_response_init(&state.response, NULL);
	status = hx_os_coro_runtime_create(NULL, NULL, &runtime, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_http_create(
			runtime, 0, 0, 0, &state.service, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_go(runtime, HX_OS_CORO_STACKLESS, fetch,
			&state, NULL, &task, NULL);
	if (status == HX_OS_OK)
		status = hx_os_coro_wait_until(
			task, hx_os_deadline_after_ms(6000), &work, NULL);
	if (status != HX_OS_OK || work.status != HX_OS_OK) {
		fprintf(stderr, "HTTP coroutine: %s\n",
			hx_os_status_name(
				status != HX_OS_OK ? status : work.status));
		hx_os_coro_cancel(task);
	}

	closed = hx_os_coro_http_close_until(
		&state.service, hx_os_deadline_after_ms(2000), NULL);
	if (closed != HX_OS_OK) {
		fprintf(stderr, "HTTP close: %s; keep owner to retry\n",
			hx_os_status_name(closed));
		return 1;
	}
	if (runtime &&
		hx_os_coro_runtime_close_until(runtime,
			hx_os_deadline_after_ms(2000), NULL) != HX_OS_OK)
		return 1;
	hx_os_coro_release(task);
	(void)hx_os_coro_runtime_destroy(&runtime, NULL);
	hx_os_http_response_destroy(&state.response);
	return status == HX_OS_OK && work.status == HX_OS_OK ? 0 : 1;
}
