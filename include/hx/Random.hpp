#ifndef HX_C_PLATFORM_CPP_RANDOM_HPP
#define HX_C_PLATFORM_CPP_RANDOM_HPP
#include <hx_os_util.h>
#include <hx/Values.hpp>
namespace hx {
class Random {
public:
	static Result<void> fill(void *buffer, size_t bytes) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_random_bytes(buffer, bytes, &error);
		return detail::Status(status, error);
	}
	static Result<Buffer> bytes(
		size_t count, hx_os_context *context = NULL) noexcept
	{
		auto result = Buffer::allocate(count, context);
		if (!result)
			return Failure(result.error());
		auto filled =
			fill(result.value().data(), result.value().size());
		if (!filled)
			return Failure(filled.error());
		return result;
	}
	static Result<String> uuid(hx_os_context *context = NULL) noexcept
	{
		return detail::StringQuery(hx_os_uuid_generate_string, context);
	}
};
} // namespace hx
#endif
