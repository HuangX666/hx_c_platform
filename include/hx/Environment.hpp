#ifndef HX_C_PLATFORM_CPP_ENVIRONMENT_HPP
#define HX_C_PLATFORM_CPP_ENVIRONMENT_HPP

#include <hx_os_env.h>
#include <hx/Values.hpp>

namespace hx {
class Environment {
public:
	static Result<String> get(
		const char *name, hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_env_get_string(name, out, error);
			},
			context);
	}

	static Result<String> getOr(const char *name, StringView fallback,
		hx_os_context *context = NULL) noexcept
	{
		auto result = get(name, context);
		if (result.status() == HX_OS_NOT_FOUND)
			return String::copy(fallback, context);

		return result;
	}

	static Result<void> set(const char *name, const char *value,
		bool replace = true) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_env_set(name, value, replace, &error);

		return detail::Status(status, error);
	}

	static Result<void> remove(const char *name) noexcept
	{
		hx_os_error error = {};

		auto status = hx_os_env_remove(name, &error);

		return detail::Status(status, error);
	}
};
} // namespace hx
#endif
