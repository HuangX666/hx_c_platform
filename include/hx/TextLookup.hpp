#ifndef HX_C_PLATFORM_CPP_TEXT_LOOKUP_HPP
#define HX_C_PLATFORM_CPP_TEXT_LOOKUP_HPP
#include <hx_os_text_lookup.h>
#include <hx/Values.hpp>
namespace hx {

class TextLookup {
public:
	TextLookup() noexcept
	    : m_handle_(NULL)
	{
	}
	static Result<TextLookup> create(hx_os_context *context = NULL,
		const hx_os_text_lookup_options *options = NULL) noexcept
	{
		TextLookup result;
		hx_os_error error = {};
		auto status = hx_os_text_lookup_create_with_options(
			context, options, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}
	static Result<TextLookup> load(
		const char *path, hx_os_context *context = NULL) noexcept
	{
		TextLookup result;
		hx_os_error error = {};
		auto status = hx_os_text_lookup_load(
			context, path, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}
	TextLookup(const TextLookup &) = delete;
	TextLookup &operator=(const TextLookup &) = delete;
	TextLookup(TextLookup &&other) noexcept
	    : m_handle_(other.m_handle_)
	{
		other.m_handle_ = NULL;
	}
	TextLookup &operator=(TextLookup &&other) noexcept
	{
		std::swap(m_handle_, other.m_handle_);
		return *this;
	}
	~TextLookup() noexcept
	{
		hx_os_text_lookup_destroy(&m_handle_);
	}
	Result<void> addFile(const char *path) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);
		hx_os_error error = {};
		auto status =
			hx_os_text_lookup_add_file(m_handle_, path, &error);
		return detail::Status(status, error);
	}
	Result<void> addText(StringView text) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);
		hx_os_error error = {};
		auto status = hx_os_text_lookup_add_text(
			m_handle_, text.native(), &error);
		return detail::Status(status, error);
	}
	Result<void> add(const char *fallback, const char *translated) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);
		hx_os_error error = {};
		auto status = hx_os_text_lookup_add(
			m_handle_, fallback, translated, &error);
		return detail::Status(status, error);
	}
	Result<String> get(const char *fallback,
		hx_os_context *context = NULL) const noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);
		return detail::StringQuery(
			[=](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_text_lookup_get_string(
					m_handle_, fallback, out, error);
			},
			context);
	}
	Result<StringView> getView(const char *fallback) const & noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);
		hx_string_view text = {};
		hx_os_error error = {};
		auto status = hx_os_text_lookup_get_view(
			m_handle_, fallback, &text, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return StringView(text);
	}
	Result<StringView> getView(const char *) const && = delete;

private:
	hx_os_text_lookup *m_handle_;
};
} // namespace hx
#endif
