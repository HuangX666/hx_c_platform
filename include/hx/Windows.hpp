#ifndef HX_C_PLATFORM_CPP_WINDOWS_HPP
#define HX_C_PLATFORM_CPP_WINDOWS_HPP

#include <hx_os_windows.h>
#include <hx/Values.hpp>

namespace hx {

class Windows {
public:
	static Result<hx_os_windows_version_info> fileVersion(
		const char *path) noexcept
	{
		return query<hx_os_windows_version_info>(
			hx_os_windows_version_query, path);
	}

	static Result<String> versionString(const char *path, const char *field,
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[path, field](
				hx_string *out, hx_os_error *error) noexcept {
				return hx_os_windows_version_string(
					path, field, out, error);
			},
			context);
	}

	static Result<hx_os_windows_window_info> window(
		uintptr_t handle) noexcept
	{
		return query<hx_os_windows_window_info>(
			hx_os_windows_window_query, handle);
	}

	static Result<hx_os_windows_monitor_info> monitor(
		uintptr_t handle) noexcept
	{
		return query<hx_os_windows_monitor_info>(
			hx_os_windows_monitor_query, handle);
	}

	static Result<hx_os_windows_gpu_info> gpu(uint32_t index = 0) noexcept
	{
		return query<hx_os_windows_gpu_info>(
			hx_os_windows_gpu_query, index);
	}

	static Result<hx_os_windows_gpu_info> monitorGpu(
		uintptr_t handle) noexcept
	{
		return query<hx_os_windows_gpu_info>(
			hx_os_windows_monitor_gpu_query, handle);
	}

	static Result<String> windowTitle(
		uintptr_t handle, hx_os_context *context = NULL) noexcept
	{
		return text(hx_os_windows_window_title, handle, context);
	}

	static Result<String> windowClass(
		uintptr_t handle, hx_os_context *context = NULL) noexcept
	{
		return text(hx_os_windows_window_class, handle, context);
	}

	static Result<String> windowProcessPath(
		uintptr_t handle, hx_os_context *context = NULL) noexcept
	{
		return text(hx_os_windows_window_process_path, handle, context);
	}

private:
	template <typename T, typename F, typename A>
	static Result<T> query(F function, A argument) noexcept
	{
		T value = {};
		hx_os_error error = {};

		auto status = function(argument, &value, sizeof(value), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	template <typename F>
	static Result<String> text(
		F function, uintptr_t handle, hx_os_context *context) noexcept
	{
		return detail::StringQuery(
			[function, handle](
				hx_string *out, hx_os_error *error) noexcept {
				return function(handle, out, error);
			},
			context);
	}
};
} // namespace hx
#endif
