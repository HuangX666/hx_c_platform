#ifndef HX_C_PLATFORM_CPP_POWER_HPP
#define HX_C_PLATFORM_CPP_POWER_HPP
#include <hx_os_power.h>
#include <hx/Status.hpp>
namespace hx {
/* Synchronous RAII. Explicit close reports cleanup errors; destructors
 * release only their own native requests without changing another token.
 */
class SleepInhibit {
public:
	explicit SleepInhibit(const char *reason, hx_os_context *context = NULL)
	    : m_handle_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_sleep_inhibit_create(context, reason,
					    &m_handle_, &error),
			error);
	}
	SleepInhibit(const SleepInhibit &) = delete;
	SleepInhibit &operator=(const SleepInhibit &) = delete;
	SleepInhibit(SleepInhibit &&other) noexcept
	    : m_handle_(other.m_handle_)
	{
		other.m_handle_ = NULL;
	}
	SleepInhibit &operator=(SleepInhibit &&other) noexcept
	{
		if (this != &other) {
			(void)hx_os_sleep_inhibit_destroy(&m_handle_, NULL);
			m_handle_ = other.m_handle_;
			other.m_handle_ = NULL;
		}
		return *this;
	}
	~SleepInhibit()
	{
		(void)hx_os_sleep_inhibit_destroy(&m_handle_, NULL);
	}
	void setActive(bool active)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_sleep_inhibit_set_active(
					    m_handle_, active ? 1 : 0, &error),
			error);
	}
	bool active() const noexcept
	{
		return hx_os_sleep_inhibit_active(m_handle_);
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sleep_inhibit_destroy(&m_handle_, &error), error);
	}

private:
	hx_os_sleep_inhibit *m_handle_;
};
class PerformanceRequest {
public:
	explicit PerformanceRequest(
		const char *reason, hx_os_context *context = NULL)
	    : m_handle_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_performance_request(context, reason,
					    &m_handle_, &error),
			error);
	}
	PerformanceRequest(const PerformanceRequest &) = delete;
	PerformanceRequest &operator=(const PerformanceRequest &) = delete;
	PerformanceRequest(PerformanceRequest &&other) noexcept
	    : m_handle_(other.m_handle_)
	{
		other.m_handle_ = NULL;
	}
	PerformanceRequest &operator=(PerformanceRequest &&other) noexcept
	{
		if (this != &other) {
			(void)hx_os_performance_release(&m_handle_, NULL);
			m_handle_ = other.m_handle_;
			other.m_handle_ = NULL;
		}
		return *this;
	}
	~PerformanceRequest()
	{
		(void)hx_os_performance_release(&m_handle_, NULL);
	}
	void close()
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_performance_release(&m_handle_, &error), error);
	}

private:
	hx_os_performance_token *m_handle_;
};
} // namespace hx
#endif
