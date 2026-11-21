#include <hx_os.h>
#include <stdio.h>

static void tick(hx_os_timer *timer, const hx_os_error *error, void *user)
{
	unsigned *count = user;

	if (error->status != HX_OS_OK)
		return;

	printf("tick %u\n", ++*count);
	if (*count == 3)
		hx_os_timer_stop(timer);
}

int main(void)
{
	static hx_os_loop *loop;
	static hx_os_timer *timer;
	hx_os_status status;
	static unsigned count;
	hx_os_deadline deadline;

	if (hx_os_loop_create_local(NULL, NULL, &loop, NULL) != HX_OS_OK)
		return 1;

	status = hx_os_timer_create(
		loop, UINT64_C(10000000), tick, &count, &timer, NULL);
	if (status == HX_OS_OK)
		status = hx_os_loop_run_until(
			loop, hx_os_deadline_after_ms(2000), NULL);

	deadline = hx_os_deadline_after_ms(500);
	if (timer &&
		hx_os_timer_close_until(&timer, deadline, NULL) != HX_OS_OK)
		return 1;
	if (hx_os_loop_close_until(loop, deadline, NULL) != HX_OS_OK)
		return 1;
	if (hx_os_loop_destroy(&loop, NULL) != HX_OS_OK)
		return 1;

	return status == HX_OS_OK && count == 3 ? 0 : 1;
}
