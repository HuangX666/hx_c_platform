#ifndef HX_C_PLATFORM_CPP_SYSTEM_INFO_HPP
#define HX_C_PLATFORM_CPP_SYSTEM_INFO_HPP

#include <hx_os_sysinfo.h>
#include <hx/Status.hpp>
#include <string>
#include <utility>

namespace hx {

class SystemInfo {
public:
	static uint64_t totalMemory()
	{
		uint64_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_total_memory(&value, &error), error);
		return value;
	}
	static uint64_t availableMemory()
	{
		uint64_t value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_available_memory(&value, &error), error);
		return value;
	}
	static int logicalCores()
	{
		int value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_logical_cores(&value, &error), error);
		return value;
	}
	static int physicalCores()
	{
		int value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_physical_cores(&value, &error), error);
		return value;
	}
	static size_t pageSize()
	{
		size_t value;
		hx_os_error error;
		detail::CheckStatus(hx_os_sys_page_size(&value, &error), error);
		return value;
	}
	static hx_os_sys_disk_space_info diskSpace(const char *path)
	{
		hx_os_sys_disk_space_info value =
			HX_OS_SYS_DISK_SPACE_INFO_INIT;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_disk_space(path, &value, &error), error);
		return value;
	}
	static hx_os_sys_proc_memory_info processMemory()
	{
		hx_os_sys_proc_memory_info value =
			HX_OS_SYS_PROC_MEMORY_INFO_INIT;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sys_proc_memory(&value, &error), error);
		return value;
	}
	static std::string version()
	{
		char text[256];
		size_t required;
		hx_os_error error;
		detail::CheckStatus(hx_os_sys_version(text, sizeof(text),
					    &required, &error),
			error);
		return std::string(text, required - 1);
	}
};

class CpuUsage {
public:
	explicit CpuUsage(hx_os_context *context = NULL)
	    : m_usage_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_cpu_usage_create(context, &m_usage_, &error),
			error);
	}
	CpuUsage(const CpuUsage &) = delete;
	CpuUsage &operator=(const CpuUsage &) = delete;
	CpuUsage(CpuUsage &&other) noexcept
	    : m_usage_(other.m_usage_)
	{
		other.m_usage_ = NULL;
	}
	CpuUsage &operator=(CpuUsage &&other) noexcept
	{
		std::swap(m_usage_, other.m_usage_);
		return *this;
	}
	~CpuUsage()
	{
		hx_os_cpu_usage_destroy(&m_usage_);
	}
	double query()
	{
		double value;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_cpu_usage_query(m_usage_, &value, &error), error);
		return value;
	}

private:
	hx_os_cpu_usage *m_usage_;
};

} // namespace hx
#endif
