#ifndef HX_C_PLATFORM_CPP_THREAD_POOL_HPP
#define HX_C_PLATFORM_CPP_THREAD_POOL_HPP

#include <hx_os_pool.h>
#include <hx/Callback.hpp>
#include <chrono>
#include <functional>
#include <tuple>

namespace hx {

class CancellationToken {
public:
	explicit CancellationToken(hx_os_task *task = NULL) noexcept
	    : m_task_(task)
	{
		if (m_task_)
			hx_os_task_retain(m_task_);
	}

	CancellationToken(const CancellationToken &other) noexcept
	    : CancellationToken(other.m_task_)
	{
	}

	CancellationToken(CancellationToken &&other) noexcept
	    : m_task_(other.m_task_)
	{
		other.m_task_ = NULL;
	}

	CancellationToken &operator=(CancellationToken other) noexcept
	{
		std::swap(m_task_, other.m_task_);

		return *this;
	}

	~CancellationToken() noexcept
	{
		hx_os_task_release(m_task_);
	}

	bool stopRequested() const noexcept
	{
		return m_task_ && hx_os_task_cancel_requested(m_task_);
	}

	Result<void> check() const noexcept
	{
		return Result<void>(
			stopRequested() ? HX_OS_CANCELLED : HX_OS_OK);
	}

private:
	friend class CancellationSource;
	hx_os_task *m_task_;
};

class CancellationSource {
public:
	explicit CancellationSource(hx_os_task *task = NULL) noexcept
	    : m_token_(task)
	{
	}

	void requestStop() const noexcept
	{
		if (m_token_.m_task_)
			hx_os_task_cancel(m_token_.m_task_);
	}

	bool stopRequested() const noexcept
	{
		return m_token_.stopRequested();
	}

	bool valid() const noexcept
	{
		return m_token_.m_task_ != NULL;
	}

private:
	CancellationToken m_token_;
};

namespace detail {
template <typename F, typename... A>
auto CallImpl(std::false_type, F &function, A &&...arguments) noexcept(
	noexcept(function(std::forward<A>(arguments)...)))
	-> decltype(function(std::forward<A>(arguments)...))
{
	return function(std::forward<A>(arguments)...);
}

template <typename F, typename... A>
auto CallImpl(std::true_type, F &function, A &&...arguments) noexcept
	-> decltype(std::mem_fn(function)(std::forward<A>(arguments)...))
{
	/* C++11 member-pointer types cannot encode their noexcept contract. */
	return std::mem_fn(function)(std::forward<A>(arguments)...);
}

template <typename F, typename... A>
auto Call(F &function, A &&...arguments) noexcept(noexcept(CallImpl(
	std::is_member_pointer<F>(), function, std::forward<A>(arguments)...)))
	-> decltype(CallImpl(std::is_member_pointer<F>(), function,
		std::forward<A>(arguments)...))
{
	static_assert(std::is_pointer<F>::value ||
			std::is_member_pointer<F>::value ||
			noexcept(CallImpl(std::is_member_pointer<F>(), function,
				std::forward<A>(arguments)...)),
		"Task callables must be noexcept");

	return CallImpl(std::is_member_pointer<F>(), function,
		std::forward<A>(arguments)...);
}

template <size_t... I> struct Indices {};
template <size_t N, size_t... I>
struct MakeIndices : MakeIndices<N - 1, N - 1, I...> {};
template <size_t... I> struct MakeIndices<0, I...> {
	typedef Indices<I...> type;
};

template <typename T> struct UnwrapResult {
	typedef T type;
};

template <typename T> struct UnwrapResult<Result<T>> {
	typedef T type;
};

template <typename T, typename V> Result<T> NormalizeTask(V &&value) noexcept
{
	return Result<T>(std::forward<V>(value));
}

template <typename T> Result<T> NormalizeTask(Result<T> &&value) noexcept
{
	return std::move(value);
}

template <bool Token> struct Invoke;
template <> struct Invoke<false> {
	template <typename F, typename Tuple, size_t... I>
	static auto call(F &function, Tuple &arguments, hx_os_task *,
		Indices<I...>) noexcept -> decltype(Call(function,
		std::move(std::get<I>(arguments))...))
	{
		return Call(function, std::move(std::get<I>(arguments))...);
	}
};

template <> struct Invoke<true> {
	template <typename F, typename Tuple, size_t... I>
	static auto call(F &function, Tuple &arguments, hx_os_task *task,
		Indices<I...>) noexcept -> decltype(Call(function,
		CancellationToken(task), std::move(std::get<I>(arguments))...))
	{
		return Call(function, CancellationToken(task),
			std::move(std::get<I>(arguments))...);
	}
};

template <bool Token, typename F, typename... A> struct ReturnType {
	typedef decltype(Call(std::declval<typename std::decay<F>::type &>(),
		std::declval<typename std::decay<A>::type>()...)) raw;

	typedef typename UnwrapResult<raw>::type type;
};

template <typename F, typename... A> struct ReturnType<true, F, A...> {
	typedef decltype(Call(std::declval<typename std::decay<F>::type &>(),
		std::declval<CancellationToken>(),
		std::declval<typename std::decay<A>::type>()...)) raw;

	typedef typename UnwrapResult<raw>::type type;
};

template <typename T> struct TaskState {
	hx_os_atomic references;
	Result<T> result;

	void (*dispose)(TaskState *);

	bool invoked;

	TaskState() noexcept
	    : references{2}
	    , result(Failure(HX_OS_CANCELLED))
	    , dispose(NULL)
	    , invoked(false)
	{
		static_assert(!std::is_rvalue_reference<T>::value,
			"Return values, lvalue references or void, not rvalue "
			"references");
	}

	void release() noexcept
	{
		if (hx_os_atomic_fetch_add(&references, -1, HX_OS_ACQ_REL) == 1)
			dispose(this);
	}
};

template <bool Token, typename F, typename... A>
class Job : public TaskState<typename ReturnType<Token, F, A...>::type> {
public:
	typedef typename ReturnType<Token, F, A...>::type Value;
	typedef typename ReturnType<Token, F, A...>::raw Raw;
	typedef TaskState<Value> State;
	typedef std::tuple<typename std::decay<F>::type,
		typename std::decay<A>::type...>
		Payload;

	Job(F &&function, A &&...arguments) noexcept
	    : m_live_(true)
	{
		static_assert(std::is_nothrow_constructible<Payload, F &&,
				      A &&...>::value,
			"Task captures/arguments must be copied or moved "
			"without failure");
		static_assert(std::is_nothrow_destructible<Payload>::value,
			"Task captures/arguments need noexcept destruction");
		this->dispose = destroy_state;
		new (&m_payload_) Payload(std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	~Job() noexcept
	{
		dropPayload();
	}

	static hx_os_status HX_OS_CALL execute(
		hx_os_task *task, void *user) noexcept
	{
		Job *job = static_cast<Job *>(user);

		job->run(task, typename MakeIndices<sizeof...(A)>::type(),
			std::is_void<Raw>());
		job->invoked = true;

		return job->result.status();
	}

	static void HX_OS_CALL cleanup(void *user) noexcept
	{
		Job *job = static_cast<Job *>(user);

		job->dropPayload();

		job->release();
	}

private:
	alignas(Payload) unsigned char m_payload_[sizeof(Payload)];
	bool m_live_;

	void dropPayload() noexcept
	{
		if (m_live_) {
			reinterpret_cast<Payload *>(&m_payload_)->~Payload();
			m_live_ = false;
		}
	}

	template <size_t... I>
	void run(hx_os_task *task, Indices<I...>, std::false_type) noexcept
	{
		auto &payload = *reinterpret_cast<Payload *>(&m_payload_);
		auto arguments =
			std::forward_as_tuple(std::get<I + 1>(payload)...);

		this->result = NormalizeTask<Value>(
			Invoke<Token>::call(std::get<0>(payload), arguments,
				task, Indices<I...>()));
	}

	template <size_t... I>
	void run(hx_os_task *task, Indices<I...>, std::true_type) noexcept
	{
		auto &payload = *reinterpret_cast<Payload *>(&m_payload_);
		auto arguments =
			std::forward_as_tuple(std::get<I + 1>(payload)...);

		Invoke<Token>::call(
			std::get<0>(payload), arguments, task, Indices<I...>());
		this->result = Result<void>();
	}

	static void destroy_state(State *state) noexcept
	{
		Job *job = static_cast<Job *>(state);

		job->~Job();

		hx_os_memory_free(job);
	}
};

template <bool Token, typename F, typename... A> class Factory {
public:
	typedef Job<Token, F, A...> Work;
	typedef typename Work::State State;
	hx_os_context *context;
	State *state;
	std::tuple<F &&, A &&...> arguments;

	Factory(hx_os_context *ctx, F &&function, A &&...values) noexcept
	    : context(ctx)
	    , state(NULL)
	    , arguments(std::forward<F>(function), std::forward<A>(values)...)
	{
	}

	static hx_os_status HX_OS_CALL prepare(void *user, void **out) noexcept
	{
		auto *factory = static_cast<Factory *>(user);
		void *memory = NULL;

		*out = NULL;

		auto status = hx_os_memory_alloc(factory->context, sizeof(Work),
			alignof(Work), &memory, NULL);
		if (status != HX_OS_OK)
			return status;

		factory->construct(
			memory, typename MakeIndices<sizeof...(A)>::type());
		*out = static_cast<Work *>(factory->state);

		return HX_OS_OK;
	}

private:
	template <size_t... I>
	void construct(void *memory, Indices<I...>) noexcept
	{
		state = new (memory)
			Work(std::forward<F>(std::get<0>(arguments)),
				std::forward<A>(std::get<I + 1>(arguments))...);
	}
};
} // namespace detail

class ThreadPool;

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

	bool valid() const noexcept
	{
		return m_task_ != NULL;
	}

	bool ready() const noexcept
	{
		return m_task_ &&
			hx_os_task_state(m_task_) == HX_OS_TASK_FINISHED;
	}

	void cancel() noexcept
	{
		if (m_task_)
			hx_os_task_cancel(m_task_);
	}

	CancellationSource cancellationSource() const noexcept
	{
		return CancellationSource(m_task_);
	}

	Result<void> waitUntil(hx_os_deadline deadline) const noexcept
	{
		if (!m_task_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status =
			hx_os_task_wait_until(m_task_, deadline, NULL, &error);

		return detail::Status(status, error);
	}

	Result<void> waitFor(std::chrono::milliseconds duration) const noexcept
	{
		return waitUntil(duration.count() <= 0
				? HX_OS_DEADLINE_NOW
				: hx_os_deadline_after_ms(static_cast<uint64_t>(
					  duration.count())));
	}

	Result<void> wait() const noexcept
	{
		return waitUntil(HX_OS_DEADLINE_INFINITE);
	}

	/* Failed waits retain the task. Completed work consumes it even on
	 * error. A callable returning Result<T> is flattened into Task<T>,
	 * preserving native error details and partial values. Reference results
	 * borrow external data.
	 */
	Result<T> getUntil(hx_os_deadline deadline) noexcept
	{
		if (!m_task_)
			return Failure(HX_OS_CLOSED);

		hx_os_status work = HX_OS_OK;
		hx_os_error error = {};
		auto status =
			hx_os_task_wait_until(m_task_, deadline, &work, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		Task owner(std::move(*this));
		if (!owner.m_state_->invoked)
			return Failure(
				work == HX_OS_OK ? HX_OS_CANCELLED : work);

		return std::move(owner.m_state_->result);
	}

	Result<T> get() noexcept
	{
		return getUntil(HX_OS_DEADLINE_INFINITE);
	}

private:
	friend class ThreadPool;
	hx_os_task *m_task_;
	detail::TaskState<T> *m_state_;

	Task(hx_os_task *task, detail::TaskState<T> *state) noexcept
	    : m_task_(task)
	    , m_state_(state)
	{
	}

	void reset() noexcept
	{
		hx_os_task_release(m_task_);
		if (m_state_)
			m_state_->release();

		m_task_ = NULL;
		m_state_ = NULL;
	}
};

/* The host owns the C pointer. Explicit bounded shutdown preserves it on
 * failure. Destruction of this facade never waits, detaches or kills.
 */
class ThreadPool {
public:
	explicit ThreadPool(hx_os_thread_pool *&owner,
		size_t maxTaskBytes = 1024 * 1024) noexcept
	    : m_owner_(&owner)
	    , m_maxTaskBytes_(maxTaskBytes)
	{
	}

	static Result<ThreadPool> create(hx_os_thread_pool *&owner,
		hx_os_context *context = NULL,
		const hx_os_thread_pool_options *options = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};

		auto status = hx_os_thread_pool_create(
			context, options, &owner, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return ThreadPool(owner);
	}

	static Result<ThreadPool> withThreads(hx_os_thread_pool *&owner,
		size_t threads, size_t queueCapacity = 256,
		hx_os_context *context = NULL) noexcept
	{
		hx_os_thread_pool_options options;

		hx_os_thread_pool_options_init(&options);
		options.threads = threads;
		options.queue_capacity = queueCapacity;

		return create(owner, context, &options);
	}

	hx_os_thread_pool *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

	Result<bool> inside() const noexcept
	{
		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		int value = 0;

		auto status = hx_os_thread_pool_inside(*m_owner_, &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return value != 0;
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<false, F, A...>::type>> submit(
		F &&function, A &&...arguments) const noexcept
	{
		return submitImpl<false>(false, 0, std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<true, F, A...>::type>>
	submitCancelable(F &&function, A &&...arguments) const noexcept
	{
		return submitImpl<true>(false, 0, std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<false, F, A...>::type>>
	submitAt(hx_os_deadline due, F &&function,
		A &&...arguments) const noexcept
	{
		return submitImpl<false>(true, due, std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<true, F, A...>::type>>
	submitCancelableAt(hx_os_deadline due, F &&function,
		A &&...arguments) const noexcept
	{
		return submitImpl<true>(true, due, std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<false, F, A...>::type>>
	submitAfter(std::chrono::milliseconds duration, F &&function,
		A &&...arguments) const noexcept
	{
		return submitAt(duration.count() <= 0
				? HX_OS_DEADLINE_NOW
				: hx_os_deadline_after_ms(static_cast<uint64_t>(
					  duration.count())),
			std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	template <typename F, typename... A>
	Result<Task<typename detail::ReturnType<true, F, A...>::type>>
	submitCancelableAfter(std::chrono::milliseconds duration, F &&function,
		A &&...arguments) const noexcept
	{
		return submitCancelableAt(duration.count() <= 0
				? HX_OS_DEADLINE_NOW
				: hx_os_deadline_after_ms(static_cast<uint64_t>(
					  duration.count())),
			std::forward<F>(function),
			std::forward<A>(arguments)...);
	}

	Result<void> closeUntil(hx_os_deadline deadline,
		int mode = HX_OS_POOL_DRAIN) const noexcept
	{
		if (!*m_owner_)
			return Result<void>();

		hx_os_error error = {};

		auto status = hx_os_thread_pool_close_until(
			*m_owner_, mode, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> shutdownUntil(
		hx_os_deadline deadline, int mode = HX_OS_POOL_DRAIN) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_thread_pool_shutdown_ex(
			m_owner_, mode, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> destroy() noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_thread_pool_destroy(m_owner_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_thread_pool **m_owner_;
	size_t m_maxTaskBytes_;

	template <bool Token, typename F, typename... A>
	Result<Task<typename detail::ReturnType<Token, F, A...>::type>>
	submitImpl(bool delayed, hx_os_deadline due, F &&function,
		A &&...arguments) const noexcept
	{
		typedef detail::Factory<Token, F, A...> Factory;
		typedef typename Factory::Work Work;
		typedef typename Work::Value Value;
		typedef typename std::decay<F>::type Function;

		if (!*m_owner_)
			return Failure(HX_OS_CLOSED);

		if (sizeof(Work) > m_maxTaskBytes_)
			return Failure(HX_OS_LIMIT);

		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		Factory factory(hx_os_thread_pool_context(*m_owner_),
			std::forward<F>(function),
			std::forward<A>(arguments)...);

		hx_os_task *task = NULL;
		hx_os_error error = {};
		auto status = delayed
			? hx_os_thread_pool_submit_delayed_prepared(*m_owner_,
				  due, Factory::prepare, &factory,
				  Work::execute, Work::cleanup, &task, &error)
			: hx_os_thread_pool_submit_prepared(*m_owner_,
				  Factory::prepare, &factory, Work::execute,
				  Work::cleanup, &task, &error);

		if (status != HX_OS_OK) {
			if (factory.state)
				factory.state->release();

			return Failure(status, error);
		}

		return Task<Value>(task, factory.state);
	}
};
} // namespace hx
#endif
