#ifndef HX_C_PLATFORM_CPP_THREAD_POOL_HPP
#define HX_C_PLATFORM_CPP_THREAD_POOL_HPP

#include <hx_os_atomic.h>
#include <hx_os_pool.h>
#include <hx/Status.hpp>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace hx {

class CancellationToken {
public:
	CancellationToken() noexcept
	    : m_task_(NULL)
	{
	}
	explicit CancellationToken(hx_os_task *task) noexcept
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
	~CancellationToken()
	{
		hx_os_task_release(m_task_);
	}
	bool stopRequested() const noexcept
	{
		return m_task_ && hx_os_task_cancel_requested(m_task_);
	}
	void throwIfCancelled() const
	{
		if (stopRequested())
			throw StatusError(HX_OS_CANCELLED);
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

template <typename Function, typename... Arguments>
auto CallImpl(std::false_type, Function &function, Arguments &&...arguments)
	-> decltype(function(std::forward<Arguments>(arguments)...))
{
	return function(std::forward<Arguments>(arguments)...);
}

template <typename Function, typename... Arguments>
auto CallImpl(std::true_type, Function &function, Arguments &&...arguments)
	-> decltype(std::mem_fn(function)(
		std::forward<Arguments>(arguments)...))
{
	return std::mem_fn(function)(std::forward<Arguments>(arguments)...);
}

template <typename Function, typename... Arguments>
auto Call(Function &function, Arguments &&...arguments)
	-> decltype(CallImpl(std::is_member_pointer<Function>(), function,
		std::forward<Arguments>(arguments)...))
{
	return CallImpl(std::is_member_pointer<Function>(), function,
		std::forward<Arguments>(arguments)...);
}

template <std::size_t... Values> struct Indices {};
template <std::size_t Count, std::size_t... Values>
struct MakeIndices : MakeIndices<Count - 1, Count - 1, Values...> {};
template <std::size_t... Values> struct MakeIndices<0, Values...> {
	typedef Indices<Values...> type;
};

template <typename Result> class ResultSlot {
public:
	ResultSlot() noexcept
	    : m_ready_(false)
	{
		static_assert(std::is_nothrow_destructible<Result>::value,
			"Task results must have non-throwing destructors");
		static_assert(!std::is_rvalue_reference<Result>::value,
			"Return values, lvalue references or void from a task");
	}
	~ResultSlot()
	{
		if (m_ready_)
			reinterpret_cast<Result *>(&m_storage_)->~Result();
	}
	template <typename Function, typename... Arguments>
	void invoke(Function &function, Arguments &&...arguments)
	{
		new (&m_storage_) Result(
			Call(function, std::forward<Arguments>(arguments)...));
		m_ready_ = true;
	}
	Result take()
	{
		if (!m_ready_)
			throw StatusError(HX_OS_CANCELLED);
		return std::move(*reinterpret_cast<Result *>(&m_storage_));
	}

private:
	alignas(Result) unsigned char m_storage_[sizeof(Result)];
	bool m_ready_;
};

template <typename Result> class ResultSlot<Result &> {
public:
	ResultSlot() noexcept
	    : m_value_(NULL)
	{
	}
	template <typename Function, typename... Arguments>
	void invoke(Function &function, Arguments &&...arguments)
	{
		m_value_ = std::addressof(
			Call(function, std::forward<Arguments>(arguments)...));
	}
	Result &take()
	{
		if (!m_value_)
			throw StatusError(HX_OS_CANCELLED);
		return *m_value_;
	}

private:
	Result *m_value_;
};

template <> class ResultSlot<void> {
public:
	template <typename Function, typename... Arguments>
	void invoke(Function &function, Arguments &&...arguments)
	{
		Call(function, std::forward<Arguments>(arguments)...);
	}
	void take() const noexcept
	{
	}
};

template <typename Result> struct TaskState;

template <typename Result, bool Inherit> struct TaskStorage;

/* A short base before the aligned reference count can grow 32-bit objects. */
template <typename Result> struct TaskStorage<Result, false> {
	hx_os_atomic m_references_;
	std::exception_ptr m_exception_;
	ResultSlot<Result> m_result_;
	void (*m_dispose_)(TaskState<Result> *);
	TaskStorage() noexcept
	    : m_references_{2}
	    , m_dispose_(NULL)
	{
	}
	ResultSlot<Result> &result() noexcept
	{
		return m_result_;
	}
};

/* Empty bases and aligned result storage avoid an extra member boundary. */
template <typename Result>
struct TaskStorage<Result, true> : ResultSlot<Result> {
	hx_os_atomic m_references_;
	std::exception_ptr m_exception_;
	void (*m_dispose_)(TaskState<Result> *);
	TaskStorage() noexcept
	    : m_references_{2}
	    , m_dispose_(NULL)
	{
	}
	ResultSlot<Result> &result() noexcept
	{
		return *this;
	}
};

template <typename Result>
struct TaskState
    : TaskStorage<Result,
	      std::is_empty<ResultSlot<Result>>::value ||
		      sizeof(ResultSlot<Result>) % alignof(hx_os_atomic) == 0> {
	void release() noexcept
	{
		if (hx_os_atomic_fetch_add(
			    &this->m_references_, -1, HX_OS_ACQ_REL) == 1)
			this->m_dispose_(this);
	}
};

template <bool WithToken> struct Invoke;
template <> struct Invoke<false> {
	template <typename Slot, typename Function, typename Tuple,
		std::size_t... I>
	static void call(Slot &slot, Function &function, Tuple &arguments,
		hx_os_task *, Indices<I...>)
	{
		slot.invoke(function, std::move(std::get<I>(arguments))...);
	}
};
template <> struct Invoke<true> {
	template <typename Slot, typename Function, typename Tuple,
		std::size_t... I>
	static void call(Slot &slot, Function &function, Tuple &arguments,
		hx_os_task *task, Indices<I...>)
	{
		slot.invoke(function, CancellationToken(task),
			std::move(std::get<I>(arguments))...);
	}
};

template <bool WithToken, typename Function, typename... Arguments>
struct ReturnType {
	typedef decltype(hx::detail::Call(
		std::declval<typename std::decay<Function>::type &>(),
		std::declval<typename std::decay<Arguments>::type>()...)) type;
};
template <typename Function, typename... Arguments>
struct ReturnType<true, Function, Arguments...> {
	typedef decltype(hx::detail::Call(
		std::declval<typename std::decay<Function>::type &>(),
		std::declval<CancellationToken>(),
		std::declval<typename std::decay<Arguments>::type>()...)) type;
};

template <bool WithToken, typename Function, typename... Arguments>
class Job
    : public TaskState<
	      typename ReturnType<WithToken, Function, Arguments...>::type> {
public:
	typedef typename ReturnType<WithToken, Function, Arguments...>::type
		Result;
	typedef TaskState<Result> State;
	typedef std::tuple<typename std::decay<Function>::type,
		typename std::decay<Arguments>::type...>
		Payload;
	Job(Function &&function, Arguments &&...arguments)
	    : m_live_(false)
	{
		static_assert(std::is_nothrow_destructible<Payload>::value,
			"Task callables and arguments must have non-throwing "
			"destructors");
		this->m_dispose_ = destroy_state;
		new (&m_payload_) Payload(std::forward<Function>(function),
			std::forward<Arguments>(arguments)...);
		m_live_ = true;
	}
	~Job()
	{
		dropPayload();
	}
	static hx_os_status HX_OS_CALL execute(
		hx_os_task *task, void *user) noexcept
	{
		Job *job = static_cast<Job *>(user);
		try {
			job->run(task,
				typename MakeIndices<sizeof...(
					Arguments)>::type());
		} catch (...) {
			job->m_exception_ = std::current_exception();
		}
		return HX_OS_OK;
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
	template <std::size_t... I> void run(hx_os_task *task, Indices<I...>)
	{
		Payload &payload = *reinterpret_cast<Payload *>(&m_payload_);
		/* Reference tuple avoids copying bound move-only arguments. */
		auto arguments =
			std::forward_as_tuple(std::get<I + 1>(payload)...);
		Invoke<WithToken>::call(this->result(), std::get<0>(payload),
			arguments, task, Indices<I...>());
	}
	static void destroy_state(State *state) noexcept
	{
		Job *job = static_cast<Job *>(state);
		job->~Job();
		hx_os_memory_free(job);
	}
};

template <bool WithToken, typename Function, typename... Arguments>
class Factory {
public:
	typedef Job<WithToken, Function, Arguments...> Work;
	typedef typename Work::State State;
	hx_os_context *m_context_;
	State *m_state_;
	std::exception_ptr m_exception_;
	std::tuple<Function &&, Arguments &&...> m_arguments_;
	Factory(hx_os_context *contextValue, Function &&function,
		Arguments &&...values)
	    : m_context_(contextValue)
	    , m_state_(NULL)
	    , m_arguments_(std::forward<Function>(function),
		      std::forward<Arguments>(values)...)
	{
	}
	static hx_os_status HX_OS_CALL prepare(void *user, void **out) noexcept
	{
		Factory *factory = static_cast<Factory *>(user);
		void *memory = NULL;
		*out = NULL;
		hx_os_status status = hx_os_memory_alloc(factory->m_context_,
			sizeof(Work), alignof(Work), &memory, NULL);
		if (status != HX_OS_OK)
			return status;
		try {
			factory->construct(memory,
				typename MakeIndices<sizeof...(
					Arguments)>::type());
		} catch (...) {
			factory->m_exception_ = std::current_exception();
			hx_os_memory_free(memory);
			return HX_OS_INVALID;
		}
		*out = static_cast<Work *>(factory->m_state_);
		return HX_OS_OK;
	}

private:
	template <std::size_t... I> void construct(void *memory, Indices<I...>)
	{
		m_state_ = new (memory)
			Work(std::forward<Function>(std::get<0>(m_arguments_)),
				std::forward<Arguments>(
					std::get<I + 1>(m_arguments_))...);
	}
};

} // namespace detail

class ThreadPool;

template <typename Result> class Task {
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
	~Task()
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
	/* Copy this source before handing the Task to another thread. */
	CancellationSource cancellationSource() const noexcept
	{
		return CancellationSource(m_task_);
	}
	hx_os_status waitUntil(hx_os_deadline deadline) const noexcept
	{
		return hx_os_task_wait_until(m_task_, deadline, NULL, NULL);
	}
	hx_os_status waitFor(std::chrono::milliseconds duration) const noexcept
	{
		return waitUntil(duration.count() <= 0
				? HX_OS_DEADLINE_NOW
				: hx_os_deadline_after_ms(static_cast<uint64_t>(
					  duration.count())));
	}
	void wait() const
	{
		hx_os_error error;
		if (hx_os_task_wait_until(m_task_, HX_OS_DEADLINE_INFINITE,
			    NULL, &error) != HX_OS_OK)
			throw StatusError(error);
	}
	Result get()
	{
		hx_os_status work;
		hx_os_error error;
		if (hx_os_task_wait_until(m_task_, HX_OS_DEADLINE_INFINITE,
			    &work, &error) != HX_OS_OK)
			throw StatusError(error);
		Task owner(std::move(*this));
		if (work != HX_OS_OK)
			throw StatusError(work);
		if (owner.m_state_->m_exception_)
			std::rethrow_exception(owner.m_state_->m_exception_);
		return owner.m_state_->result().take();
	}

private:
	friend class ThreadPool;
	hx_os_task *m_task_;
	detail::TaskState<Result> *m_state_;
	Task(hx_os_task *task, detail::TaskState<Result> *state) noexcept
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

/*
 * A non-owning facade over a host-owned C pointer variable. The variable
 * outlives the facade; closeUntil is bounded/retryable and never destroys.
 * After success, externally exclude all pool calls and explicitly destroy.
 * Facade destruction performs no wait, detach, implicit close or ownership
 * loss: the original C owner remains available for retry. Task results own
 * references independently and may outlive the successfully destroyed pool.
 */
class ThreadPool {
public:
	explicit ThreadPool(hx_os_thread_pool *&owner,
		std::size_t maxTaskBytes = 1024 * 1024) noexcept
	    : m_owner_(&owner)
	    , m_maxTaskBytes_(maxTaskBytes)
	{
	}
	static ThreadPool create(hx_os_thread_pool *&owner,
		hx_os_context *context = NULL,
		const hx_os_thread_pool_options *options = NULL)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		if (hx_os_thread_pool_create(
			    context, options, &owner, &error) != HX_OS_OK)
			throw StatusError(error);
		return ThreadPool(owner);
	}
	hx_os_thread_pool *nativeHandle() const noexcept
	{
		return *m_owner_;
	}
	bool inside() const
	{
		if (!*m_owner_)
			throw StatusError(HX_OS_CLOSED);
		int value;
		hx_os_status status =
			hx_os_thread_pool_inside(*m_owner_, &value);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return value != 0;
	}
	template <typename Function, typename... Arguments>
	Task<typename detail::ReturnType<false, Function, Arguments...>::type>
	submit(Function &&function, Arguments &&...arguments) const
	{
		return submitImpl<false>(std::forward<Function>(function),
			std::forward<Arguments>(arguments)...);
	}
	template <typename Function, typename... Arguments>
	Task<typename detail::ReturnType<true, Function, Arguments...>::type>
	submitCancelable(Function &&function, Arguments &&...arguments) const
	{
		return submitImpl<true>(std::forward<Function>(function),
			std::forward<Arguments>(arguments)...);
	}
	hx_os_status closeUntil(hx_os_deadline deadline,
		int mode = HX_OS_POOL_DRAIN,
		hx_os_error *error = NULL) const noexcept
	{
		if (!*m_owner_) {
			hx_os_error_clear(error);
			return HX_OS_OK;
		}
		return hx_os_thread_pool_close_until(
			*m_owner_, mode, deadline, error);
	}
	hx_os_status destroy(hx_os_error *error = NULL) noexcept
	{
		return hx_os_thread_pool_destroy(m_owner_, error);
	}

private:
	hx_os_thread_pool **m_owner_;
	std::size_t m_maxTaskBytes_;
	template <bool WithToken, typename Function, typename... Arguments>
	Task<typename detail::ReturnType<WithToken, Function,
		Arguments...>::type>
	submitImpl(Function &&function, Arguments &&...arguments) const
	{
		typedef detail::Factory<WithToken, Function, Arguments...>
			Factory;
		typedef typename Factory::Work Work;
		typedef typename Work::Result Result;
		if (!*m_owner_)
			throw StatusError(HX_OS_CLOSED);
		if (sizeof(Work) > m_maxTaskBytes_)
			throw StatusError(HX_OS_LIMIT);
		Factory factory(hx_os_thread_pool_context(*m_owner_),
			std::forward<Function>(function),
			std::forward<Arguments>(arguments)...);
		hx_os_task *task = NULL;
		hx_os_error error;
		hx_os_status status = hx_os_thread_pool_submit_prepared(
			*m_owner_, Factory::prepare, &factory, Work::execute,
			Work::cleanup, &task, &error);
		if (status != HX_OS_OK) {
			if (factory.m_state_)
				factory.m_state_->release();
			if (factory.m_exception_)
				std::rethrow_exception(factory.m_exception_);
			throw StatusError(error);
		}
		return Task<Result>(task, factory.m_state_);
	}
};

} // namespace hx
#endif
