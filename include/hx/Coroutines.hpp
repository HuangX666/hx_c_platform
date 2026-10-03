#ifndef HX_C_PLATFORM_CPP_COROUTINES_HPP
#define HX_C_PLATFORM_CPP_COROUTINES_HPP
#include <hx_os_coroutine.h>
#include <hx/ThreadPool.hpp>

namespace hx {
namespace coro {
template <typename T> class Routine;
class Runtime;

/* A task handle never owns the worker thread. Releasing it does not cancel. */
template <typename T> class Task {
public:
	Task() noexcept
	    : m_task_(NULL)
	    , m_state_(NULL)
	{
	}
	Task(Task &&other) noexcept
	    : m_task_(other.m_task_)
	    , m_state_(other.m_state_)
	{
		other.m_task_ = NULL;
		other.m_state_ = NULL;
	}
	Task &operator=(Task &&other) noexcept
	{
		if (this != &other) {
			reset();
			m_task_ = other.m_task_;
			m_state_ = other.m_state_;
			other.m_task_ = NULL;
			other.m_state_ = NULL;
		}
		return *this;
	}
	Task(const Task &) = delete;
	Task &operator=(const Task &) = delete;
	~Task() noexcept
	{
		reset();
	}
	bool ready() const noexcept
	{
		return hx_os_coro_ready(m_task_) != 0;
	}
	void cancel() const noexcept
	{
		hx_os_coro_cancel(m_task_);
	}
	hx_os_coro *nativeHandle() const noexcept
	{
		return m_task_;
	}
	Result<void> waitUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_coro_wait_until(m_task_, deadline, NULL, &error);
		return detail::Status(status, error);
	}
	Result<void> wait() const noexcept
	{
		return waitUntil(HX_OS_DEADLINE_INFINITE);
	}
	Result<T> getUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error result = {}, error = {};
		auto status = hx_os_coro_wait_until(
			m_task_, deadline, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		Task owner(std::move(*this));
		if (!owner.m_state_->invoked)
			return Failure(result);
		return std::move(owner.m_state_->result);
	}
	Result<T> get() noexcept
	{
		return getUntil(HX_OS_DEADLINE_INFINITE);
	}

private:
	friend class Runtime;
	hx_os_coro *m_task_;
	detail::TaskState<T> *m_state_;
	Task(hx_os_coro *task, detail::TaskState<T> *state) noexcept
	    : m_task_(task)
	    , m_state_(state)
	{
	}
	void reset() noexcept
	{
		hx_os_coro_release(m_task_);
		if (m_state_)
			m_state_->release();
		m_task_ = NULL;
		m_state_ = NULL;
	}
};

namespace impl {
template <typename T> struct IsRoutine : std::false_type {};
template <typename T> struct IsRoutine<Routine<T>> : std::true_type {};
template <typename F, typename... A>
struct Factory : detail::Factory<false, F, A...> {
	typedef detail::Factory<false, F, A...> Base;
	typedef typename Base::Work Work;
	Factory(hx_os_context *context, F &&function, A &&...arguments) noexcept
	    : Base(context, std::forward<F>(function),
		      std::forward<A>(arguments)...)
	{
	}
	static hx_os_status HX_OS_CALL prepare(
		void *user, void **out, hx_os_error *error) noexcept
	{
		auto *factory = static_cast<Factory *>(user);
		auto status = Base::prepare(static_cast<Base *>(factory), out);
		if (error)
			error->status = status;
		return status;
	}
	static hx_os_status HX_OS_CALL execute(
		hx_os_coro *, void *user, hx_os_error *error) noexcept
	{
		auto status = Work::execute(NULL, user);
		*error = static_cast<Work *>(user)->result.error();
		return hx_os_coro_complete(status, error);
	}
};
} // namespace impl

/* Explicit owner matches ThreadPool: keep it alive and retry shutdown on
 * timeout. Destruction of this borrowed facade never waits or detaches.
 */
class Runtime {
public:
	explicit Runtime(hx_os_coro_runtime *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	static Result<Runtime> create(hx_os_coro_runtime *&owner,
		hx_os_context *context = NULL,
		const hx_os_coro_options *options = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_coro_runtime_create(
			context, options, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return Runtime(owner);
	}
	hx_os_coro_runtime *nativeHandle() const noexcept
	{
		return *m_owner_;
	}
	template <typename F, typename... A,
		typename std::enable_if<
			!impl::IsRoutine<typename std::decay<F>::type>::value,
			int>::type = 0>
	Result<Task<typename detail::ReturnType<false, F, A...>::type>> go(
		F &&function, A &&...arguments) const noexcept
	{
		typedef impl::Factory<F, A...> Factory;
		typedef typename Factory::Work Work;
		typedef typename Work::Value Value;
		static_assert(!impl::IsRoutine<typename Work::Raw>::value,
			"Submit C++20 frames with "
			"runtime.go(function(args...))");
		typedef typename std::decay<F>::type Function;
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);
		if (sizeof(Work) > 1024 * 1024)
			return Failure(HX_OS_LIMIT);
		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);
		Factory factory(hx_os_coro_runtime_context(*m_owner_),
			std::forward<F>(function),
			std::forward<A>(arguments)...);
		hx_os_coro *task = NULL;
		hx_os_error error = {};
		auto status = hx_os_coro_go_prepared(*m_owner_,
			HX_OS_CORO_STACKFUL, Factory::prepare, &factory,
			Factory::execute, Work::cleanup, &task, &error);
		if (status != HX_OS_OK) {
			if (factory.state)
				factory.state->release();
			return Failure(status, error);
		}
		return Task<Value>(task, factory.state);
	}
	template <typename T>
	Result<Task<T>> go(Routine<T> &&routine) const noexcept;
	Result<void> closeUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_runtime_close_until(
			*m_owner_, deadline, &error);
		return detail::Status(status, error);
	}
	Result<void> drainUntil(hx_os_deadline deadline) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_runtime_drain_until(
			*m_owner_, deadline, &error);
		return detail::Status(status, error);
	}
	Result<void> destroy() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_runtime_destroy(m_owner_, &error);
		return detail::Status(status, error);
	}
	Result<void> shutdownUntil(hx_os_deadline deadline) const noexcept
	{
		if (!*m_owner_)
			return Result<void>();
		auto result = closeUntil(deadline);
		return result ? destroy() : std::move(result);
	}

private:
	hx_os_coro_runtime **m_owner_;
};

inline bool stopRequested() noexcept
{
	return hx_os_coro_cancel_requested(hx_os_coro_current()) != 0;
}
inline Result<size_t> select(const hx_os_coro_case *cases, size_t count,
	hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
{
	size_t selected = SIZE_MAX;
	hx_os_error error = {};
	auto status =
		hx_os_coro_select(cases, count, deadline, &selected, &error);
	if (selected == SIZE_MAX)
		return Failure(status, error);
	return Result<size_t>::withValue(selected, status, error);
}
inline Result<void> yield() noexcept
{
	hx_os_error error = {};
	auto status = hx_os_coro_yield(&error);
	return detail::Status(status, error);
}
inline Result<void> sleepUntil(hx_os_deadline deadline) noexcept
{
	hx_os_error error = {};
	auto status = hx_os_coro_sleep_until(deadline, &error);
	return detail::Status(status, error);
}
inline Result<void> sleepFor(std::chrono::milliseconds duration) noexcept
{
	return sleepUntil(duration.count() <= 0
			? HX_OS_DEADLINE_NOW
			: hx_os_deadline_after_ms(
				  static_cast<uint64_t>(duration.count())));
}

/* Trivial values cross the channel by value. Owning C values can instead use
 * the C copy/drop callbacks; C++ objects with internal self-pointers cannot be
 * relocated as raw C storage. */
template <typename T> class Channel {
	static_assert(std::is_trivially_copyable<T>::value,
		"Channel<T> requires a trivially copyable value");
	static_assert(std::is_nothrow_default_constructible<T>::value,
		"Channel<T> receives into nonthrowing default storage");

public:
	Channel() noexcept
	    : m_channel_(NULL)
	{
	}
	Channel(const Channel &other) noexcept
	    : m_channel_(other.m_channel_)
	{
		hx_os_coro_channel_retain(m_channel_);
	}
	Channel(Channel &&other) noexcept
	    : m_channel_(other.m_channel_)
	{
		other.m_channel_ = NULL;
	}
	Channel &operator=(Channel other) noexcept
	{
		std::swap(m_channel_, other.m_channel_);
		return *this;
	}
	~Channel() noexcept
	{
		hx_os_coro_channel_release(m_channel_);
	}
	static Result<Channel> create(
		const Runtime &runtime, size_t capacity = 0) noexcept
	{
		hx_os_container_options options;
		hx_os_container_options_init(&options, sizeof(T));
		options.element_alignment = alignof(T);
		Channel value;
		hx_os_error error = {};
		auto status = hx_os_coro_channel_create(runtime.nativeHandle(),
			&options, capacity, &value.m_channel_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	hx_os_coro_channel *nativeHandle() const noexcept
	{
		return m_channel_;
	}
	Result<void> send(const T &value,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_channel_send_until(
			m_channel_, &value, deadline, &error);
		return detail::Status(status, error);
	}
	Result<T> receive(hx_os_deadline deadline =
				  HX_OS_DEADLINE_INFINITE) const noexcept
	{
		T value;
		if (hx_os_coro_kind(hx_os_coro_current()) !=
			HX_OS_CORO_STACKFUL)
			return Failure(HX_OS_INVALID);
		hx_os_error error = {};
		auto status = hx_os_coro_channel_receive_until(
			m_channel_, &value, deadline, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<void> close() const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_coro_channel_close(m_channel_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_coro_channel *m_channel_;
};
class Addresses {
public:
	Addresses() noexcept
	    : m_addresses_(NULL)
	{
	}
	Addresses(Addresses &&other) noexcept
	    : m_addresses_(other.m_addresses_)
	{
		other.m_addresses_ = NULL;
	}
	Addresses &operator=(Addresses &&other) noexcept
	{
		if (this != &other) {
			hx_os_address_list_destroy(m_addresses_);
			m_addresses_ = other.m_addresses_;
			other.m_addresses_ = NULL;
		}
		return *this;
	}
	Addresses(const Addresses &) = delete;
	Addresses &operator=(const Addresses &) = delete;
	~Addresses() noexcept
	{
		hx_os_address_list_destroy(m_addresses_);
	}
	size_t size() const noexcept
	{
		return hx_os_address_list_count(m_addresses_);
	}
	const hx_os_address *get(size_t index) const noexcept
	{
		return hx_os_address_list_get(m_addresses_, index);
	}
	static Result<Addresses> resolve(const char *host, uint16_t port,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
	{
		Addresses value;
		hx_os_error error = {};
		auto status = hx_os_coro_resolve(
			host, port, deadline, &value.m_addresses_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}

private:
	hx_os_address_list *m_addresses_;
};

class Stream {
public:
	Stream() noexcept
	    : m_stream_(NULL)
	{
	}
	Stream(const Stream &other) noexcept
	    : m_stream_(other.m_stream_)
	{
		hx_os_coro_stream_retain(m_stream_);
	}
	Stream(Stream &&other) noexcept
	    : m_stream_(other.m_stream_)
	{
		other.m_stream_ = NULL;
	}
	Stream &operator=(Stream other) noexcept
	{
		std::swap(m_stream_, other.m_stream_);
		return *this;
	}
	~Stream() noexcept
	{
		hx_os_coro_stream_release(m_stream_);
	}
	hx_os_coro_stream *nativeHandle() const noexcept
	{
		return m_stream_;
	}
	static Result<Stream> dial(const hx_os_address &address,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
	{
		Stream value;
		hx_os_error error = {};
		auto status = hx_os_coro_dial(
			&address, deadline, &value.m_stream_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	static Result<Stream> listen(const hx_os_address &address,
		hx_os_address *bound = NULL, int backlog = 128,
		bool reuse = false) noexcept
	{
		Stream value;
		hx_os_error error = {};
		auto status = hx_os_coro_listen(&address, backlog,
			reuse ? 1 : 0, &value.m_stream_, bound, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<Stream>
	accept(hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) const noexcept
	{
		Stream value;
		hx_os_error error = {};
		auto status = hx_os_coro_accept(
			m_stream_, deadline, &value.m_stream_, NULL, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	Result<size_t> read(void *data, size_t bytes,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		size_t count = 0;
		hx_os_error error = {};
		auto status = hx_os_coro_read(
			m_stream_, data, bytes, deadline, &count, &error);
		return Result<size_t>::withValue(count, status, error);
	}
	Result<size_t> write(const void *data, size_t bytes,
		hx_os_deadline deadline =
			HX_OS_DEADLINE_INFINITE) const noexcept
	{
		size_t count = 0;
		hx_os_error error = {};
		auto status = hx_os_coro_write(
			m_stream_, data, bytes, deadline, &count, &error);
		return Result<size_t>::withValue(count, status, error);
	}
	static Result<void> pipePair(Stream &reader, Stream &writer) noexcept
	{
		if (&reader == &writer)
			return Failure(HX_OS_INVALID);
		Stream read, write;
		hx_os_error error = {};
		auto status = hx_os_coro_pipe_pair(
			&read.m_stream_, &write.m_stream_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		reader = std::move(read);
		writer = std::move(write);
		return Result<void>();
	}

private:
	hx_os_coro_stream *m_stream_;
};
} // namespace coro
} // namespace hx
#endif
