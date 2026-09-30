#ifndef HX_C_PLATFORM_CPP_TEXT_HPP
#define HX_C_PLATFORM_CPP_TEXT_HPP

#include <hx_os_text.h>
#include <hx/Status.hpp>
#include <vector>

namespace hx {
class Text {
public:
	struct Bom {
		size_t bytes;
		hx_os_encoding encoding;
		bool found;
	};
	/* encoding is meaningful only when found is true. */
	static Bom detectBom(const void *data, size_t bytes)
	{
		Bom result = {0, HX_OS_UTF8, false};
		hx_os_status status = hx_os_text_bom_detect(
			data, bytes, &result.encoding, &result.bytes);
		if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
			throw StatusError(status);
		result.found = status == HX_OS_OK;
		return result;
	}
	static std::vector<unsigned char> bomBytes(hx_os_encoding encoding)
	{
		unsigned char buffer[4];
		size_t bytes;
		hx_os_status status =
			hx_os_text_bom_bytes(encoding, buffer, &bytes);
		if (status != HX_OS_OK)
			throw StatusError(status);
		return std::vector<unsigned char>(buffer, buffer + bytes);
	}
};
} // namespace hx
#endif
