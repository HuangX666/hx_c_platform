#ifndef HX_C_PLATFORM_CPP_PATHS_HPP
#define HX_C_PLATFORM_CPP_PATHS_HPP
#include <hx_os_paths.h>
#include <hx/Values.hpp>
namespace hx {
class Paths {
public:
	static Result<String> home(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_path_home_string, context);
	}
	static Result<String> configHome(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			hx_os_path_config_home_string, context);
	}
	static Result<String> dataHome(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			hx_os_path_data_home_string, context);
	}
	static Result<String> programData(
		hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			hx_os_path_program_data_string, context);
	}
	static Result<String> temp(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_path_temp_string, context);
	}
	static Result<String> executable(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			hx_os_path_executable_string, context);
	}
	static Result<String> module(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_path_module_string, context);
	}
	static Result<String> configFile(const char *application,
		const char *name, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_path_config_file(
					application, name, out, error);
			},
			context);
	}
};
} // namespace hx
#endif
