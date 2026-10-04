#include <hx/ThreadPool.hpp>
#include <hx_os_thread.h>
#include <memory>
#include <functional>
#include <cstring>
#include "cpp_check.hpp"

struct MoveOnly {
	std::unique_ptr<int> m_value_;

	explicit MoveOnly(int value) noexcept
	    : m_value_(new(std::nothrow) int(value))
	{
		CHECK(m_value_);
	}

	MoveOnly(MoveOnly &&) noexcept = default;
	MoveOnly(const MoveOnly &) = delete;

	int operator()(std::unique_ptr<int> value) noexcept
	{
		return *m_value_ + *value;
	}
};

struct CopyProbe {
	hx_os_atomic *m_copies_;

	explicit CopyProbe(hx_os_atomic *copies) noexcept
	    : m_copies_(copies)
	{
	}

	CopyProbe(const CopyProbe &other) noexcept
	    : m_copies_(other.m_copies_)
	{
		hx_os_atomic_fetch_add(m_copies_, 1, HX_OS_RELAXED);
	}

	int operator()() const noexcept
	{
		return 1;
	}
};

struct MemberWork {
	int m_value_;

	int add(int value) & noexcept
	{
		return m_value_ + value;
	}

	int operator()() & noexcept
	{
		return m_value_;
	}
};

template <size_t Alignment> struct alignas(Alignment) AlignedResult {
	hx_os_atomic *m_live_;
	unsigned char m_bytes_[Alignment - sizeof(hx_os_atomic *)];

	explicit AlignedResult(hx_os_atomic *live) noexcept
	    : m_live_(live)
	    , m_bytes_{}
	{
		CHECK(reinterpret_cast<uintptr_t>(this) % Alignment == 0);

		m_bytes_[0] = 42;
		hx_os_atomic_fetch_add(m_live_, 1, HX_OS_RELAXED);
	}

	AlignedResult(AlignedResult &&other) noexcept
	    : m_live_(other.m_live_)
	    , m_bytes_{}
	{
		CHECK(reinterpret_cast<uintptr_t>(this) % Alignment == 0);

		std::memcpy(m_bytes_, other.m_bytes_, sizeof(m_bytes_));
		other.m_live_ = NULL;
	}

	AlignedResult(const AlignedResult &) = delete;
	~AlignedResult() noexcept
	{
		if (m_live_)
			hx_os_atomic_fetch_add(m_live_, -1, HX_OS_RELAXED);
	}
};

template <size_t Alignment> static void TestAlignedResult(hx::ThreadPool &pool)
{
	hx_os_atomic live = HX_OS_ATOMIC_INIT(0);
	{
		auto task = Take(pool.submit([&live]() noexcept {
			return AlignedResult<Alignment>(&live);
		}));

		auto value = Take(task.get());
		CHECK(value.m_bytes_[0] == 42 && !task.valid());
		CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 1);
	}

	CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 0);

	{
		auto discarded = Take(pool.submit([&live]() noexcept {
			return AlignedResult<Alignment>(&live);
		}));
		Take(discarded.wait());
		CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 1);
	}

	CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 0);

	auto failed = Take(pool.submit(
		[]() noexcept -> hx::Result<AlignedResult<Alignment>> {
			return hx::Failure(HX_OS_ACCESS_DENIED);
		}));

	CHECK(failed.get().status() == HX_OS_ACCESS_DENIED && !failed.valid());
}

static void TestResults(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 256, context));
	TestAlignedResult<16>(pool);
	TestAlignedResult<32>(pool);
	TestAlignedResult<64>(pool);
	TestAlignedResult<128>(pool);

	auto small = Take(pool.submit([]() noexcept { return 'x'; }));
	CHECK(Take(small.get()) == 'x');

	MemberWork member = {40};

	auto memberResult = Take(pool.submit(&MemberWork::add, &member, 2));
	CHECK(Take(memberResult.get()) == 42);

	auto memberValue =
		Take(pool.submit(&MemberWork::m_value_, std::ref(member)));

	CHECK(&Take(memberValue.get()) == &member.m_value_);

	auto lvalue = Take(pool.submit(member));
	CHECK(Take(lvalue.get()) == 40);

	auto moved = Take(pool.submit(
		MoveOnly(2), std::unique_ptr<int>(new (std::nothrow) int(40))));
	CHECK(Take(moved.get()) == 42);

	auto pointer = Take(pool.submit([]() noexcept {
		return std::unique_ptr<int>(new (std::nothrow) int(7));
	}));

	auto value = Take(pointer.get());
	CHECK(value && *value == 7);

	int referred = 0;

	auto reference = Take(pool.submit(
		[](int &target) noexcept -> int & {
			target = 9;
			return target;
		},
		std::ref(referred)));

	CHECK(&Take(reference.get()) == &referred && referred == 9);

	auto failure = Take(pool.submit([]() noexcept -> hx::Result<int> {
		hx_os_error error = {HX_OS_IO_ERROR, HX_OS_ERROR_ERRNO, 5, 19};
		return hx::Failure(error);
	}));

	auto failed = failure.get();
	CHECK(!failed && failed.error().native_code == 5);
	CHECK(failed.error().cause_code == 19 && !failure.valid());
	CHECK(failure.get().status() == HX_OS_CLOSED);

	auto successful = Take(
		pool.submit([]() noexcept -> hx::Result<int> { return 17; }));
	CHECK(Take(successful.get()) == 17);

	auto empty = Take(pool.submit([]() noexcept {}));
	Take(empty.get());

	auto status = Take(pool.submit([]() noexcept -> hx::Result<void> {
		return hx::Failure(HX_OS_ACCESS_DENIED);
	}));
	CHECK(status.get().status() == HX_OS_ACCESS_DENIED);

	auto identity =
		Take(pool.submit([&pool]() noexcept { return pool.inside(); }));

	CHECK(Take(identity.get()));

	auto retained = Take(pool.submit([]() noexcept { return 123; }));
	Take(pool.closeUntil(TestJoinDeadline()));
	Take(pool.destroy());
	CHECK(!owner && hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(Take(retained.get()) == 123);
}

static void TestBoundaries(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_event *entered = NULL, *release = NULL;

	CHECK(hx_os_event_create(context, 1, 0, &entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &release, NULL) == HX_OS_OK);

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 1, context));

	auto first = Take(pool.submit([entered, release]() noexcept {
		hx_os_event_set(entered);
		CHECK(hx_os_event_wait_until(release, HX_OS_DEADLINE_INFINITE,
			      NULL) == HX_OS_OK);
		return 1;
	}));
	CHECK(hx_os_event_wait_until(entered, hx_os_deadline_after_ms(2000),
		      NULL) == HX_OS_OK);

	auto queued = Take(pool.submit([]() noexcept { return 2; }));
	hx_os_atomic copies = HX_OS_ATOMIC_INIT(0);

	CopyProbe probe(&copies);
	CHECK(pool.submit(probe).status() == HX_OS_LIMIT);
	CHECK(hx_os_atomic_load(&copies, HX_OS_RELAXED) == 0);
	CHECK(first.getUntil(HX_OS_DEADLINE_NOW).status() == HX_OS_TIMEOUT);
	CHECK(first.valid());
	CHECK(pool.closeUntil(
			  hx_os_deadline_after_ms(2), HX_OS_POOL_CANCEL_PENDING)
			.status() ==
		((hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN)
				? HX_OS_TIMEOUT
				: HX_OS_NOT_SUPPORTED));
	CHECK(owner && pool.destroy().status() == HX_OS_BUSY);

	hx_os_event_set(release);
	CHECK(Take(first.get()) == 1);
	CHECK(queued.get().status() == HX_OS_CANCELLED);

	Take(pool.shutdownUntil(TestJoinDeadline()));

	hx_os_event_destroy(entered);
	hx_os_event_destroy(release);
}

static void TestCancellationAndDeadlock(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 16, context));

	auto running = Take(
		pool.submitCancelable([](hx::CancellationToken token) noexcept {
			while (!token.stopRequested())
				hx_os_cpu_relax();
			return token.check();
		}));

	running.cancel();
	CHECK(running.get().status() == HX_OS_CANCELLED);

	auto parent = Take(pool.submit([&pool]() noexcept {
		auto child = Take(pool.submit([]() noexcept { return 1; }));
		return child.waitUntil(HX_OS_DEADLINE_INFINITE).status();
	}));
	CHECK(Take(parent.get()) == HX_OS_DEADLOCK);

	Take(pool.shutdownUntil(TestJoinDeadline()));
}

struct PreparationRace {
	hx_os_thread_pool **m_owner_;
	hx_os_event *m_entered_, *m_release_;
	hx_os_atomic m_status_;
	bool m_delayed_;
};

struct BlockingCopy {
	PreparationRace *m_race_;

	explicit BlockingCopy(PreparationRace *race) noexcept
	    : m_race_(race)
	{
	}

	BlockingCopy(const BlockingCopy &other) noexcept
	    : m_race_(other.m_race_)
	{
		hx_os_event_set(m_race_->m_entered_);
		CHECK(hx_os_event_wait_until(m_race_->m_release_,
			      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);
	}

	void operator()() const noexcept
	{
		CHECK(false);
	}
};

static void *prepare_submit(void *user)
{
	auto *race = static_cast<PreparationRace *>(user);

	hx::ThreadPool pool(*race->m_owner_);

	BlockingCopy callable(race);

	auto result = race->m_delayed_
		? pool.submitAfter(std::chrono::milliseconds(30), callable)
		: pool.submit(callable);

	hx_os_atomic_store(&race->m_status_, result.status(), HX_OS_RELEASE);

	return NULL;
}

static void TestPreparationRace(hx_os_context *context, bool delayed)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread *producer = NULL;
	PreparationRace race = {};

	race.m_owner_ = &owner;
	race.m_delayed_ = delayed;

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 16, context));
	CHECK(hx_os_event_create(context, 1, 0, &race.m_entered_, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &race.m_release_, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(context, prepare_submit, &race, &producer,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(race.m_entered_,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);

	auto closed = pool.closeUntil(hx_os_deadline_after_ms(20));
	if (hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN)
		CHECK(closed.status() == HX_OS_BUSY ||
			closed.status() == HX_OS_TIMEOUT);
	else
		CHECK(closed.status() == HX_OS_NOT_SUPPORTED);

	CHECK(pool.destroy().status() == HX_OS_BUSY);

	hx_os_event_set(race.m_release_);
	CHECK(hx_os_thread_join_until(
		      producer, TestJoinDeadline(), NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&producer, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&race.m_status_, HX_OS_ACQUIRE) ==
		HX_OS_CLOSED);

	Take(pool.shutdownUntil(TestJoinDeadline()));

	hx_os_event_destroy(race.m_entered_);
	hx_os_event_destroy(race.m_release_);
}

struct CancelWait {
	hx::Task<int> m_task_;
	hx_os_atomic m_result_;
};

static void *wait_result(void *user)
{
	auto *wait = static_cast<CancelWait *>(user);

	auto result = wait->m_task_.get();
	hx_os_atomic_store(&wait->m_result_,
		result ? result.value() : result.status(), HX_OS_RELEASE);

	return NULL;
}

static void TestIndependentCancellation(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread *waiter = NULL;
	hx_os_event *entered = NULL;

	CHECK(hx_os_event_create(context, 1, 0, &entered, NULL) == HX_OS_OK);

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 16, context));

	CancelWait wait = {};

	wait.m_task_ = Take(pool.submitCancelable(
		[entered](hx::CancellationToken token) noexcept {
			hx_os_event_set(entered);
			while (!token.stopRequested())
				hx_os_cpu_relax();
			return 42;
		}));

	{
		auto source = wait.m_task_.cancellationSource();
		CHECK(source.valid());
		CHECK(hx_os_event_wait_until(entered,
			      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
		CHECK(hx_os_thread_create(context, wait_result, &wait, &waiter,
			      NULL) == HX_OS_OK);

		source.requestStop();
		CHECK(hx_os_thread_join_until(waiter, TestJoinDeadline(), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&waiter, NULL) == HX_OS_OK);
		CHECK(hx_os_atomic_load(&wait.m_result_, HX_OS_ACQUIRE) == 42);

		Take(pool.shutdownUntil(TestJoinDeadline()));
		source.requestStop();
		CHECK(source.stopRequested());
	}

	hx_os_event_destroy(entered);
}

struct AllocationCounter {
	hx_os_atomic m_attempts_, m_fail_, m_live_;
};

static void *count_allocate(size_t bytes, void *user)
{
	auto *counter = static_cast<AllocationCounter *>(user);

	int64_t attempt = hx_os_atomic_fetch_add(
				  &counter->m_attempts_, 1, HX_OS_RELAXED) +
		1;
	if (attempt == hx_os_atomic_load(&counter->m_fail_, HX_OS_RELAXED))
		return NULL;

	void *memory = std::malloc(bytes);
	if (memory)
		hx_os_atomic_fetch_add(&counter->m_live_, 1, HX_OS_RELAXED);

	return memory;
}

static void count_free(void *memory, void *user)
{
	auto *counter = static_cast<AllocationCounter *>(user);

	hx_os_atomic_fetch_add(&counter->m_live_, -1, HX_OS_RELAXED);
	std::free(memory);
}

static void TestAllocationFailure(bool delayed)
{
	AllocationCounter counter = {};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_thread_pool *owner = NULL;

	hx_os_context_options_init(&options);
	options.allocator = {count_allocate, count_free, &counter};
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 16, context));

	const int64_t baseline =
		hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED);
	bool succeeded = false;

	for (int64_t failure = 1; failure < 16; ++failure) {
		hx_os_atomic_store(&counter.m_attempts_, 0, HX_OS_RELAXED);
		hx_os_atomic_store(&counter.m_fail_, failure, HX_OS_RELAXED);

		auto result = delayed
			? pool.submitAfter(std::chrono::milliseconds(1),
				  []() noexcept { return 42; })
			: pool.submit([]() noexcept { return 42; });

		if (result) {
			CHECK(Take(result.value().get()) == 42);

			succeeded = true;
			break;
		}

		CHECK(result.status() == HX_OS_NO_MEMORY);
		CHECK(hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED) ==
			baseline);
	}

	hx_os_atomic_store(&counter.m_fail_, 0, HX_OS_RELAXED);
	CHECK(succeeded);

	Take(pool.shutdownUntil(TestJoinDeadline()));
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED) == 0);
}

static void TestDelayed(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;

	auto pool = Take(hx::ThreadPool::withThreads(owner, 1, 16, context));
	if (!(hx_os_capabilities() & HX_OS_CAP_DELAYED_TASKS)) {
		CHECK(pool.submitAfter(std::chrono::milliseconds(1),
				  []() noexcept {})
				.status() == HX_OS_NOT_SUPPORTED);
	}
	else {
		auto task = Take(pool.submitAfter(std::chrono::milliseconds(1),
			[]() noexcept { return 42; }));
		CHECK(Take(task.get()) == 42);

		auto cancelled =
			Take(pool.submitAfter(std::chrono::milliseconds(60000),
				[]() noexcept { return 1; }));

		cancelled.cancel();
		CHECK(cancelled.getUntil(hx_os_deadline_after_ms(2000))
				.status() == HX_OS_CANCELLED);
	}

	Take(pool.shutdownUntil(TestJoinDeadline()));
}

int main()
{
	hx_os_context *context = NULL;

	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_THREAD_POOL) {
		TestResults(context);
		TestBoundaries(context);
		TestCancellationAndDeadlock(context);
		TestIndependentCancellation(context);
		TestPreparationRace(context, false);
		TestAllocationFailure(false);
		TestDelayed(context);
		if (hx_os_capabilities() & HX_OS_CAP_DELAYED_TASKS) {
			TestPreparationRace(context, true);
			TestAllocationFailure(true);
		}
	}
	else {
		hx_os_thread_pool *owner = NULL;

		CHECK(hx::ThreadPool::create(owner, context).status() ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!owner);
	}

	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	return 0;
}
