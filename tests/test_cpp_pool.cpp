#include <hx/ThreadPool.hpp>
#include <hx_os_thread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			std::fprintf(stderr, "check failed at %d: %s\n",       \
				__LINE__, #condition);                         \
			std::abort();                                          \
		}                                                              \
	} while (0)

struct MoveOnly {
	std::unique_ptr<int> m_value_;
	explicit MoveOnly(int value)
	    : m_value_(new int(value))
	{
	}
	MoveOnly(MoveOnly &&) noexcept = default;
	MoveOnly(const MoveOnly &) = delete;
	int operator()(std::unique_ptr<int> value)
	{
		return *m_value_ + *value;
	}
};

struct CopyFailure {
	CopyFailure() = default;
	CopyFailure(const CopyFailure &)
	{
		throw std::runtime_error("copy failed");
	}
	int operator()() const
	{
		return 0;
	}
};

struct CopyProbe {
	hx_os_atomic *m_copies_;
	explicit CopyProbe(hx_os_atomic *copies)
	    : m_copies_(copies)
	{
	}
	CopyProbe(const CopyProbe &other)
	    : m_copies_(other.m_copies_)
	{
		hx_os_atomic_fetch_add(m_copies_, 1, HX_OS_RELAXED);
	}
	int operator()() const
	{
		return 1;
	}
};

struct MemberWork {
	int m_value_;
	int add(int value) &
	{
		return m_value_ + value;
	}
	int operator()() &
	{
		return m_value_;
	}
};

template <std::size_t Alignment> struct alignas(Alignment) AlignedResult {
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
	~AlignedResult()
	{
		if (m_live_)
			hx_os_atomic_fetch_add(m_live_, -1, HX_OS_RELAXED);
	}
};

template <std::size_t Alignment>
static void TestAlignedResult(hx::ThreadPool &pool)
{
	hx_os_atomic live = HX_OS_ATOMIC_INIT(0);
	{
		auto task = pool.submit(
			[&live] { return AlignedResult<Alignment>(&live); });
		auto value = task.get();
		CHECK(value.m_bytes_[0] == 42 && !task.valid());
		CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 1);
	}
	CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 0);
	{
		auto discarded = pool.submit(
			[&live] { return AlignedResult<Alignment>(&live); });
		discarded.wait();
		CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 1);
	}
	CHECK(hx_os_atomic_load(&live, HX_OS_ACQUIRE) == 0);
	auto failed = pool.submit([]() -> AlignedResult<Alignment> {
		throw std::runtime_error("aligned work failed");
	});
	bool caught = false;
	try {
		(void)failed.get();
	} catch (const std::runtime_error &) {
		caught = true;
	}
	CHECK(caught && !failed.valid());
}

static void TestResults(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	auto pool = hx::ThreadPool::create(owner, context);
	TestAlignedResult<16>(pool);
	TestAlignedResult<32>(pool);
	TestAlignedResult<64>(pool);
	TestAlignedResult<128>(pool);
	auto small = pool.submit([] { return 'x'; });
	CHECK(small.get() == 'x');
	MemberWork member = {40};
	auto memberResult = pool.submit(&MemberWork::add, &member, 2);
	CHECK(memberResult.get() == 42);
	auto memberValue = pool.submit(&MemberWork::m_value_, std::ref(member));
	CHECK(&memberValue.get() == &member.m_value_);
	auto lvalueCallable = pool.submit(member);
	CHECK(lvalueCallable.get() == 40);
	auto integer = pool.submit([](int a, int b) { return a + b; }, 20, 22);
	CHECK(integer.get() == 42 && !integer.valid());
	auto moved =
		pool.submit(MoveOnly(2), std::unique_ptr<int>(new int(40)));
	CHECK(moved.get() == 42);
	auto result =
		pool.submit([] { return std::unique_ptr<int>(new int(7)); });
	CHECK(*result.get() == 7);
	int referred = 0;
	auto reference = pool.submit(
		[](int &value) -> int & {
			value = 9;
			return value;
		},
		std::ref(referred));
	CHECK(&reference.get() == &referred && referred == 9);
	auto nothing = pool.submit([] {});
	nothing.get();
	auto exceptional = pool.submit(
		[]() -> int { throw std::runtime_error("work failed"); });
	bool caught = false;
	try {
		(void)exceptional.get();
	} catch (const std::runtime_error &error) {
		caught = std::string(error.what()) == "work failed";
	}
	CHECK(caught && !exceptional.valid());
	CopyFailure failing;
	caught = false;
	try {
		(void)pool.submit(failing);
	} catch (const std::runtime_error &error) {
		caught = std::string(error.what()) == "copy failed";
	}
	CHECK(caught);
	auto retained = pool.submit([] { return 123; });
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(pool.destroy() == HX_OS_OK && !owner);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(retained.get() == 123);
}

static void TestBoundaries(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread_pool_options options;
	hx_os_event *entered = NULL, *release = NULL;
	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	options.queue_capacity = 1;
	CHECK(hx_os_event_create(context, 1, 0, &entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &release, NULL) == HX_OS_OK);
	auto pool = hx::ThreadPool::create(owner, context, &options);
	auto first = pool.submit([entered, release] {
		hx_os_event_set(entered);
		CHECK(hx_os_event_wait_until(release, HX_OS_DEADLINE_INFINITE,
			      NULL) == HX_OS_OK);
		return 1;
	});
	CHECK(hx_os_event_wait_until(entered, hx_os_deadline_after_ms(2000),
		      NULL) == HX_OS_OK);
	auto queued = pool.submit([] { return 2; });
	hx_os_atomic copies = HX_OS_ATOMIC_INIT(0);
	CopyProbe probe(&copies);
	bool caught = false;
	try {
		(void)pool.submit(probe);
	} catch (const hx::StatusError &error) {
		caught = error.status() == HX_OS_LIMIT;
	}
	CHECK(caught && hx_os_atomic_load(&copies, HX_OS_RELAXED) == 0);
	CHECK(first.waitFor(std::chrono::milliseconds(0)) == HX_OS_TIMEOUT);
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2),
		      HX_OS_POOL_CANCEL_PENDING) == HX_OS_TIMEOUT);
	CHECK(owner && pool.destroy() == HX_OS_BUSY);
	hx_os_event_set(release);
	CHECK(first.get() == 1);
	caught = false;
	try {
		(void)queued.get();
	} catch (const hx::StatusError &error) {
		caught = error.status() == HX_OS_CANCELLED;
	}
	CHECK(caught);
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(pool.destroy() == HX_OS_OK);
	hx_os_event_destroy(entered);
	hx_os_event_destroy(release);
}

static void TestCancellationAndDeadlock(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread_pool_options options;
	hx_os_thread_pool_options_init(&options);
	options.threads = 1;
	auto pool = hx::ThreadPool::create(owner, context, &options);
	auto running = pool.submitCancelable([](hx::CancellationToken token) {
		while (!token.stopRequested())
			hx_os_cpu_relax();
		return 99;
	});
	running.cancel();
	try {
		CHECK(running.get() == 99);
	} catch (const hx::StatusError &error) {
		CHECK(error.status() == HX_OS_CANCELLED);
	}
	auto parent = pool.submit([&pool] {
		auto child = pool.submit([] { return 1; });
		return child.waitUntil(HX_OS_DEADLINE_INFINITE);
	});
	CHECK(parent.get() == HX_OS_DEADLOCK);
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(pool.destroy() == HX_OS_OK);
}

struct PreparationRace {
	hx_os_thread_pool **m_owner_;
	hx_os_event *m_entered_;
	hx_os_event *m_release_;
	hx_os_atomic m_status_;
};

struct BlockingCopy {
	PreparationRace *m_race_;
	explicit BlockingCopy(PreparationRace *race)
	    : m_race_(race)
	{
	}
	BlockingCopy(const BlockingCopy &other)
	    : m_race_(other.m_race_)
	{
		hx_os_event_set(m_race_->m_entered_);
		CHECK(hx_os_event_wait_until(m_race_->m_release_,
			      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);
	}
	void operator()() const
	{
		CHECK(false);
	}
};

static void *prepare_submit(void *user)
{
	PreparationRace *race = static_cast<PreparationRace *>(user);
	hx::ThreadPool pool(*race->m_owner_);
	BlockingCopy callable(race);
	try {
		(void)pool.submit(callable);
	} catch (const hx::StatusError &error) {
		hx_os_atomic_store(
			&race->m_status_, error.status(), HX_OS_RELEASE);
	}
	return NULL;
}

static void TestPreparationRace(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread *producer = NULL;
	PreparationRace race = {};
	race.m_owner_ = &owner;
	auto pool = hx::ThreadPool::create(owner, context);
	CHECK(hx_os_event_create(context, 1, 0, &race.m_entered_, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &race.m_release_, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(context, prepare_submit, &race, &producer,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_event_wait_until(race.m_entered_,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	hx_os_status closed = pool.closeUntil(hx_os_deadline_after_ms(20));
	CHECK(closed == HX_OS_BUSY || closed == HX_OS_TIMEOUT);
	CHECK(pool.destroy() == HX_OS_BUSY);
	hx_os_event_set(race.m_release_);
	CHECK(hx_os_thread_join_until(producer, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&producer, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&race.m_status_, HX_OS_ACQUIRE) ==
		HX_OS_CLOSED);
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(pool.destroy() == HX_OS_OK);
	hx_os_event_destroy(race.m_entered_);
	hx_os_event_destroy(race.m_release_);
}

struct CancelWait {
	hx::Task<int> m_task_;
	hx_os_atomic m_result_;
};

static void *wait_result(void *user)
{
	CancelWait *wait = static_cast<CancelWait *>(user);
	try {
		hx_os_atomic_store(
			&wait->m_result_, wait->m_task_.get(), HX_OS_RELEASE);
	} catch (const hx::StatusError &error) {
		hx_os_atomic_store(
			&wait->m_result_, error.status(), HX_OS_RELEASE);
	}
	return NULL;
}

static void TestIndependentCancellation(hx_os_context *context)
{
	hx_os_thread_pool *owner = NULL;
	hx_os_thread *waiter = NULL;
	hx_os_event *entered = NULL;
	CHECK(hx_os_event_create(context, 1, 0, &entered, NULL) == HX_OS_OK);
	auto pool = hx::ThreadPool::create(owner, context);
	CancelWait wait;
	wait.m_result_.value = 0;
	wait.m_task_ =
		pool.submitCancelable([entered](hx::CancellationToken token) {
			hx_os_event_set(entered);
			while (!token.stopRequested())
				hx_os_cpu_relax();
			return 42;
		});
	{
		auto source = wait.m_task_.cancellationSource();
		CHECK(source.valid());
		CHECK(hx_os_event_wait_until(entered,
			      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
		CHECK(hx_os_thread_create(context, wait_result, &wait, &waiter,
			      NULL) == HX_OS_OK);
		source.requestStop();
		CHECK(hx_os_thread_join_until(waiter,
			      hx_os_deadline_after_ms(2000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&waiter, NULL) == HX_OS_OK);
		CHECK(hx_os_atomic_load(&wait.m_result_, HX_OS_ACQUIRE) == 42);
		CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) ==
			HX_OS_OK);
		CHECK(pool.destroy() == HX_OS_OK);
		source.requestStop();
		CHECK(source.stopRequested());
	}
	hx_os_event_destroy(entered);
}

struct AllocationCounter {
	hx_os_atomic m_attempts_;
	hx_os_atomic m_fail_;
	hx_os_atomic m_live_;
};

static void *count_allocate(size_t bytes, void *user)
{
	AllocationCounter *counter = static_cast<AllocationCounter *>(user);
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
	AllocationCounter *counter = static_cast<AllocationCounter *>(user);
	hx_os_atomic_fetch_add(&counter->m_live_, -1, HX_OS_RELAXED);
	std::free(memory);
}

static void TestAllocationFailure()
{
	AllocationCounter counter = {};
	hx_os_context_options options;
	hx_os_context *context = NULL;
	hx_os_thread_pool *owner = NULL;
	hx_os_context_options_init(&options);
	options.allocator.allocate = count_allocate;
	options.allocator.deallocate = count_free;
	options.allocator.user = &counter;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	auto pool = hx::ThreadPool::create(owner, context);
	const int64_t baseline =
		hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED);
	bool succeeded = false;
	for (int64_t failure = 1; failure < 16; ++failure) {
		hx_os_atomic_store(&counter.m_attempts_, 0, HX_OS_RELAXED);
		hx_os_atomic_store(&counter.m_fail_, failure, HX_OS_RELAXED);
		try {
			auto task = pool.submit([] { return 42; });
			CHECK(task.get() == 42);
			succeeded = true;
		} catch (const hx::StatusError &error) {
			CHECK(error.status() == HX_OS_NO_MEMORY);
		}
		if (succeeded)
			break;
		CHECK(hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED) ==
			baseline);
	}
	hx_os_atomic_store(&counter.m_fail_, 0, HX_OS_RELAXED);
	CHECK(succeeded);
	CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) == HX_OS_OK);
	CHECK(pool.destroy() == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&counter.m_live_, HX_OS_RELAXED) == 0);
}

int main()
{
	hx_os_context *context = NULL;
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_THREAD_POOL) {
		hx_os_thread_pool *owner = NULL;
		auto pool = hx::ThreadPool::create(owner, context);
		CHECK(!pool.inside());
		auto identity =
			pool.submit([&pool]() { return pool.inside(); });
		CHECK(identity.get());
		CHECK(pool.closeUntil(hx_os_deadline_after_ms(2000)) ==
			HX_OS_OK);
		CHECK(pool.destroy() == HX_OS_OK);
		TestResults(context);
		TestBoundaries(context);
		TestCancellationAndDeadlock(context);
		TestPreparationRace(context);
		TestIndependentCancellation(context);
		TestAllocationFailure();
	}
	else {
		hx_os_thread_pool *owner = NULL;
		bool caught = false;
		try {
			(void)hx::ThreadPool::create(owner, context);
		} catch (const hx::StatusError &error) {
			caught = error.status() == HX_OS_NOT_SUPPORTED;
		}
		CHECK(caught && !owner);
	}
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	std::puts("C++11 task contracts passed");
}
