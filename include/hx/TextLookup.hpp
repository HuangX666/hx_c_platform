#ifndef HX_C_PLATFORM_CPP_TEXT_LOOKUP_HPP
#define HX_C_PLATFORM_CPP_TEXT_LOOKUP_HPP
#include <hx_os_text_lookup.h>
#include <hx/Status.hpp>
#include <memory>
namespace hx {
class TextLookup {
public:
	explicit TextLookup(hx_os_context *context = NULL,
		const hx_os_text_lookup_options *options = NULL)
	    : m_handle_(NULL)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_text_lookup_create_with_options(
				context, options, &m_handle_, &error),
			error);
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
		if (this != &other) {
			hx_os_text_lookup_destroy(&m_handle_);
			m_handle_ = other.m_handle_;
			other.m_handle_ = NULL;
		}
		return *this;
	}
	~TextLookup()
	{
		hx_os_text_lookup_destroy(&m_handle_);
	}
	static TextLookup load(const char *path, hx_os_context *context = NULL)
	{
		TextLookup table(context);
		table.addFile(path);
		return table;
	}
	void addFile(const char *path)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_text_lookup_add_file(m_handle_, path, &error),
			error);
	}
	void addText(const std::string &text)
	{
		hx_string_view view = {text.data(), text.size()};
		hx_os_error error;
		detail::CheckStatus(
			hx_os_text_lookup_add_text(m_handle_, view, &error),
			error);
	}
	void add(const char *fallback, const char *translated)
	{
		hx_os_error error;
		detail::CheckStatus(hx_os_text_lookup_add(m_handle_, fallback,
					    translated, &error),
			error);
	}
	std::string get(const char *fallback) const
	{
		hx_string text = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&text, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(hx_os_text_lookup_get_string(
					    m_handle_, fallback, &text, &error),
			error);
		return std::string(hx_string_c_str(&text), text.size);
	}

private:
	hx_os_text_lookup *m_handle_;
};
} // namespace hx
#endif
