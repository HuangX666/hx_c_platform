#if defined(HX_TEST_CPP20)
#include <hx/Coroutines20.hpp>
#else
#include <hx/Coroutines.hpp>
#endif
#include <cstdio>
#include <cstdlib>
#include <hx_os_thread.h>

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

#if defined(HX_TEST_CPP20)
static hx::coro::Routine<int> test_produce(hx::coro::Channel<int> channel)
{
	int total = 0;

	for (int i = 1; i <= 100; ++i) {
		auto sent = co_await hx::coro::async::send(channel, i);
		if (!sent)
			co_return hx::Result<int>(hx::Failure(sent.error()));

		total += i;
		CHECK(co_await hx::coro::async::yield());
	}

	CHECK(channel.close());

	co_return total;
}

static hx::coro::Routine<int> test_consume(hx::coro::Channel<int> channel)
{
	int total = 0;

	for (;;) {
		auto received = co_await hx::coro::async::receive(channel);
		if (received.status() == HX_OS_EOF)
			break;

		CHECK(received);

		total += received.value();
	}

	co_return total;
}

static hx::coro::Routine<> test_cancel(int *destroyed, hx_os_event *entered)
{
	struct Guard {
		int *count;

		~Guard()
		{
			++*count;
		}
	} guard{destroyed};
	hx_os_event_set(entered);

	auto slept =
		co_await hx::coro::async::sleepUntil(HX_OS_DEADLINE_INFINITE);

	co_return std::move(slept);
}

static hx::coro::Routine<int> test_stream()
{
	auto addresses = co_await hx::coro::async::resolve("localhost", 80);
	CHECK(addresses && addresses.value().size());

	hx_os_address bound = {};

	CHECK(hx_os_address_parse("127.0.0.1", 0, &bound, NULL) == HX_OS_OK);

	auto listener = co_await hx::coro::async::listen(bound, &bound);
	CHECK(listener && bound.port);

	hx::coro::Stream reader, writer;

	CHECK(co_await hx::coro::async::pipePair(reader, writer));

	char data[4] = {'t', 'e', 's', 't'}, output[4] = {};

	auto sent = co_await hx::coro::async::write(writer, data, sizeof(data));
	CHECK(sent && sent.value() == 4);

	auto received =
		co_await hx::coro::async::read(reader, output, sizeof(output));

	CHECK(received && received.value() == 4);
	CHECK(output[0] == 't' && output[3] == 't');

	writer = hx::coro::Stream();

	auto eof =
		co_await hx::coro::async::read(reader, output, sizeof(output));

	CHECK(eof.status() == HX_OS_EOF && eof.hasValue() && !eof.value());

	co_return 4;
}

static hx::coro::Routine<int> test_select(hx::coro::Channel<int> channel)
{
	int value = 0;
	hx_os_coro_case entry = {
		channel.nativeHandle(), &value, HX_OS_CORO_RECEIVE};
	auto selected =
		co_await hx::coro::async::select(&entry, 1, HX_OS_DEADLINE_NOW);

	CHECK(selected.status() == HX_OS_EOF && selected.hasValue() &&
		selected.value() == 0);

	co_return 0;
}
#endif

int main()
{
	if (!(hx_os_capabilities() & HX_OS_CAP_COROUTINES))
		return 0;

	hx_os_coro_runtime *owner = NULL;
	hx_os_coro_options options = HX_OS_CORO_OPTIONS_INIT;

	options.workers = 1;

	auto created = hx::coro::Runtime::create(owner, NULL, &options);
	CHECK(created);

	auto &runtime = created.value();
	if (hx_os_capabilities() & HX_OS_CAP_STACKFUL_COROUTINES) {
		auto value = runtime.go(
			[](int input) noexcept -> hx::Result<int> {
				auto waited = hx::coro::sleepFor(
					std::chrono::milliseconds(1));
				if (!waited)
					return hx::Failure(waited.error());
				return input * 2;
			},
			21);

		CHECK(value);

		auto result = value.value().getUntil(test_deadline());
		CHECK(result && result.value() == 42);
	}
#if defined(HX_TEST_CPP20)
	auto channel = hx::coro::Channel<int>::create(runtime);
	CHECK(channel);

	auto consumer = runtime.go(test_consume(channel.value()));

	auto producer = runtime.go(test_produce(channel.value()));
	CHECK(consumer && producer);

	auto sent = producer.value().getUntil(test_deadline());

	auto received = consumer.value().getUntil(test_deadline());
	CHECK(sent && received && sent.value() == 5050 &&
		received.value() == 5050);

	int destroyed = 0;

	auto selected = runtime.go(test_select(channel.value()));
	CHECK(selected && selected.value().getUntil(test_deadline()));
	if ((hx_os_capabilities() & HX_OS_CAP_COROUTINE_IO) &&
		(hx_os_capabilities() & HX_OS_CAP_PIPE)) {
		auto transfer = runtime.go(test_stream());
		CHECK(transfer);

		auto transferred = transfer.value().getUntil(test_deadline());
		CHECK(transferred && transferred.value() == 4);
	}

	hx_os_event *entered = NULL;

	CHECK(hx_os_event_create(NULL, 1, 0, &entered, NULL) == HX_OS_OK);

	auto cancelled = runtime.go(test_cancel(&destroyed, entered));
	CHECK(cancelled);
	CHECK(hx_os_event_wait_until(entered, test_deadline(), NULL) ==
		HX_OS_OK);
	/* Drain timeout proves the frame can stay suspended without a worker.
	 */
	CHECK(runtime.drainUntil(hx_os_deadline_after_ms(10)).status() ==
		HX_OS_TIMEOUT);

	cancelled.value().cancel();
	CHECK(cancelled.value().getUntil(test_deadline()).status() ==
		HX_OS_CANCELLED);
	CHECK(destroyed == 1);

	hx_os_event_destroy(entered);
#endif
	auto deadline = hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? test_deadline()
		: HX_OS_DEADLINE_INFINITE;
	CHECK(runtime.shutdownUntil(deadline));
	CHECK(!owner);

	std::puts("C++ coroutine facade passed");

	return 0;
}
