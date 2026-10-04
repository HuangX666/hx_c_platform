#ifndef HX_C_PLATFORM_CPP_WATCH_HPP
#define HX_C_PLATFORM_CPP_WATCH_HPP

#include <hx_os_watch.h>
#include <hx_os_loop_fd.h>
#include <hx/Io.hpp>

namespace hx {

/* Facades retain callback diagnostics; native owners remain with the host. */
class DirectoryWatch {
public:
	template <typename F>
	static Result<DirectoryWatch> create(hx_os_watch *&owner,
		const char *path, F &&function, bool recursive = true,
		hx_os_context *context = NULL,
		const hx_os_watch_options *options = NULL) noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);

		typedef typename std::decay<F>::type Function;

		auto callback = detail::CallbackOwner::create(
			context, std::forward<F>(function));
		if (!callback)
			return Failure(callback.error());

		auto *state = callback.value().nativeLease();

		hx_os_error error = {};

		auto status = hx_os_watch_create_with_options(context, path,
			recursive ? 1 : 0, options, changed<Function>,
			detail::callback_cleanup, state, &owner, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return DirectoryWatch(owner, std::move(callback.value()));
	}

	void stop() const noexcept
	{
		hx_os_watch_stop(*m_owner_);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_watch_close_until(m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> callbackResult() const noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		return m_callback_.result();
	}

private:
	hx_os_watch **m_owner_;
	detail::CallbackOwner m_callback_;

	DirectoryWatch(
		hx_os_watch *&owner, detail::CallbackOwner callback) noexcept
	    : m_owner_(&owner)
	    , m_callback_(std::move(callback))
	{
	}

	template <typename F>
	static void HX_OS_CALL changed(hx_os_watch *watch,
		const hx_os_watch_event *event, void *user) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));

		auto result = detail::InvokeCallback(
			state->function(), watch, *event);
		state->record(result);
		if (!result)
			hx_os_watch_stop(watch);
	}
};

class DescriptorWatch {
public:
	template <typename F>
	static Result<DescriptorWatch> create(hx_os_loop_watch *&owner,
		IoLoop &loop, int descriptor, int events, F &&function) noexcept
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

		auto status = hx_os_loop_attach_fd_with_cleanup(
			loop.nativeHandle(), descriptor, events,
			ready<Function>, detail::callback_cleanup, state,
			&owner, &error);
		if (status != HX_OS_OK) {
			state->release();

			return Failure(status, error);
		}

		return DescriptorWatch(owner, std::move(callback.value()));
	}

	void stop() const noexcept
	{
		hx_os_loop_watch_stop(*m_owner_);
	}

	int descriptor() const noexcept
	{
		return hx_os_loop_watch_descriptor(*m_owner_);
	}

	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_loop_watch_close_until(
			m_owner_, deadline, &error);

		return detail::Status(status, error);
	}

	Result<void> callbackResult() const noexcept
	{
		if (*m_owner_) {
			hx_os_error error = {};

			auto status = hx_os_loop_watch_wait_until(
				*m_owner_, HX_OS_DEADLINE_NOW, &error);
			if (status != HX_OS_OK)
				return Failure(status, error);
		}

		return m_callback_.result();
	}

private:
	hx_os_loop_watch **m_owner_;
	detail::CallbackOwner m_callback_;

	DescriptorWatch(hx_os_loop_watch *&owner,
		detail::CallbackOwner callback) noexcept
	    : m_owner_(&owner)
	    , m_callback_(std::move(callback))
	{
	}

	template <typename F>
	static void HX_OS_CALL ready(
		hx_os_loop_watch *watch, int events, void *user) noexcept
	{
		auto *state = static_cast<detail::CallbackModel<F> *>(
			static_cast<detail::CallbackState *>(user));

		auto result = detail::InvokeCallback(
			state->function(), watch, events);
		state->record(result);
		if (!result)
			hx_os_loop_watch_stop(watch);
	}
};
} // namespace hx
#endif
