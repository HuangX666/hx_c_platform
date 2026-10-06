#ifndef HX_C_PLATFORM_CPP_TEXT_HPP
#define HX_C_PLATFORM_CPP_TEXT_HPP

#include <hx_os_text.h>
#include <hx/Values.hpp>

namespace hx {
class WideString {
public:
	WideString() noexcept = default;
	WideString(WideString &&) noexcept = default;
	WideString &operator=(WideString &&) noexcept = default;
	WideString(const WideString &) = delete;
	WideString &operator=(const WideString &) = delete;

	const wchar_t *c_str() const noexcept
	{
		return m_buffer_.size()
			? reinterpret_cast<const wchar_t *>(m_buffer_.data())
			: L"";
	}

	size_t size() const noexcept
	{
		return m_buffer_.size() ? m_buffer_.size() / sizeof(wchar_t) - 1
					: 0;
	}

private:
	friend class Text;
	Buffer m_buffer_;

	explicit WideString(Buffer &&buffer) noexcept
	    : m_buffer_(std::move(buffer))
	{
	}
};

class Text {
public:
	static Result<String> fromWideUnits(const wchar_t *text, size_t units,
		hx_os_context *context = NULL) noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_string_assign_wide(
			result.native(), text, units, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<String> fromWide(
		const wchar_t *text, hx_os_context *context = NULL) noexcept
	{
		return fromWideUnits(text, text ? wcslen(text) : 0, context);
	}

	static Result<WideString> toWide(
		StringView text, hx_os_context *context = NULL) noexcept
	{
		hx_os_error error = {};
		size_t units = 0;
		auto status = hx_os_text_to_wide(
			text.data(), text.size(), NULL, 0, &units, &error);
		if (status != HX_OS_OUTPUT_FULL)
			return Failure(status, error);

		auto buffer =
			Buffer::allocate(units * sizeof(wchar_t), context);
		if (!buffer)
			return Failure(buffer.error());

		status = hx_os_text_to_wide(text.data(), text.size(),
			reinterpret_cast<wchar_t *>(buffer.value().data()),
			units, &units, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return WideString(std::move(buffer.value()));
	}

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
