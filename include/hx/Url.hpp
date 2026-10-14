#ifndef HX_C_PLATFORM_CPP_URL_HPP
#define HX_C_PLATFORM_CPP_URL_HPP

#include <hx_os_url.h>
#include <hx/Values.hpp>

namespace hx {
class Url {
public:
	Url() noexcept
	    : m_url_(NULL)
	{
	}

	Url(Url &&other) noexcept
	    : m_url_(other.m_url_)
	{
		other.m_url_ = NULL;
	}

	Url &operator=(Url &&other) noexcept
	{
		std::swap(m_url_, other.m_url_);
		return *this;
	}

	Url(const Url &) = delete;
	Url &operator=(const Url &) = delete;

	~Url() noexcept
	{
		hx_os_url_destroy(&m_url_);
	}

	static Result<Url> parse(
		const char *text, hx_os_context *context = NULL) noexcept
	{
		Url result;
		hx_os_error error = {};
		auto status =
			hx_os_url_parse(context, text, &result.m_url_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<String> part(unsigned part = HX_OS_URL_FULL,
		hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status =
			hx_os_url_part(m_url_, part, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<String> query(
		const char *key, hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_url_query_get(m_url_,
			hx_string_view_from_cstr(key), result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<void> setQuery(const char *key, const char *value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_url_query_set(m_url_,
			hx_string_view_from_cstr(key),
			hx_string_view_from_cstr(value), &error);
		return detail::Status(status, error);
	}

	Result<void> appendQuery(const char *key, const char *value) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_url_query_append(m_url_,
			hx_string_view_from_cstr(key),
			hx_string_view_from_cstr(value), &error);
		return detail::Status(status, error);
	}

private:
	hx_os_url *m_url_;
};
} // namespace hx
#endif
