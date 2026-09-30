#ifndef HX_C_PLATFORM_CPP_WATCH_HPP
#define HX_C_PLATFORM_CPP_WATCH_HPP
#include <hx_os_watch.h>
#include <hx_os_loop_fd.h>
#include <hx/Io.hpp>

namespace hx {
namespace detail {
template <typename Function> struct WatchCallback {
	Function function;
	std::exception_ptr exception;
	explicit WatchCallback(Function fn)
	    : function(std::move(fn))
	{
	}
};
typedef WatchCallback<
	std::function<void(hx_os_watch *, const hx_os_watch_event &)>>
	DirectoryCallback;
typedef WatchCallback<std::function<void(hx_os_loop_watch *, int)>>
	DescriptorCallback;
inline void HX_OS_CALL directory_changed(
	hx_os_watch *watch, const hx_os_watch_event *event, void *user)
{
	auto &state = *static_cast<std::shared_ptr<DirectoryCallback> *>(user);
	try {
		state->function(watch, *event);
	} catch (...) {
		state->exception = std::current_exception();
		hx_os_watch_stop(watch);
	}
}
inline void HX_OS_CALL descriptor_ready(
	hx_os_loop_watch *watch, int events, void *user)
{
	auto &state = *static_cast<std::shared_ptr<DescriptorCallback> *>(user);
	try {
		state->function(watch, events);
	} catch (...) {
		state->exception = std::current_exception();
		hx_os_loop_watch_stop(watch);
	}
}
template <typename State> inline void HX_OS_CALL watch_cleanup(void *user)
{
	std::unique_ptr<std::shared_ptr<State>> state(
		static_cast<std::shared_ptr<State> *>(user));
	(*state)->function = nullptr;
}
} // namespace detail

/* Facades keep the host's C owner reachable; destructors do not wait/detach. */
class DirectoryWatch {
public:
	static DirectoryWatch create(hx_os_watch *&owner, const char *path,
		std::function<void(hx_os_watch *, const hx_os_watch_event &)>
			function,
		bool recursive = true, hx_os_context *context = NULL,
		const hx_os_watch_options *options = NULL)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		if (!function)
			throw StatusError(HX_OS_INVALID);
		auto callback = std::make_shared<detail::DirectoryCallback>(
			std::move(function));
		std::unique_ptr<std::shared_ptr<detail::DirectoryCallback>>
			user(new std::shared_ptr<detail::DirectoryCallback>(
				callback));
		hx_os_error error;
		detail::CheckStatus(hx_os_watch_create_with_options(context,
					    path, recursive ? 1 : 0, options,
					    detail::directory_changed,
					    detail::watch_cleanup<
						    detail::DirectoryCallback>,
					    user.get(), &owner, &error),
			error);
		(void)user.release();
		return DirectoryWatch(owner, std::move(callback));
	}
	void stop() const noexcept
	{
		hx_os_watch_stop(*m_owner_);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_watch_close_until(m_owner_, deadline, error);
	}
	/* Successful close synchronizes with the worker and releases captures.
	 */
	void rethrowCallbackError() const
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		if (m_callback_->exception)
			std::rethrow_exception(m_callback_->exception);
	}

private:
	hx_os_watch **m_owner_;
	std::shared_ptr<detail::DirectoryCallback> m_callback_;
	DirectoryWatch(hx_os_watch *&owner,
		std::shared_ptr<detail::DirectoryCallback> state)
	    : m_owner_(&owner)
	    , m_callback_(std::move(state))
	{
	}
};

class DescriptorWatch {
public:
	static DescriptorWatch create(hx_os_loop_watch *&owner, IoLoop &loop,
		int descriptor, int events,
		std::function<void(hx_os_loop_watch *, int)> function)
	{
		if (owner)
			throw StatusError(HX_OS_BUSY);
		if (!function)
			throw StatusError(HX_OS_INVALID);
		auto callback = std::make_shared<detail::DescriptorCallback>(
			std::move(function));
		std::unique_ptr<std::shared_ptr<detail::DescriptorCallback>>
			user(new std::shared_ptr<detail::DescriptorCallback>(
				callback));
		hx_os_error error;
		detail::CheckStatus(
			hx_os_loop_attach_fd_with_cleanup(loop.nativeHandle(),
				descriptor, events, detail::descriptor_ready,
				detail::watch_cleanup<
					detail::DescriptorCallback>,
				user.get(), &owner, &error),
			error);
		(void)user.release();
		return DescriptorWatch(owner, std::move(callback));
	}
	void stop() const noexcept
	{
		hx_os_loop_watch_stop(*m_owner_);
	}
	int descriptor() const noexcept
	{
		return hx_os_loop_watch_descriptor(*m_owner_);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_loop_watch_close_until(m_owner_, deadline, error);
	}
	void rethrowCallbackError() const
	{
		if (*m_owner_) {
			hx_os_error error;
			detail::CheckStatus(
				hx_os_loop_watch_wait_until(
					*m_owner_, HX_OS_DEADLINE_NOW, &error),
				error);
		}
		if (m_callback_->exception)
			std::rethrow_exception(m_callback_->exception);
	}

private:
	hx_os_loop_watch **m_owner_;
	std::shared_ptr<detail::DescriptorCallback> m_callback_;
	DescriptorWatch(hx_os_loop_watch *&owner,
		std::shared_ptr<detail::DescriptorCallback> state)
	    : m_owner_(&owner)
	    , m_callback_(std::move(state))
	{
	}
};
} // namespace hx
#endif
