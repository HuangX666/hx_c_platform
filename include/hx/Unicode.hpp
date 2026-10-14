#ifndef HX_C_PLATFORM_CPP_UNICODE_HPP
#define HX_C_PLATFORM_CPP_UNICODE_HPP

#include <hx_unicode.h>
#include <hx/Values.hpp>

namespace hx {
class Unicode {
public:
	static Result<String> normalize(StringView input,
		unsigned form = HX_OS_UNICODE_NFC,
		hx_os_context *context = NULL) noexcept
	{
		String output(context);
		hx_os_error error = {};
		auto status = hx_os_unicode_normalize(
			hx_string_view{input.data(), input.size()}, form,
			output.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return output;
	}

	static Result<String> casefold(
		StringView input, hx_os_context *context = NULL) noexcept
	{
		String output(context);
		hx_os_error error = {};
		auto status = hx_os_unicode_casefold(
			hx_string_view{input.data(), input.size()},
			output.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return output;
	}
};
} // namespace hx
#endif
