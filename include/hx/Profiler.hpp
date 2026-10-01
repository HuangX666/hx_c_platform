#ifndef HX_C_PLATFORM_CPP_PROFILER_HPP
#define HX_C_PLATFORM_CPP_PROFILER_HPP
#include <hx_os_profiler.h>
#include <hx/Values.hpp>
namespace hx {

/* Host owns the profiler through all threads and TLS retirement. */
class Profiler {
public:
	explicit Profiler(hx_os_profiler *&owner) noexcept
	    : m_owner_(&owner)
	{
	}
	Result<void> create(hx_os_context *context = NULL,
		const hx_os_profiler_options *options = NULL) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_profiler_create_with_options(
			context, options, m_owner_, &error);
		return detail::Status(status, error);
	}
	Result<void> startRoot(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_profiler_start_root(*m_owner_, name, &error);
		return detail::Status(status, error);
	}
	Result<void> reach(const char *name) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_profiler_reach(*m_owner_, name, &error);
		return detail::Status(status, error);
	}
	Result<void> leave() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_profiler_leave(*m_owner_, &error);
		return detail::Status(status, error);
	}
	Result<void> restartThread() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_profiler_restart_thread(*m_owner_, &error);
		return detail::Status(status, error);
	}
	Result<String> dump(hx_os_context *context = NULL) const noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_profiler_dump_string(
					*m_owner_, out, error);
			},
			context);
	}
	Result<void> save(const char *path) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_profiler_save(*m_owner_, path, &error);
		return detail::Status(status, error);
	}
	void stop() noexcept
	{
		hx_os_profiler_stop(*m_owner_);
	}
	Result<void> closeUntil(hx_os_deadline deadline) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_profiler_close_until(m_owner_, deadline, &error);
		return detail::Status(status, error);
	}
	hx_os_profiler *native() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_profiler **m_owner_;
};
/* A lexical, nonmovable thread/fiber scope. start() reports entry failure;
 * only successful entries are balanced. The profiler outlives this object.
 * Do not interleave external restart/leave with an active guard.
 */
class ProfileScope {
public:
	explicit ProfileScope(hx_os_profiler *profiler) noexcept
	    : m_profiler_(profiler)
	    , m_active_(false)
	{
	}
	Result<void> start(const char *name, bool root = false) noexcept
	{
		if (m_active_)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = root
			? hx_os_profiler_start_root(m_profiler_, name, &error)
			: hx_os_profiler_reach(m_profiler_, name, &error);
		if (status == HX_OS_OK)
			m_active_ = true;
		return detail::Status(status, error);
	}
	ProfileScope(const ProfileScope &) = delete;
	ProfileScope &operator=(const ProfileScope &) = delete;
	~ProfileScope() noexcept
	{
		if (m_active_ &&
			hx_os_profiler_leave(m_profiler_, NULL) != HX_OS_OK)
			(void)hx_os_profiler_restart_thread(m_profiler_, NULL);
	}
	Result<void> finish() noexcept
	{
		if (!m_active_)
			return Result<void>();
		hx_os_error error = {};
		auto status = hx_os_profiler_leave(m_profiler_, &error);
		if (status == HX_OS_OK)
			m_active_ = false;
		return detail::Status(status, error);
	}

private:
	hx_os_profiler *m_profiler_;
	bool m_active_;
};
} // namespace hx
#endif
