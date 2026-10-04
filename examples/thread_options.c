#include <hx_os_thread.h>
#include <stdio.h>

static void *work(void *user)
{
	(void)hx_os_thread_set_current_name("hx-worker", NULL);

	return user;
}

int main(void)
{
	hx_os_thread_options options = HX_OS_THREAD_OPTIONS_INIT;
	hx_os_thread *thread = NULL;
	hx_os_status status;

	options.stack_size = 2 * 1024 * 1024;

	status = hx_os_thread_create_with_options(
		NULL, &options, work, NULL, &thread, NULL);
	if (status != HX_OS_OK)
		return 1;

	status = hx_os_thread_join_until(
		thread, hx_os_deadline_after_ms(2000), NULL, NULL);
	if (status == HX_OS_TIMEOUT) {
		/* The local worker is finite; keep ownership and explicitly
		 * retry. */
		status = hx_os_thread_join_until(
			thread, HX_OS_DEADLINE_INFINITE, NULL, NULL);
	}

	if (status != HX_OS_OK)
		return 1;

	status = hx_os_thread_destroy(&thread, NULL);
	puts(hx_os_status_name(status));

	return status != HX_OS_OK;
}
