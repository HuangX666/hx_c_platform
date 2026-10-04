#if defined(HX_TEST_CPP20)
#include <hx/Coroutines20.hpp>
#else
#include <hx/CoroutineSync.hpp>
#endif
#include <hx/Values.hpp>
#include <cstdio>
#include <cstdlib>
#include <hx/GoKeywords.hpp>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			std::fprintf(                                          \
				stderr, "line %d: %s\n", __LINE__, #value);    \
			std::exit(1);                                          \
		}                                                              \
	} while (0)

static hx_os_deadline test_deadline()
{
	return hx_os_deadline_after_ms(5000);
}

struct alignas(64) MoveValue {
	const int *self;
	int value;

	explicit MoveValue(int input) noexcept
	    : self(&value)
	    , value(input)
	{
	}

	MoveValue(MoveValue &&other) noexcept
	    : self(&value)
	    , value(other.value)
	{
	}

	MoveValue(const MoveValue &) = delete;

	MoveValue &operator=(const MoveValue &) = delete;
};

#if defined(HX_TEST_CPP20)
static hx::coro::Routine<> test_async(hx::coro::Mutex mutex,
	hx::coro::Event entered, hx::coro::Event release,
	hx::coro::Channel<hx::String> channel)
{
	auto guard = co_await hx::coro::async::acquire(mutex, test_deadline());
	CHECK(guard);
	CHECK(entered.set());
	CHECK(co_await hx::coro::async::wait(release, test_deadline()));

	auto message = hx::String::copy("frame value");
	CHECK(message);
	CHECK(co_await hx::coro::async::send(
		channel, std::move(message.value()), test_deadline()));

	co_return hx::Result<void>();
}
#endif

static void test_runtime(hx_os_context *context)
{
	hx_os_coro_runtime *owner = NULL;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;

	options.max_io_handles = 0;

	auto created = hx::coro::Runtime::create(owner, context, &options);
	CHECK(created);

	auto &runtime = created.value();

	auto scope = hx::coro::Scope::enter(runtime);
	CHECK(scope);

	co_chan<int> numbers;

	CHECK(numbers);

	go[numbers]() noexcept -> hx::Result<void>
	{
		return numbers << 42;
	};

	auto answer = numbers.receive(test_deadline());
	CHECK(answer && answer.value() == 42);
	CHECK(runtime.waitUntil(test_deadline()));
	CHECK(numbers.receive(HX_OS_DEADLINE_NOW).status() ==
		HX_OS_WOULD_BLOCK);

	co_chan<hx::String> strings(1);
	CHECK(strings);

	go[strings, context]() noexcept -> hx::Result<void>
	{
		auto value = hx::String::copy("中文🙂", context);
		if (!value)
			return hx::Failure(value.error());

		auto sent = strings << std::move(value.value());
		if (!sent)
			return sent;

		return strings.close();
	};

	auto text = strings.receive(test_deadline());
	CHECK(text && text.value() == "中文🙂");
	CHECK(strings.receive(test_deadline()).status() == HX_OS_EOF);
	CHECK(runtime.waitUntil(test_deadline()));

	auto move_channel = hx::coro::Channel<MoveValue>::create(runtime, 1);
	CHECK(move_channel);
	CHECK(move_channel.value().send(MoveValue(7), test_deadline()));

	auto moved = move_channel.value().receive(test_deadline());
	CHECK(moved && moved.value().value == 7 &&
		moved.value().self == &moved.value().value);

	auto event = hx::coro::Event::create(runtime);
	CHECK(event);

	{
		hx::coro::Group group(runtime, 2);

		auto blocker = event.value();
		CHECK(group.spawn([blocker]() noexcept {
			return blocker.waitUntil(test_deadline());
		}));
		CHECK(group.waitUntil(HX_OS_DEADLINE_NOW).status() ==
			HX_OS_TIMEOUT);
		CHECK(blocker.set());
		CHECK(group.waitUntil(test_deadline()));
	}

	auto local = hx::coro::Local<int>::create(context);
	CHECK(local);

	auto key = local.value();
	go[key]() noexcept -> hx::Result<void>
	{
		CHECK(!key.get());
		CHECK(key.set(17));
		CHECK(co_sleep(1));
		CHECK(key.get() && *key.get() == 17);

		return hx::Result<void>();
	};

	CHECK(runtime.waitUntil(test_deadline()));

	go[]() noexcept -> hx::Result<void>
	{
		return hx::Failure(HX_OS_IO_ERROR);
	};

	CHECK(runtime.waitUntil(test_deadline()).status() == HX_OS_IO_ERROR);

	auto errors = runtime.errors(true);
	CHECK(errors && errors.value().count == 1 &&
		errors.value().first.status == HX_OS_IO_ERROR);
	CHECK(runtime.waitUntil(test_deadline()));

	auto typed = runtime.submit([]() noexcept { return 21 * 2; });
	CHECK(typed && typed.value().getUntil(test_deadline()).value() == 42);
#if defined(HX_TEST_CPP20)
	auto mutex = hx::coro::Mutex::create(runtime);

	auto entered = hx::coro::Event::create(runtime);

	auto release = hx::coro::Event::create(runtime);

	auto values = hx::coro::Channel<hx::String>::create(runtime, 1);
	CHECK(mutex && entered && release && values);

	hx::coro::Group group(runtime);
	CHECK(group.spawn(test_async(mutex.value(), entered.value(),
		release.value(), values.value())));
	CHECK(entered.value().waitUntil(test_deadline()));
	CHECK(mutex.value().lockUntil(HX_OS_DEADLINE_NOW).status() ==
		HX_OS_WOULD_BLOCK);
	CHECK(release.value().set());

	auto completed = group.waitUntil(test_deadline());
	if (!completed)
		std::fprintf(stderr, "frame status=%d domain=%d native=%lld\n",
			int(completed.status()), int(completed.error().domain),
			static_cast<long long>(completed.error().native_code));

	CHECK(completed);

	auto frame_value = values.value().receive(test_deadline());
	CHECK(frame_value && frame_value.value() == "frame value");
	CHECK(mutex.value().acquireUntil(test_deadline()));
#endif
	auto nested = hx::coro::Scope::enter(runtime);
	CHECK(nested);
	CHECK(scope.value().close().status() == HX_OS_INVALID);
	CHECK(nested.value().close());
	CHECK(scope.value().close());
	CHECK(!hx_os_coro_current_runtime());

	auto deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(runtime.shutdownUntil(deadline));
	CHECK(!owner);
}

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES))
		return 0;

	hx_os_context *context = NULL;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);

	test_runtime(context);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	std::puts(
		"libgo-style syntax, owned values and structured waits passed");

	return 0;
}
