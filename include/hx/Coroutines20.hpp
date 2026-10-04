#ifndef HX_C_PLATFORM_CPP_COROUTINES20_HPP
#define HX_C_PLATFORM_CPP_COROUTINES20_HPP

#include <hx/Coroutines.hpp>
#include <hx/CoroutineSync.hpp>
#include <coroutine>
#include <cstddef>

namespace hx {
namespace coro {
namespace async {
/* The poll object, including every output buffer, lives in the C++ frame. */
template <typename Poll> class Awaiter {
public:
	explicit Awaiter(Poll poll) noexcept
	    : m_poll_(std::move(poll))
	{
	}

	bool await_ready() noexcept
	{
		m_result_ = m_poll_();

		return m_result_.status() != HX_OS_WOULD_BLOCK ||
			!hx_os_coro_wait_pending();
	}

	void await_suspend(std::coroutine_handle<>) const noexcept
	{
	}

	auto await_resume() noexcept -> decltype(std::declval<Poll &>()())
	{
		if (hx_os_coro_wait_pending())
			m_result_ = m_poll_();

		return std::move(m_result_);
	}

private:
	Poll m_poll_;
	decltype(std::declval<Poll &>()()) m_result_{
		Failure(HX_OS_WOULD_BLOCK)};
};

struct Sleep {
	hx_os_deadline deadline;

	Result<void> operator()() const noexcept
	{
		return coro::sleepUntil(deadline);
	}
};

template <typename Owner, typename T,
	Result<T> (Owner::*Method)(hx_os_deadline) const noexcept>
struct SyncWait {
	Owner owner;
	hx_os_deadline deadline;

	Result<T> operator()() noexcept
	{
		return (owner.*Method)(deadline);
	}
};

inline auto acquire(
	Mutex mutex, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	using Poll = SyncWait<Mutex, Lock, &Mutex::acquireUntil>;

	return Awaiter<Poll>(Poll{std::move(mutex), deadline});
}

inline auto wait(
	Event event, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	using Poll = SyncWait<Event, void, &Event::waitUntil>;

	return Awaiter<Poll>(Poll{std::move(event), deadline});
}

inline auto wait(Semaphore semaphore,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	using Poll = SyncWait<Semaphore, void, &Semaphore::waitUntil>;

	return Awaiter<Poll>(Poll{std::move(semaphore), deadline});
}

inline auto readLock(
	RwLock lock, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	using Poll = SyncWait<RwLock, Lock, &RwLock::readUntil>;

	return Awaiter<Poll>(Poll{std::move(lock), deadline});
}

inline auto writeLock(
	RwLock lock, hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	using Poll = SyncWait<RwLock, Lock, &RwLock::writeUntil>;

	return Awaiter<Poll>(Poll{std::move(lock), deadline});
}

struct ConditionWait {
	Condition condition;
	const Lock *lock;
	hx_os_deadline deadline;

	Result<void> operator()() noexcept
	{
		return condition.waitUntil(*lock, deadline);
	}
};

inline auto wait(Condition condition, const Lock &lock,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<ConditionWait>(
		ConditionWait{std::move(condition), &lock, deadline});
}

struct Select {
	const hx_os_coro_case *cases;
	size_t count;
	hx_os_deadline deadline;

	Result<size_t> operator()() const noexcept
	{
		return coro::select(cases, count, deadline);
	}
};

inline Awaiter<Select> select(const hx_os_coro_case *cases, size_t count,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Select>(Select{cases, count, deadline});
}

struct Yield {
	Result<void> operator()() const noexcept
	{
		return coro::yield();
	}
};

inline Awaiter<Sleep> sleepUntil(hx_os_deadline deadline) noexcept
{
	return Awaiter<Sleep>(Sleep{deadline});
}

inline Awaiter<Sleep> sleepFor(std::chrono::milliseconds duration) noexcept
{
	return sleepUntil(duration.count() <= 0
			? HX_OS_DEADLINE_NOW
			: hx_os_deadline_after_ms(
				  static_cast<uint64_t>(duration.count())));
}

inline Awaiter<Yield> yield() noexcept
{
	return Awaiter<Yield>(Yield{});
}

template <typename T> struct Receive {
	Channel<T> channel;
	hx_os_deadline deadline;
	typename Channel<T>::ReceiveStorage value;

	Result<T> operator()() noexcept
	{
		return channel.receiveInto(value, deadline);
	}
};

template <typename T> struct Send {
	Channel<T> channel;
	hx_os_deadline deadline;
	T value;

	Result<void> operator()() noexcept
	{
		return channel.send(std::move(value), deadline);
	}
};

template <typename T>
Awaiter<Receive<T>> receive(Channel<T> channel,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Receive<T>>(
		Receive<T>{std::move(channel), deadline, {}});
}

template <typename T>
Awaiter<Send<T>> send(Channel<T> channel, T value,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Send<T>>(
		Send<T>{std::move(channel), deadline, std::move(value)});
}

template <typename T> struct Join {
	Task<T> *task;
	hx_os_deadline deadline;

	Result<T> operator()() noexcept
	{
		return task->getUntil(deadline);
	}
};

/* The task owner must outlive this await. */
template <typename T>
Awaiter<Join<T>> join(Task<T> &task,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Join<T>>(Join<T>{&task, deadline});
}

struct Read {
	Stream stream;
	void *data;
	size_t bytes;
	hx_os_deadline deadline;

	Result<size_t> operator()() const noexcept
	{
		return stream.read(data, bytes, deadline);
	}
};

struct Write {
	Stream stream;
	const void *data;
	size_t bytes;
	hx_os_deadline deadline;

	Result<size_t> operator()() const noexcept
	{
		return stream.write(data, bytes, deadline);
	}
};

inline Awaiter<Read> read(Stream stream, void *data, size_t bytes,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Read>(Read{std::move(stream), data, bytes, deadline});
}

inline Awaiter<Write> write(Stream stream, const void *data, size_t bytes,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Write>(Write{std::move(stream), data, bytes, deadline});
}

struct Dial {
	hx_os_address address;
	hx_os_deadline deadline;

	Result<Stream> operator()() const noexcept
	{
		return Stream::dial(address, deadline);
	}
};

inline Awaiter<Dial> dial(const hx_os_address &address,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Dial>(Dial{address, deadline});
}

struct Accept {
	Stream stream;
	hx_os_deadline deadline;

	Result<Stream> operator()() const noexcept
	{
		return stream.accept(deadline);
	}
};

struct Listen {
	hx_os_address address;
	hx_os_address *bound;
	int backlog;
	bool reuse;

	Result<Stream> operator()() const noexcept
	{
		return Stream::listen(address, bound, backlog, reuse);
	}
};

inline Awaiter<Listen> listen(const hx_os_address &address,
	hx_os_address *bound = NULL, int backlog = 128,
	bool reuse = false) noexcept
{
	return Awaiter<Listen>(Listen{address, bound, backlog, reuse});
}

struct Resolve {
	const char *host;
	uint16_t port;
	hx_os_deadline deadline;

	Result<Addresses> operator()() const noexcept
	{
		return Addresses::resolve(host, port, deadline);
	}
};

inline Awaiter<Resolve> resolve(const char *host, uint16_t port,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Resolve>(Resolve{host, port, deadline});
}

inline Awaiter<Accept> accept(Stream stream,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	return Awaiter<Accept>(Accept{std::move(stream), deadline});
}

struct PipePair {
	Stream *reader;
	Stream *writer;

	Result<void> operator()() const noexcept
	{
		return Stream::pipePair(*reader, *writer);
	}
};

inline Awaiter<PipePair> pipePair(Stream &reader, Stream &writer) noexcept
{
	return Awaiter<PipePair>(PipePair{&reader, &writer});
}
} // namespace async

namespace impl {
template <typename T> struct FrameJob;
}

/* Frames are allocated with the default C allocator; creation is fallible.
 * Only library awaiters are accepted: foreign executors cannot resume this
 * pinned frame. co_return supplies a value, Result<T>, or Result<void>().
 */
template <typename T = void> class Routine {
public:
	struct promise_type {
		Result<T> result{Failure(HX_OS_CANCELLED)};

		static void *operator new(size_t bytes) noexcept
		{
			void *memory = NULL;
			/* MSVC's max_align_t is 8 on x64, but coroutine frames
			 * can use aligned SSE stores under ordinary new's ABI.
			 */
			constexpr size_t baseAlignment =
				alignof(std::max_align_t) >
					__STDCPP_DEFAULT_NEW_ALIGNMENT__
				? alignof(std::max_align_t)
				: __STDCPP_DEFAULT_NEW_ALIGNMENT__;
			constexpr size_t alignment =
				alignof(promise_type) > baseAlignment
				? alignof(promise_type)
				: baseAlignment;

			(void)hx_os_memory_alloc(
				NULL, bytes, alignment, &memory, NULL);

			return memory;
		}

		static void operator delete(void *memory, size_t) noexcept
		{
			hx_os_memory_free(memory);
		}

		static Routine
		get_return_object_on_allocation_failure() noexcept
		{
			return Routine();
		}

		Routine get_return_object() noexcept
		{
			return Routine(std::coroutine_handle<
				promise_type>::from_promise(*this));
		}

		std::suspend_always initial_suspend() const noexcept
		{
			return {};
		}

		std::suspend_always final_suspend() const noexcept
		{
			return {};
		}

		template <typename V> void return_value(V &&value) noexcept
		{
			result = detail::NormalizeTask<T>(
				std::forward<V>(value));
		}

		void unhandled_exception() noexcept
		{
			result = Failure(HX_OS_INVALID);
		}

		template <typename P>
		async::Awaiter<P> await_transform(
			async::Awaiter<P> value) noexcept
		{
			return value;
		}
	};

	typedef std::coroutine_handle<promise_type> Handle;

	Routine() noexcept
	    : m_handle_()
	{
	}

	Routine(Routine &&other) noexcept
	    : m_handle_(other.release())
	{
	}

	Routine &operator=(Routine &&other) noexcept
	{
		if (this != &other) {
			if (m_handle_)
				m_handle_.destroy();

			m_handle_ = other.release();
		}

		return *this;
	}

	Routine(const Routine &) = delete;

	Routine &operator=(const Routine &) = delete;

	~Routine() noexcept
	{
		if (m_handle_)
			m_handle_.destroy();
	}

	bool valid() const noexcept
	{
		return bool(m_handle_);
	}

private:
	friend struct impl::FrameJob<T>;
	Handle release() noexcept
	{
		Handle handle = m_handle_;

		m_handle_ = {};

		return handle;
	}

	Handle m_handle_;

	explicit Routine(Handle handle) noexcept
	    : m_handle_(handle)
	{
	}
};

namespace impl {
template <typename T> struct FrameJob : detail::TaskState<T> {
	typename Routine<T>::Handle handle;

	FrameJob(Routine<T> &&routine) noexcept
	    : handle(routine.release())
	{
		this->dispose = dispose_state;
	}

	static void dispose_state(detail::TaskState<T> *state) noexcept
	{
		auto *job = static_cast<FrameJob *>(state);

		job->~FrameJob();

		hx_os_memory_free(job);
	}

	static hx_os_status HX_OS_CALL execute(
		hx_os_coro *, void *user, hx_os_error *) noexcept
	{
		auto *job = static_cast<FrameJob *>(user);

		job->handle.resume();
		if (!job->handle.done())
			return HX_OS_WOULD_BLOCK;

		job->result = std::move(job->handle.promise().result);
		job->invoked = true;

		return hx_os_coro_complete(
			job->result.status(), &job->result.error());
	}

	static void HX_OS_CALL cleanup(void *user) noexcept
	{
		auto *job = static_cast<FrameJob *>(user);

		job->handle.destroy();
		job->handle = {};

		job->release();
	}
};

template <typename T> struct FrameFactory {
	hx_os_context *context;
	Routine<T> *routine;
	FrameJob<T> *state;

	static hx_os_status HX_OS_CALL prepare(
		void *user, void **out, hx_os_error *error) noexcept
	{
		auto *factory = static_cast<FrameFactory *>(user);
		void *memory = NULL;

		if (!factory->routine->valid()) {
			if (error)
				*error = Failure(HX_OS_NO_MEMORY).detail;

			return HX_OS_NO_MEMORY;
		}

		auto status = hx_os_memory_alloc(factory->context,
			sizeof(FrameJob<T>), alignof(FrameJob<T>), &memory,
			error);
		if (status != HX_OS_OK)
			return status;

		factory->state =
			new (memory) FrameJob<T>(std::move(*factory->routine));
		*out = factory->state;

		return HX_OS_OK;
	}
};
} // namespace impl

template <typename T>
Result<Task<T>> Runtime::go(Routine<T> &&routine) const noexcept
{
	if (!*m_owner_)
		return Failure(HX_OS_CLOSED);

	if (!routine.valid())
		return Failure(HX_OS_NO_MEMORY);

	impl::FrameFactory<T> factory{
		hx_os_coro_runtime_context(*m_owner_), &routine, NULL};

	hx_os_coro *task = NULL;
	hx_os_error error = {};

	auto status = hx_os_coro_go_prepared(*m_owner_, HX_OS_CORO_STACKLESS,
		impl::FrameFactory<T>::prepare, &factory,
		impl::FrameJob<T>::execute, impl::FrameJob<T>::cleanup, &task,
		&error);
	if (status != HX_OS_OK) {
		if (factory.state)
			factory.state->release();

		return Failure(status, error);
	}

	return Task<T>(task, factory.state);
}

template <typename T>
Result<void> Runtime::spawn(Routine<T> &&routine) const noexcept
{
	impl::FrameFactory<T> factory{
		hx_os_coro_runtime_context(*m_owner_), &routine, NULL};

	hx_os_error error = {};

	auto status = hx_os_coro_spawn_prepared(*m_owner_, HX_OS_CORO_STACKLESS,
		impl::FrameFactory<T>::prepare, &factory,
		impl::FrameJob<T>::execute, impl::FrameJob<T>::cleanup, &error);
	if (factory.state)
		factory.state->release();

	return detail::Status(status, error);
}

inline Result<void> Group::spawn(Routine<void> &&routine) noexcept
{
	if (m_busy_)
		return record(Failure(HX_OS_BUSY));

	if (m_count_ >= m_capacity_)
		return record(Failure(HX_OS_LIMIT));

	m_busy_ = true;

	void *memory = NULL;
	hx_os_error error = {};

	auto status = hx_os_memory_alloc(
		hx_os_coro_runtime_context(m_runtime_.nativeHandle()),
		sizeof(Entry), alignof(Entry), &memory, &error);
	if (status != HX_OS_OK) {
		m_busy_ = false;

		return record(Failure(status, error));
	}

	auto submitted = m_runtime_.go(std::move(routine));
	if (!submitted) {
		hx_os_memory_free(memory);
		m_busy_ = false;

		return record(Failure(submitted.error()));
	}

	auto *entry = new (memory) Entry;
	entry->task = std::move(submitted.value());
	if (m_last_)
		m_last_->next = entry;
	else
		m_first_ = entry;

	m_last_ = entry;
	++m_count_;
	m_busy_ = false;

	return Result<void>();
}
} // namespace coro
} // namespace hx
#endif
