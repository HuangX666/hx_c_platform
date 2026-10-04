#include "hx_os.h"
#include <stdio.h>

static void timer_completed(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	int *called = user;

	(void)request;
	*called = completion->error.status == HX_OS_OK;
}

int main(void)
{
	hx_os_network *network = NULL;
	hx_os_loop *loop = NULL;
	hx_os_request *request = NULL;
	hx_os_operation operation;
	hx_os_status status;
	int called = 0;

	if (!(hx_os_capabilities() & HX_OS_CAP_LOOP)) {
		puts("The event loop is disabled in this build.");

		return 0;
	}

	status = hx_os_network_create(NULL, &network, NULL);
	if (status == HX_OS_OK)
		status = hx_os_loop_create(network, NULL, &loop, NULL);

	hx_os_operation_init(&operation, HX_OS_OP_TIMER);
	operation.deadline = hx_os_deadline_after_ms(10);
	if (status == HX_OS_OK)
		status = hx_os_loop_submit(loop, &operation, timer_completed,
			&called, &request, NULL);

	if (status == HX_OS_OK)
		status = hx_os_loop_run_until(
			loop, hx_os_deadline_after_ms(1000), NULL);

	if (loop) {
		hx_os_status closed = hx_os_loop_close_until(
			loop, hx_os_deadline_after_ms(1000), NULL);
		if (closed != HX_OS_OK) {
			fprintf(stderr, "Close incomplete: %s\n",
				hx_os_status_name(closed));

			/* A long-lived host retains these handles and retries.
			 */
			return 2;
		}

		(void)hx_os_loop_destroy(&loop, NULL);
	}

	hx_os_request_release(request);
	(void)hx_os_network_destroy(&network, NULL);
	if (status == HX_OS_OK && called) {
		printf("Timer completed through %s.\n", hx_os_loop_backend());

		return 0;
	}

	fprintf(stderr, "Timer failed: %s\n", hx_os_status_name(status));

	return 1;
}
