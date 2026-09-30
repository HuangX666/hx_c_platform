#ifndef HX_C_PLATFORM_CPP_PROFILER_HPP
#define HX_C_PLATFORM_CPP_PROFILER_HPP
#include <hx_os_profiler.h>
#include <hx/Status.hpp>
#include <memory>
namespace hx {
/* Host owns the handle through all participating threads/TLS retirement.
 * This facade never silently waits or loses a timed-out close handle.
 */
class Profiler {
public:
	explicit Profiler(hx_os_profiler *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	void create(hx_os_context *context = NULL,
		const hx_os_profiler_options *options = NULL)
	{
		if (*m_owner_)
			throw StatusError(HX_OS_BUSY);
		hx_os_error error;
		detail::CheckStatus(hx_os_profiler_create_with_options(
					    context, options, m_owner_, &error),
			error);
	}
	void startRoot(const char *name)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_start_root(*m_owner_, name, &error),
			error);
	}
	void reach(const char *name)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_reach(*m_owner_, name, &error), error);
	}
	void leave()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_leave(*m_owner_, &error), error);
	}
	void restartThread()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_restart_thread(*m_owner_, &error),
			error);
	}
	std::string dump() const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_dump_string(*m_owner_, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	void save(const char *path) const
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_profiler_save(*m_owner_, path, &error), error);
	}
	void stop() noexcept
	{
		hx_os_profiler_stop(*m_owner_);
	}
	hx_os_status closeUntil(
		hx_os_deadline deadline, hx_os_error *error = NULL) noexcept
	{
		return hx_os_profiler_close_until(m_owner_, deadline, error);
	}
	hx_os_profiler *native() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_profiler **m_owner_;
};
/* Thread/fiber-bound, nonmovable lexical scope. Balance only successful
 * entries. Explicit finish reports clock errors; destructor aborts this
 * thread's profiler stack if leave cannot finish, preserving profiler health.
 * The profiler must outlive the scope. External manual restart/leave must
 * not interleave with live scope guards.
 */
class ProfileScope {
public:
	ProfileScope(
		hx_os_profiler *profiler, const char *name, bool root = false)
	    : m_profiler_(profiler)
	    , m_active_(false)
	{
		hx_os_error error;
		auto status = root
			? hx_os_profiler_start_root(profiler, name, &error)
			: hx_os_profiler_reach(profiler, name, &error);
		detail::CheckStatus(status, error);
		m_active_ = true;
	}
	ProfileScope(const ProfileScope &) = delete;
	ProfileScope &operator=(const ProfileScope &) = delete;
	~ProfileScope()
	{
		if (m_active_ &&
			hx_os_profiler_leave(m_profiler_, NULL) != HX_OS_OK)
			(void)hx_os_profiler_restart_thread(m_profiler_, NULL);
	}
	hx_os_status finish(hx_os_error *error = NULL) noexcept
	{
		if (!m_active_) {
			if (error)
				*error = {HX_OS_OK, HX_OS_ERROR_NONE, 0, 0};
			return HX_OS_OK;
		}
		auto status = hx_os_profiler_leave(m_profiler_, error);
		if (status == HX_OS_OK)
			m_active_ = false;
		return status;
	}

private:
	hx_os_profiler *m_profiler_;
	bool m_active_;
};
} // namespace hx
#endif
