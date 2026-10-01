#ifndef HX_C_PLATFORM_CPP_LEXER_HPP
#define HX_C_PLATFORM_CPP_LEXER_HPP
#include <hx_os_lexer.h>
#include <hx/Status.hpp>
#include <memory>
#include <utility>
namespace hx {
/* C++ tokens own their text and remain valid after the lexer is destroyed. */
struct Token {
	std::string text;
	size_t offset, line, column;
	int kind;
	std::string value() const
	{
		hx_os_token token = {
			{text.data(), text.size()}, offset, line, column, kind};
		hx_string decoded = HX_STRING_INIT;
		std::unique_ptr<hx_string, decltype(&hx_string_destroy)>
			cleanup(&decoded, hx_string_destroy);
		hx_os_error error;
		detail::CheckStatus(
			hx_os_token_string(&token, &decoded, &error), error);
		return std::string(hx_string_c_str(&decoded), decoded.size);
	}
};
class Lexer {
public:
	explicit Lexer(const std::string &input, hx_os_context *context = NULL,
		const hx_os_lexer_options *options = NULL)
	    : m_handle_(NULL)
	{
		hx_string_view view = {input.data(), input.size()};
		hx_os_error error;
		detail::CheckStatus(hx_os_lexer_create_with_options(context,
					    view, options, &m_handle_, &error),
			error);
	}
	Lexer(const Lexer &) = delete;
	Lexer &operator=(const Lexer &) = delete;
	Lexer(Lexer &&other) noexcept
	    : m_handle_(other.m_handle_)
	{
		other.m_handle_ = NULL;
	}
	Lexer &operator=(Lexer &&other) noexcept
	{
		if (this != &other) {
			hx_os_lexer_destroy(&m_handle_);
			m_handle_ = other.m_handle_;
			other.m_handle_ = NULL;
		}
		return *this;
	}
	~Lexer()
	{
		hx_os_lexer_destroy(&m_handle_);
	}
	bool next(Token &out)
	{
		hx_os_token token;
		hx_os_error error;
		auto status = hx_os_lexer_peek(m_handle_, &token, &error);
		if (status != HX_OS_OK && status != HX_OS_EOF)
			detail::CheckStatus(status, error);
		Token result = {std::string(token.text.data, token.text.size),
			token.offset, token.line, token.column, token.kind};
		/* Allocation succeeded before advancing the C cursor. */
		(void)hx_os_lexer_next(m_handle_, &token, NULL);
		out = std::move(result);
		return status == HX_OS_OK;
	}
	void reset() noexcept
	{
		hx_os_lexer_reset(m_handle_);
	}

private:
	hx_os_lexer *m_handle_;
};
} // namespace hx
#endif
