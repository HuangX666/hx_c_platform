#ifndef HX_C_PLATFORM_CPP_TIMER_HPP
#define HX_C_PLATFORM_CPP_TIMER_HPP

#include <hx_os_timer.h>
#include <hx/Io.hpp>
#include <chrono>

namespace hx {

class PeriodicTimer {
public:
	template <typename F>
	static Result<PeriodicTimer> create(hx_os_timer *&owner, IoLoop &loop,
		uint64_t intervalNs, F &&function) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		typedef typename std::decay<F>::type Function;

		auto callback = detail::CallbackOwner::create(
			loop.context(), std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());

		auto *state = callback.value().nativeLease();

		hx_os_error error = {};

		auto status = hx_os_timer_create_with_cleanup(
			loop.nativeHandle(), intervalNs, tick<Function>,
			detail::callback_cleanup, state, &owner, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return PeriodicTimer(owner, std::move(callback.value()));
	}

	template <typename F>
	static Result<PeriodicTimer> every(hx_os_timer *&owner, IoLoop &loop,
		std::chrono::milliseconds interval, F &&function) noexcept
	{
		typedef typename std::decay<F>::type Function;

		if (interval.count() <= 0)
			return Failure(HX_OS_INVALID);

		if (detail::NullCallable(function, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		uint64_t milliseconds = static_cast<uint64_t>(interval.count());
		if (milliseconds > UINT64_MAX / UINT64_C(1000000))
			return Failure(HX_OS_RANGE);

		return create(owner, loop, milliseconds * UINT64_C(1000000),
			Every<Function>(std::forward<F>(function)));
	}

	void stop() const noexcept
	{
		hx_os_timer_stop(*m_owner_);
	}

	Result<void> waitUntil(hx_os_deadline deadline) const noexcept
	{
		if (!*m_owner_)
			return Result<void>();

		hx_os_error error = {};
		auto status =
			hx_os_timer_wait_until(*m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_timer_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> callbackResult() const noexcept
	{
		auto waited = waitUntil(HX_OS_DEADLINE_NOW);
		if (!waited)
			return waited;

		return m_callback_.result();
	}

	hx_os_timer *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_timer **m_owner_;
	detail::CallbackOwner m_callback_;

	PeriodicTimer(
		hx_os_timer *&owner, detail::CallbackOwner callback) noexcept
	    : m_owner_(&owner)
	    , m_callback_(std::move(callback))
	{
	}

	template <typename F>
	static void HX_OS_CALL tick(hx_os_timer *timer,
		const hx_os_error *error, void *user) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));

		auto result = detail::InvokeCallback(state->function(), *error);
		state->record(result);
		if (!result)
			hx_os_timer_stop(timer);
	}

	template <typename Function> struct Every {
		Function function;

		template <typename F>
		explicit Every(F &&input) noexcept
		    : function(std::forward<F>(input))
		{
			static_assert(std::is_nothrow_constructible<Function,
					      F &&>::value,
				"Move or copy timer captures without failure");
		}

		Result<void> operator()(const hx_os_error &error) noexcept
		{
			if (error.status != HX_OS_OK)
				return Failure(error);

			return detail::InvokeCallback(function);
		}
	};
};
} // namespace hx
#endif
