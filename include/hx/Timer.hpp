#ifndef HX_C_PLATFORM_CPP_TIMER_HPP
#define HX_C_PLATFORM_CPP_TIMER_HPP

#include <hx_os_timer.h>
#include <hx/Io.hpp>
#include <chrono>

namespace hx {
namespace detail {
struct TimerCallback {
	std::function<void(const hx_os_error &)> function;
	std::exception_ptr exception;
	explicit TimerCallback(std::function<void(const hx_os_error &)> fn)
	    : function(std::move(fn))
	{
	}
};
inline void HX_OS_CALL timer_tick(
	hx_os_timer *timer, const hx_os_error *error, void *user)
{
	auto &state = *static_cast<std::shared_ptr<TimerCallback> *>(user);
	try {
		state->function(*error);
	} catch (...) {
		state->exception = std::current_exception();
		hx_os_timer_stop(timer);
	}
}
inline void HX_OS_CALL timer_cleanup(void *user)
{
	std::unique_ptr<std::shared_ptr<TimerCallback>> state(
		static_cast<std::shared_ptr<TimerCallback> *>(user));
	(*state)->function = nullptr;
}
} // namespace detail

/* Host-owned pointer; explicit close preserves access after a timeout. */
class PeriodicTimer {
public:
	static PeriodicTimer create(hx_os_timer *&owner, IoLoop &loop,
		uint64_t intervalNs,
		std::function<void(const hx_os_error &)> function)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		if (!function)
			throw StatusError(HX_OS_INVALID);
		auto callback = std::make_shared<detail::TimerCallback>(
			std::move(function));
		std::unique_ptr<std::shared_ptr<detail::TimerCallback>> user(
			new std::shared_ptr<detail::TimerCallback>(callback));
		hx_os_error error;
		detail::CheckStatus(
			hx_os_timer_create_with_cleanup(loop.nativeHandle(),
				intervalNs, detail::timer_tick,
				detail::timer_cleanup, user.get(), &owner,
				&error),
			error);
		(void)user.release();
		return PeriodicTimer(owner, std::move(callback));
	}
	static PeriodicTimer every(hx_os_timer *&owner, IoLoop &loop,
		std::chrono::milliseconds interval, std::function<void()> tick)
	{
		if (interval.count() <= 0 || !tick)
			throw StatusError(HX_OS_INVALID);
		uint64_t milliseconds = static_cast<uint64_t>(interval.count());
		if (milliseconds > UINT64_MAX / UINT64_C(1000000))
			throw StatusError(HX_OS_RANGE);
		return create(owner, loop, milliseconds * UINT64_C(1000000),
			[tick](const hx_os_error &error) {
				detail::CheckStatus(error.status, error);
				tick();
			});
	}
	void stop() const noexcept
	{
		hx_os_timer_stop(*m_owner_);
	}
	hx_os_status waitUntil(hx_os_deadline deadline,
		hx_os_error *error = NULL) const noexcept
	{
		if (!*m_owner_) {
			hx_os_error_clear(error);
			return HX_OS_OK;
		}
		return hx_os_timer_wait_until(*m_owner_, deadline, error);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_timer_close_until(m_owner_, deadline, error);
	}
	/* Call after retirement; this synchronizes with the callback first. */
	void rethrowCallbackError() const
	{
		hx_os_error error;
		detail::CheckStatus(
			waitUntil(HX_OS_DEADLINE_NOW, &error), error);
		if (m_callback_->exception)
			std::rethrow_exception(m_callback_->exception);
	}
	hx_os_timer *nativeHandle() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_timer **m_owner_;
	std::shared_ptr<detail::TimerCallback> m_callback_;
	PeriodicTimer(hx_os_timer *&owner,
		std::shared_ptr<detail::TimerCallback> callback)
	    : m_owner_(&owner)
	    , m_callback_(std::move(callback))
	{
	}
};
} // namespace hx
#endif
