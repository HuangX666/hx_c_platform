#include <hx_os.h>
#include <stdio.h>
#include <inttypes.h>
static hx_os_status HX_OS_CALL work(hx_os_task *task, void *user)
{
	(void)task;
	(void)user;
	return HX_OS_OK;
}
int main(void)
{
	hx_os_thread_pool_options options;
	hx_os_thread_pool *pool = NULL;
	hx_os_task *tasks[128];
	uint64_t start;
	size_t batch, i, sample;
	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	options.queue_capacity = 256;
	if (hx_os_thread_pool_create(NULL, &options, &pool, NULL) != HX_OS_OK)
		return 1;
	for (sample = 0; sample < 6; ++sample) {
		start = hx_os_monotonic_ns();
		for (batch = 0; batch < 128; ++batch) {
			for (i = 0; i < 128; ++i)
				if (hx_os_thread_pool_submit(pool, work, NULL,
					    NULL, &tasks[i], NULL) != HX_OS_OK)
					return 1;
			if (hx_os_task_wait_until(tasks[127],
				    HX_OS_DEADLINE_INFINITE, NULL,
				    NULL) != HX_OS_OK)
				return 1;
			for (i = 0; i < 128; ++i)
				hx_os_task_release(tasks[i]);
		}
		printf("sample=%zu tasks=16384 elapsed_ns=%" PRIu64 "\n",
			sample, hx_os_monotonic_ns() - start);
	}
	if (hx_os_thread_pool_close_until(pool, HX_OS_POOL_DRAIN,
		    HX_OS_DEADLINE_INFINITE, NULL) != HX_OS_OK)
		return 1;
	return hx_os_thread_pool_destroy(&pool, NULL) == HX_OS_OK ? 0 : 1;
}
