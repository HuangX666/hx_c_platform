#ifndef HX_C_PLATFORM_CPP_HTTP_HPP
#define HX_C_PLATFORM_CPP_HTTP_HPP

#include <hx_os_http.h>
#include <hx/Values.hpp>

namespace hx {
class HttpResponse {
public:
	explicit HttpResponse(hx_os_context *context = NULL) noexcept
	{
		hx_os_http_response_init(&m_response_, context);
	}

	HttpResponse(HttpResponse &&other) noexcept
	    : m_response_(other.m_response_)
	{
		other.m_response_ = HX_OS_HTTP_RESPONSE_INIT;
	}

	HttpResponse &operator=(HttpResponse &&other) noexcept
	{
		std::swap(m_response_, other.m_response_);
		return *this;
	}

	HttpResponse(const HttpResponse &) = delete;
	HttpResponse &operator=(const HttpResponse &) = delete;

	~HttpResponse() noexcept
	{
		hx_os_http_response_destroy(&m_response_);
	}

	unsigned statusCode() const noexcept
	{
		return m_response_.status_code;
	}

	hx_os_bytes_view body() const noexcept
	{
		return hx_bytes_view(&m_response_.body);
	}

	hx_os_bytes_view headers() const noexcept
	{
		return hx_bytes_view(&m_response_.headers);
	}

	const char *url() const noexcept
	{
		return hx_string_c_str(&m_response_.effective_url);
	}

	hx_os_http_response *native() noexcept
	{
		return &m_response_;
	}

	Result<String> text(hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_http_response_text(
			&m_response_, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

private:
	hx_os_http_response m_response_;
};

class Http {
public:
	Http() noexcept
	    : m_http_(NULL)
	{
	}

	Http(Http &&other) noexcept
	    : m_http_(other.m_http_)
	{
		other.m_http_ = NULL;
	}

	Http &operator=(Http &&other) noexcept
	{
		std::swap(m_http_, other.m_http_);
		return *this;
	}

	Http(const Http &) = delete;
	Http &operator=(const Http &) = delete;

	~Http() noexcept
	{
		(void)hx_os_http_close(&m_http_, NULL);
	}

	static Result<Http> create(hx_os_context *context = NULL) noexcept
	{
		Http result;
		hx_os_error error = {};
		auto status =
			hx_os_http_create(context, &result.m_http_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<HttpResponse> get(const char *url,
		const hx_os_http_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		return request("GET", url, {NULL, 0}, options, context);
	}

	Result<HttpResponse> request(const char *method, const char *url,
		hx_os_bytes_view body, const hx_os_http_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		HttpResponse result(context);
		hx_os_error error = {};
		auto status = hx_os_http_request(m_http_, method, url, body,
			NULL, 0, options, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<HttpResponse> postJson(const char *url, const char *json,
		const hx_os_http_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		HttpResponse result(context);
		hx_os_error error = {};
		auto status = hx_os_http_post_json(m_http_, url,
			hx_string_view_from_cstr(json), options,
			result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<HttpResponse> downloadFile(const char *url, const char *path,
		bool replace = false,
		const hx_os_http_stream_options *options = NULL) noexcept
	{
		HttpResponse result;
		hx_os_error error = {};
		auto status = hx_os_http_download_file(m_http_, url, path,
			replace ? 1 : 0, options, result.native(), &error);

		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<HttpResponse> uploadFile(const char *url, const char *path,
		const hx_os_http_stream_options *options = NULL) noexcept
	{
		HttpResponse result;
		hx_os_error error = {};
		auto status = hx_os_http_upload_file(m_http_, "PUT", url, path,
			NULL, 0, options, result.native(), &error);

		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	const char *pendingFile() const noexcept
	{
		return hx_os_http_pending_file(m_http_);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_http_close(&m_http_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_http *m_http_;
};
} // namespace hx
#endif
