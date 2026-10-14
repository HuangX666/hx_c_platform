#include <hx_os.h>
#include <stdio.h>

typedef struct transfer {
	char text[32];
	int failed;
} transfer;

static void transfer_done(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	transfer *state = user;

	(void)request;
	if (completion->error.status != HX_OS_OK)
		state->failed = 1;
}

int main(void)
{
	hx_os_loop *loop = NULL;
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_request *read_request = NULL, *write_request = NULL;
	transfer state = {{0}, 0};

	hx_os_status status = hx_os_loop_create_local(NULL, NULL, &loop, NULL);
	if (status == HX_OS_OK)
		status = hx_os_pipe_pair_async(NULL, &reader, &writer, NULL);

	if (status == HX_OS_OK)
		status = hx_os_pipe_read_async(loop, reader, state.text,
			sizeof(state.text) - 1, hx_os_deadline_after_ms(2000),
			transfer_done, &state, &read_request, NULL);

	if (status == HX_OS_OK)
		status = hx_os_pipe_write_async(loop, writer, "hello pipe", 10,
			hx_os_deadline_after_ms(2000), transfer_done, &state,
			&write_request, NULL);

	if (status == HX_OS_OK)
		status = hx_os_loop_run_until(
			loop, hx_os_deadline_after_ms(2000), NULL);

	// This example explicitly waits for cancellation retirement at exit.
	(void)hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_INFINITE, NULL);
	(void)hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_INFINITE, NULL);
	if (loop) {
		(void)hx_os_loop_close_until(
			loop, HX_OS_DEADLINE_INFINITE, NULL);

		(void)hx_os_loop_destroy(&loop, NULL);
	}

	hx_os_request_release(read_request);
	hx_os_request_release(write_request);
	if (status != HX_OS_OK || state.failed)
		return 1;

	puts(state.text);

	return 0;
}
