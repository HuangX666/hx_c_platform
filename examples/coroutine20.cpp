#include <hx/Coroutines20.hpp>
#include <cstdio>

static hx::coro::Routine<int> answer()
{
	auto slept = co_await hx::coro::async::sleepFor(
		std::chrono::milliseconds(10));
	if (!slept)
		co_return hx::Result<int>(hx::Failure(slept.error()));

	co_return 6 * 7;
}

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINES)) {
		std::fputs("Coroutines are unavailable.\n", stderr);

		return 1;
	}

	hx_os_coro_runtime *owner = NULL;

	auto created = hx::coro::Runtime::create(owner);
	if (!created) {
		std::fprintf(stderr, "Create runtime: %s\n",
			hx_os_status_name(created.status()));
		auto closed = hx::coro::Runtime(owner).shutdownUntil(
			hx_os_deadline_after_ms(2000));
		if (!closed)
			std::fputs("Cleanup pending; keep owner to retry.\n",
				stderr);

		return 1;
	}

	auto &runtime = created.value();

	auto task = runtime.go(answer());

	hx::Result<int> result{hx::Failure(HX_OS_INVALID)};
	if (task)
		result = task.value().getUntil(hx_os_deadline_after_ms(2000));
	else
		result = hx::Failure(task.error());

	if (result)
		std::printf("Answer: %d\n", result.value());
	else
		std::fprintf(stderr, "Task: %s\n",
			hx_os_status_name(result.status()));

	auto deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? hx_os_deadline_after_ms(2000)
		: HX_OS_DEADLINE_INFINITE;

	auto closed = runtime.shutdownUntil(deadline);
	if (!closed) {
		std::fprintf(stderr, "Close: %s; keep owner to retry\n",
			hx_os_status_name(closed.status()));

		return 1;
	}

	return result ? 0 : 1;
}
