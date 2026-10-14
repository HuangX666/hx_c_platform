#ifndef HX_C_PLATFORM_CPP_LEXER_HPP
#define HX_C_PLATFORM_CPP_LEXER_HPP

#include <hx_os_lexer.h>
#include <hx/Values.hpp>

namespace hx {

/**
 * \brief A token owns its UTF-8 text independently of the lexer.
 */
struct Token {
	String text;
	size_t offset, line, column;
	int kind;

	Result<String> value(hx_os_context *context = NULL) const noexcept
	{
		hx_os_token token = {
			{text.data(), text.size()}, offset, line, column, kind};

		return detail::StringQuery(
			[&](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_token_string(&token, out, error);
			},
			context);
	}
};

class Lexer {
public:
	Lexer() noexcept
	    : m_handle_(NULL)
	    , m_context_(NULL)
	{
	}

	static Result<Lexer> create(StringView input,
		hx_os_context *context = NULL,
		const hx_os_lexer_options *options = NULL) noexcept
	{
		Lexer result;
		hx_os_error error = {};

		auto status = hx_os_lexer_create_with_options(context,
			input.native(), options, &result.m_handle_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		result.m_context_ = context;

		return result;
	}

	Lexer(const Lexer &) = delete;

	Lexer &operator=(const Lexer &) = delete;

	Lexer(Lexer &&other) noexcept
	    : m_handle_(other.m_handle_)
	    , m_context_(other.m_context_)
	{
		other.m_handle_ = NULL;
		other.m_context_ = NULL;
	}

	Lexer &operator=(Lexer &&other) noexcept
	{
		std::swap(m_handle_, other.m_handle_);
		std::swap(m_context_, other.m_context_);

		return *this;
	}

	~Lexer() noexcept
	{
		hx_os_lexer_destroy(&m_handle_);
	}

	Result<Token> next() noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		hx_os_token token = {};
		hx_os_error error = {};

		auto status = hx_os_lexer_peek(m_handle_, &token, &error);
		if (status != HX_OS_OK && status != HX_OS_EOF)
			return Failure(status, error);

		auto text = String::copy(StringView(token.text), m_context_);
		if (!text)
			return Failure(text.error());

		// Allocate before advancing so OOM leaves the next token
		// available.
		(void)hx_os_lexer_next(m_handle_, &token, NULL);

		return Result<Token>::withValue(
			Token{std::move(text.value()), token.offset, token.line,
				token.column, token.kind},
			status, error);
	}

	void reset() noexcept
	{
		if (m_handle_)
			hx_os_lexer_reset(m_handle_);
	}

	Result<bool> accept(const char *text) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		int matched = 0;
		hx_os_error error = {};
		auto status =
			hx_os_lexer_accept(m_handle_, text, &matched, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return matched != 0;
	}

	Result<void> expect(const char *text) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_lexer_expect(m_handle_, text, NULL, &error);

		return detail::Status(status, error);
	}

	Result<void> expectKind(int kind) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status =
			hx_os_lexer_expect_kind(m_handle_, kind, NULL, &error);

		return detail::Status(status, error);
	}

	Result<String> readString(hx_os_context *context = NULL) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		return detail::StringQuery(
			[this](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_lexer_read_string(
					m_handle_, out, error);
			},
			context);
	}

	Result<void> skipGroup(size_t maxDepth = 64) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_lexer_skip_group_with_limit(
			m_handle_, maxDepth, &error);

		return detail::Status(status, error);
	}

	Result<void> skipUntil(const char *text) noexcept
	{
		if (!m_handle_)
			return Failure(HX_OS_CLOSED);

		hx_os_error error = {};
		auto status = hx_os_lexer_skip_until(m_handle_, text, &error);

		return detail::Status(status, error);
	}

	hx_os_lexer *native() const noexcept
	{
		return m_handle_;
	}

private:
	hx_os_lexer *m_handle_;
	hx_os_context *m_context_;
};
} // namespace hx
#endif
