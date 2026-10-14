#ifndef HX_C_PLATFORM_CPP_CALLBACK_HPP
#define HX_C_PLATFORM_CPP_CALLBACK_HPP

#include <hx/Status.hpp>
#include <hx_os_atomic.h>

namespace hx {

namespace detail {
/**
 * \brief One native lease and any facade leases share state.
 *
 * \details Native retirement destroys captures before close completes; the
 * error record can outlive it. Error inspection requires the native wait/close
 * synchronization first.
 */
struct CallbackState {
	hx_os_atomic references;
	hx_os_error error;

	void (*retire)(CallbackState *);
	void (*dispose)(CallbackState *);
	CallbackState() noexcept
	    : references{1}
	    , error{HX_OS_OK, HX_OS_ERROR_NONE, 0, 0}
	    , retire(NULL)
	    , dispose(NULL)
	{
	}

	void retain() noexcept
	{
		(void)hx_os_atomic_fetch_add(&references, 1, HX_OS_RELAXED);
	}

	void release() noexcept
	{
		if (hx_os_atomic_fetch_add(&references, -1, HX_OS_ACQ_REL) == 1)
			dispose(this);
	}

	void record(const Result<void> &result) noexcept
	{
		if (error.status == HX_OS_OK && !result)
			error = result.error();
	}
};

template <typename Function> struct CallbackModel : CallbackState {
	alignas(Function) unsigned char payload[sizeof(Function)];

	bool active;

	template <typename F>
	explicit CallbackModel(F &&function) noexcept
	    : active(true)
	{
		static_assert(
			std::is_nothrow_constructible<Function, F &&>::value,
			"Move or copy callback captures without "
			"allocation/failure");
		static_assert(std::is_nothrow_destructible<Function>::value,
			"Callback captures need noexcept destruction");
		new (&payload) Function(std::forward<F>(function));
		retire = retire_state;
		dispose = dispose_state;
	}

	Function &function() noexcept
	{
		return *reinterpret_cast<Function *>(&payload);
	}

	static void retire_state(CallbackState *base) noexcept
	{
		auto *self = static_cast<CallbackModel *>(base);

		if (self->active) {
			self->function().~Function();
			self->active = false;
		}
	}

	static void dispose_state(CallbackState *base) noexcept
	{
		retire_state(base);

		auto *self = static_cast<CallbackModel *>(base);

		self->~CallbackModel();

		hx_os_memory_free(self);
	}
};

inline void HX_OS_CALL callback_cleanup(void *user) noexcept
{
	auto *state = static_cast<CallbackState *>(user);

	state->retire(state);

	state->release();
}

template <typename F> bool NullCallable(const F &, std::false_type) noexcept
{
	return false;
}

template <typename F> bool NullCallable(F function, std::true_type) noexcept
{
	return function == NULL;
}

inline Result<void> CallbackResult(Result<void> result) noexcept
{
	return result;
}

inline Result<void> CallbackResult(hx_os_status status) noexcept
{
	return Result<void>(status);
}

template <typename F, typename... A>
Result<void> InvokeResult(
	std::true_type, F &function, A &&...arguments) noexcept
{
	function(std::forward<A>(arguments)...);

	return Result<void>();
}

template <typename F, typename... A>
Result<void> InvokeResult(
	std::false_type, F &function, A &&...arguments) noexcept
{
	return CallbackResult(function(std::forward<A>(arguments)...));
}

template <typename F, typename... A>
Result<void> InvokeCallback(F &function, A &&...arguments) noexcept
{
	static_assert(std::is_pointer<F>::value || std::is_function<F>::value ||
			noexcept(function(std::forward<A>(arguments)...)),
		"Callbacks must be noexcept; C++11 function pointers promise "
		"it by contract");

	typedef decltype(function(std::forward<A>(arguments)...)) Return;

	return InvokeResult(std::is_void<Return>(), function,
		std::forward<A>(arguments)...);
}

class CallbackOwner {
public:
	CallbackOwner() noexcept
	    : m_state_(NULL)
	{
	}

	CallbackOwner(const CallbackOwner &other) noexcept
	    : m_state_(other.m_state_)
	{
		if (m_state_)
			m_state_->retain();
	}

	CallbackOwner &operator=(const CallbackOwner &other) noexcept
	{
		if (this != &other) {
			if (other.m_state_)
				other.m_state_->retain();

			if (m_state_)
				m_state_->release();

			m_state_ = other.m_state_;
		}

		return *this;
	}

	CallbackOwner(CallbackOwner &&other) noexcept
	    : m_state_(other.m_state_)
	{
		other.m_state_ = NULL;
	}

	CallbackOwner &operator=(CallbackOwner &&other) noexcept
	{
		if (this != &other) {
			if (m_state_)
				m_state_->release();

			m_state_ = other.m_state_;
			other.m_state_ = NULL;
		}

		return *this;
	}

	~CallbackOwner() noexcept
	{
		if (m_state_)
			m_state_->release();
	}

	template <typename F>
	static Result<CallbackOwner> create(
		hx_os_context *context, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;
		typedef CallbackModel<Function> Model;

		if (NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		void *memory = NULL;
		hx_os_error error = {};

		auto status = hx_os_memory_alloc(context, sizeof(Model),
			alignof(Model), &memory, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		CallbackOwner result;

		result.m_state_ = new (memory) Model(std::forward<F>(function));

		return result;
	}

	CallbackState *nativeLease() const noexcept
	{
		m_state_->retain();

		return m_state_;
	}

	CallbackState *state() const noexcept
	{
		return m_state_;
	}

	Result<void> result() const noexcept
	{
		return m_state_
			? Status(m_state_->error.status, m_state_->error)
			: Result<void>();
	}

private:
	CallbackState *m_state_;
};
} // namespace detail
} // namespace hx
#endif
