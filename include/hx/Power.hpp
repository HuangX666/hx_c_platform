#ifndef HX_C_PLATFORM_CPP_POWER_HPP
#define HX_C_PLATFORM_CPP_POWER_HPP
#include <hx_os_power.h>
#include <hx/Status.hpp>
namespace hx {
class SleepInhibit {
public:
	SleepInhibit() noexcept
	    : m_handle_(NULL)
	{
	}
	static Result<SleepInhibit> create(
		const char *reason, hx_os_context *context = NULL) noexcept
	{
		SleepInhibit result;
		hx_os_error error = {};
		auto status = hx_os_sleep_inhibit_create(
			context, reason, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
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
		std::swap(m_handle_, other.m_handle_);
		return *this;
	}
	~SleepInhibit() noexcept
	{
		(void)hx_os_sleep_inhibit_destroy(&m_handle_, NULL);
	}
	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_sleep_inhibit_destroy(&m_handle_, &error);
		return detail::Status(status, error);
	}
	Result<void> setActive(bool active) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_sleep_inhibit_set_active(
			m_handle_, active ? 1 : 0, &error);
		return detail::Status(status, error);
	}
	bool active() const noexcept
	{
		return hx_os_sleep_inhibit_active(m_handle_);
	}

private:
	hx_os_sleep_inhibit *m_handle_;
};
class PerformanceRequest {
public:
	PerformanceRequest() noexcept
	    : m_handle_(NULL)
	{
	}
	static Result<PerformanceRequest> create(
		const char *reason, hx_os_context *context = NULL) noexcept
	{
		PerformanceRequest result;
		hx_os_error error = {};
		auto status = hx_os_performance_request(
			context, reason, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
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
		std::swap(m_handle_, other.m_handle_);
		return *this;
	}
	~PerformanceRequest() noexcept
	{
		(void)hx_os_performance_release(&m_handle_, NULL);
	}
	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_performance_release(&m_handle_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_performance_token *m_handle_;
};
} // namespace hx
#endif
