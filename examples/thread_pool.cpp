#include <hx/ThreadPool.hpp>
#include <cstdio>

int main()
{
	hx_os_thread_pool *owner = NULL;
	if (!(hx_os_capabilities() & HX_OS_CAP_THREAD_POOL)) {
		std::puts("Thread pools are disabled.");
		return 0;
	}
	try {
		auto pool = hx::ThreadPool::create(owner);
		auto answer =
			pool.submit([](int value) { return value * 2; }, 21);
		std::printf("Task result: %d\n", answer.get());
		/* This example has only a completed CPU task and no external
		 * waits. */
		hx_os_status closed =
			pool.closeUntil(hx_os_deadline_after_ms(2000));
		if (closed != HX_OS_OK) {
			std::fprintf(stderr, "Close pending: %s\n",
				hx_os_status_name(closed));
			/* A service must retain owner and retry from its
			 * shutdown state. This command-line example reports
			 * failure. */
			return 1;
		}
		return pool.destroy() == HX_OS_OK ? 0 : 1;
	} catch (const std::exception &error) {
		std::fprintf(stderr, "%s\n", error.what());
		if (owner) {
			hx::ThreadPool remaining(owner);
			if (remaining.closeUntil(hx_os_deadline_after_ms(2000),
				    HX_OS_POOL_CANCEL_PENDING) == HX_OS_OK)
				(void)remaining.destroy();
		}
		return 1;
	}
}
