#ifndef HX_C_PLATFORM_CPP_TEXT_HPP
#define HX_C_PLATFORM_CPP_TEXT_HPP
#include <hx_os_text.h>
#include <hx/Values.hpp>
namespace hx {
class Text {
public:
	static Result<double> parseDouble(StringView text) noexcept
	{
		double value = 0;
		hx_os_error error = {};
		auto status = hx_os_text_parse_double(
			text.data(), text.size(), &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return value;
	}
	static Result<String> formatDouble(
		double value, hx_os_context *context = NULL) noexcept
	{
		char buffer[128];
		size_t required = 0;
		auto status = hx_os_text_format_double(
			value, buffer, sizeof(buffer), &required);
		if (status != HX_OS_OK)
			return Failure(status);
		return String::copy(StringView(buffer, required - 1), context);
	}
	struct Bom {
		size_t bytes;
		hx_os_encoding encoding;
		bool found;
	};
	static Result<Bom> detectBom(const void *data, size_t bytes) noexcept
	{
		Bom result = {0, HX_OS_UTF8, false};
		auto status = hx_os_text_bom_detect(
			data, bytes, &result.encoding, &result.bytes);
		if (status != HX_OS_OK && status != HX_OS_NOT_FOUND)
			return Failure(status);
		result.found = status == HX_OS_OK;
		return result;
	}
	static Result<Buffer> bomBytes(
		hx_os_encoding encoding, hx_os_context *context = NULL) noexcept
	{
		unsigned char buffer[4];
		size_t bytes = 0;
		auto status = hx_os_text_bom_bytes(encoding, buffer, &bytes);
		if (status != HX_OS_OK)
			return Failure(status);
		auto result = Buffer::allocate(bytes, context);
		if (result && bytes)
			std::memcpy(result.value().data(), buffer, bytes);
		return result;
	}
};
} // namespace hx
#endif
