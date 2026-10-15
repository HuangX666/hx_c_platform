#include <cstdio>
#include <hx/GoKeywords.hpp>

static hx::Result<void> run(hx::coro::Runtime &runtime) noexcept
{
	auto scope = hx::coro::Scope::enter(runtime);
	if (!scope)
		return hx::Failure(scope.error());

	co_chan<int> answers(1);
	if (!answers)
		return hx::Failure(answers.error());

	go[answers]() noexcept
	{
		return answers << 6 * 7;
	};

	auto completed = runtime.waitUntil(hx_os_deadline_after_ms(2000));
	if (!completed)
		return completed;

	auto value = answers.receive(HX_OS_DEADLINE_NOW);
	if (!value)
		return hx::Failure(value.error());

	std::printf("Answer: %d\n", value.value());

	return hx::Result<void>();
}

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES)) {
		std::fputs("Stackful coroutines are unavailable.\n", stderr);

		return 1;
	}

	hx_os_coro_runtime *owner = NULL;

	auto created = hx::coro::Runtime::create(owner);
	if (!created) {
		std::fprintf(stderr, "Create runtime: %s\n",
			hx_os_status_name(created.status()));

		return 1;
	}

	auto &runtime = created.value();

	auto result = run(runtime);
	if (!result)
		std::fprintf(stderr, "Run task: %s\n",
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
