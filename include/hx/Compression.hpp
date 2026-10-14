#ifndef HX_C_PLATFORM_CPP_COMPRESSION_HPP
#define HX_C_PLATFORM_CPP_COMPRESSION_HPP

#include <hx_os_compression.h>
#include <hx/Bytes.hpp>

namespace hx {
class Compression {
public:
	static Result<Bytes> compress(unsigned format, hx_os_bytes_view input,
		const hx_os_compression_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return convert(format, input, options, context, false);
	}

	static Result<Bytes> decompress(unsigned format, hx_os_bytes_view input,
		const hx_os_compression_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return convert(format, input, options, context, true);
	}

private:
	static Result<Bytes> convert(unsigned format, hx_os_bytes_view input,
		const hx_os_compression_options *options,
		hx_os_context *context, bool decode) noexcept
	{
		Bytes result(context);
		hx_os_error error = {};
		auto status = decode ? hx_os_decompress(format, input, options,
					       result.native(), &error)
				     : hx_os_compress(format, input, options,
					       result.native(), &error);

		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}
};
} // namespace hx
#endif
