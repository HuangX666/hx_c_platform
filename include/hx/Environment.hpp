#ifndef HX_C_PLATFORM_CPP_ENVIRONMENT_HPP
#define HX_C_PLATFORM_CPP_ENVIRONMENT_HPP

#include <hx_os_env.h>
#include <hx/Status.hpp>
#include <memory>
#include <string>

namespace hx {

class Environment {
public:
	static std::string get(const char *name)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_env_get_string(name, &text, &error), error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static std::string getOr(const char *name, const std::string &fallback)
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		hx_os_status status = hx_os_env_get_string(name, &text, &error);
		if (status == HX_OS_NOT_FOUND)
			return fallback;
		detail::CheckStatus(status, error);
		return std::string(hx_string_c_str(&text), text.size);
	}
	static void set(
		const char *name, const char *value, bool replace = true)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_env_set(name, value, replace, &error), error);
	}
	static void remove(const char *name)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_env_remove(name, &error), error);
	}
};

} // namespace hx
#endif
