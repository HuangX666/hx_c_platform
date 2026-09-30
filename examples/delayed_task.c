#include <hx_os.h>
#include <stdio.h>
static hx_os_status HX_OS_CALL work(hx_os_task *task, void *user)
{
	(void)task;
	(void)user;
	puts("The delayed task became eligible and ran on the existing pool.");
	return HX_OS_OK;
}
int main(void)
{
	hx_os_thread_pool *pool = NULL;
	hx_os_task *task = NULL;
	hx_os_status status, result = HX_OS_OK;
	hx_os_error error;
	status = hx_os_thread_pool_create(NULL, NULL, &pool, &error);
	if (status == HX_OS_OK)
		status = hx_os_thread_pool_submit_delayed(pool,
			hx_os_deadline_after_ms(30), work, NULL, NULL, &task,
			&error);
	if (status == HX_OS_OK)
		status = hx_os_task_wait_until(
			task, hx_os_deadline_after_ms(2000), &result, &error);
	if (pool) {
		/* This example's callback is bounded. The caller explicitly
		 * chooses to finish closing after requesting pending
		 * cancellation.
		 */
		hx_os_status closed = hx_os_thread_pool_close_until(pool,
			HX_OS_POOL_CANCEL_PENDING, HX_OS_DEADLINE_INFINITE,
			&error);
		if (closed == HX_OS_OK)
			closed = hx_os_thread_pool_destroy(&pool, &error);
		if (status == HX_OS_OK)
			status = closed;
	}
	hx_os_task_release(task);
	return status == HX_OS_OK && result == HX_OS_OK ? 0 : 1;
}
