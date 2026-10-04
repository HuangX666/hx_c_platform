#ifndef HX_C_PLATFORM_CPP_SYSTEM_INFO_HPP
#define HX_C_PLATFORM_CPP_SYSTEM_INFO_HPP

#include <hx_os_sysinfo.h>
#include <hx/Values.hpp>

namespace hx {
class SystemInfo {
public:
	static Result<uint64_t> totalMemory() noexcept
	{
		uint64_t value{};
		hx_os_error error = {};

		hx_os_status status = hx_os_sys_total_memory(&value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<uint64_t> availableMemory() noexcept
	{
		uint64_t value{};
		hx_os_error error = {};
		hx_os_status status =
			hx_os_sys_available_memory(&value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<int> logicalCores() noexcept
	{
		int value{};
		hx_os_error error = {};

		hx_os_status status = hx_os_sys_logical_cores(&value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<int> physicalCores() noexcept
	{
		int value{};
		hx_os_error error = {};

		hx_os_status status = hx_os_sys_physical_cores(&value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<size_t> pageSize() noexcept
	{
		size_t value{};
		hx_os_error error = {};

		hx_os_status status = hx_os_sys_page_size(&value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<hx_os_sys_disk_space_info> diskSpace(
		const char *path) noexcept
	{
		hx_os_sys_disk_space_info value =
			HX_OS_SYS_DISK_SPACE_INFO_INIT;
		hx_os_error error = {};

		auto status = hx_os_sys_disk_space(path, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<hx_os_sys_proc_memory_info> processMemory() noexcept
	{
		hx_os_sys_proc_memory_info value =
			HX_OS_SYS_PROC_MEMORY_INFO_INIT;
		hx_os_error error = {};

		auto status = hx_os_sys_proc_memory(&value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<String> version(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_sys_version_string, context);
	}
};

class CpuUsage {
public:
	CpuUsage() noexcept
	    : m_usage_(NULL)
	{
	}

	static Result<CpuUsage> create(hx_os_context *context = NULL) noexcept
	{
		CpuUsage result;
		hx_os_error error = {};

		auto status = hx_os_cpu_usage_create(
			context, &result.m_usage_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
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

	~CpuUsage() noexcept
	{
		hx_os_cpu_usage_destroy(&m_usage_);
	}

	Result<double> query() noexcept
	{
		if (!m_usage_)
			return Failure(HX_OS_CLOSED);

		double value = 0;
		hx_os_error error = {};

		auto status = hx_os_cpu_usage_query(m_usage_, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

private:
	hx_os_cpu_usage *m_usage_;
};
} // namespace hx
#endif
