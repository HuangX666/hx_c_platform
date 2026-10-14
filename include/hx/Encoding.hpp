#ifndef HX_C_PLATFORM_CPP_ENCODING_HPP
#define HX_C_PLATFORM_CPP_ENCODING_HPP

#include <hx_encoding.h>
#include <hx/Values.hpp>

namespace hx {
class Encoding {
public:
	static Result<String> base64(const void *data, size_t bytes,
		uint32_t flags = 0, hx_os_context *context = NULL) noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_base64_encode_string(
			data, bytes, flags, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<String> hex(const void *data, size_t bytes,
		uint32_t flags = 0, hx_os_context *context = NULL) noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_hex_encode_string(
			data, bytes, flags, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<Buffer> fromBase64(StringView text, uint32_t flags = 0,
		hx_os_context *context = NULL) noexcept
	{
		return decode(text, flags, false, context);
	}

	static Result<Buffer> fromHex(
		StringView text, hx_os_context *context = NULL) noexcept
	{
		return decode(text, 0, true, context);
	}

private:
	static Result<Buffer> decode(StringView text, uint32_t flags, bool hex,
		hx_os_context *context) noexcept
	{
		size_t bytes = 0;
		hx_os_error error = {};
		auto status = hex
			? hx_os_hex_decode(text.data(), text.size(), NULL, 0,
				  &bytes, &error)
			: hx_os_base64_decode(text.data(), text.size(), flags,
				  NULL, 0, &bytes, &error);
		if (status != HX_OS_OK && status != HX_OS_OUTPUT_FULL)
			return Failure(status, error);
		if (bytes > 16 * 1024 * 1024)
			return Failure(HX_OS_LIMIT);

		auto result = Buffer::allocate(bytes, context);
		if (!result)
			return Failure(result.error());

		status = hex
			? hx_os_hex_decode(text.data(), text.size(),
				  result.value().data(), bytes, &bytes, &error)
			: hx_os_base64_decode(text.data(), text.size(), flags,
				  result.value().data(), bytes, &bytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}
};
} // namespace hx
#endif
