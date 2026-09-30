#ifndef HX_C_PLATFORM_CPP_PATHS_HPP
#define HX_C_PLATFORM_CPP_PATHS_HPP

#include <hx_os_paths.h>
#include <hx/Status.hpp>
#include <memory>
#include <string>

namespace hx {
class Paths {
public:
	static std::string home()
	{
		return query(hx_os_path_home_string);
	}
	static std::string configHome()
	{
		return query(hx_os_path_config_home_string);
	}
	static std::string dataHome()
	{
		return query(hx_os_path_data_home_string);
	}
	static std::string programData()
	{
		return query(hx_os_path_program_data_string);
	}
	static std::string temp()
	{
		return query(hx_os_path_temp_string);
	}
	static std::string executable()
	{
		return query(hx_os_path_executable_string);
	}
	static std::string module()
	{
		return query(hx_os_path_module_string);
	}

private:
	static std::string query(
		hx_os_status(HX_OS_CALL *function)(hx_string *, hx_os_error *))
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(function(&text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
};
} // namespace hx
#endif
