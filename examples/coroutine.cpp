#include <hx/Coroutines.hpp>
#include <cstdio>

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES))
		return 0;
	hx_os_coro_runtime *owner = NULL;
	auto created = hx::coro::Runtime::create(owner);
	if (!created)
		return 1;
	auto &runtime = created.value();
	auto task = runtime.go([]() noexcept -> hx::Result<int> {
		auto slept = hx::coro::sleepFor(std::chrono::milliseconds(10));
		if (!slept)
			return hx::Failure(slept.error());
		return 6 * 7;
	});
	hx::Result<int> result{hx::Failure(HX_OS_INVALID)};
	if (task)
		result = task.value().getUntil(hx_os_deadline_after_ms(2000));
	if (result)
		std::printf("Answer: %d\n", result.value());
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
