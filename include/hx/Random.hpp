#ifndef HX_C_PLATFORM_CPP_RANDOM_HPP
#define HX_C_PLATFORM_CPP_RANDOM_HPP

#include <hx_os_util.h>
#include <hx/Status.hpp>
#include <vector>

namespace hx {
class Random {
public:
	static void fill(void *buffer, size_t bytes)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_random_bytes(buffer, bytes, &error), error);
	}
	static std::vector<unsigned char> bytes(size_t count)
	{
		std::vector<unsigned char> result(count);
		fill(result.data(), result.size());
		return result;
	}
	static std::string uuid()
	{
		unsigned char value[16];
		char text[37];
		hx_os_error error;
		detail::CheckStatus(hx_os_uuid_generate(value, &error), error);
		(void)hx_os_uuid_format(value, text);
		return text;
	}
};
} // namespace hx
#endif
