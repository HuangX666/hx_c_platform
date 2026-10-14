#ifndef HX_C_PLATFORM_CPP_LINE_READER_HPP
#define HX_C_PLATFORM_CPP_LINE_READER_HPP

#include <hx_line_reader.h>
#include <hx/Values.hpp>

namespace hx {
class LineReader {
public:
	LineReader() noexcept
	    : m_reader_(NULL)
	{
	}

	LineReader(LineReader &&other) noexcept
	    : m_reader_(other.m_reader_)
	{
		other.m_reader_ = NULL;
	}

	LineReader &operator=(LineReader &&other) noexcept
	{
		std::swap(m_reader_, other.m_reader_);

		return *this;
	}

	LineReader(const LineReader &) = delete;
	LineReader &operator=(const LineReader &) = delete;

	~LineReader() noexcept
	{
		(void)hx_os_line_reader_close(&m_reader_, NULL);
	}

	static Result<LineReader> open(const char *path,
		size_t maxLineBytes = 0, hx_os_context *context = NULL) noexcept
	{
		LineReader reader;
		hx_os_error error = {};
		auto status = hx_os_line_reader_open(
			context, path, maxLineBytes, &reader.m_reader_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return reader;
	}

	static Result<LineReader> borrow(hx_os_stream *stream,
		size_t maxLineBytes = 0, hx_os_context *context = NULL) noexcept
	{
		LineReader reader;
		hx_os_error error = {};
		auto status = hx_os_line_reader_from_stream(context, stream,
			maxLineBytes, &reader.m_reader_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return reader;
	}

	Result<bool> next(String &line) noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_line_reader_next(
			m_reader_, line.native(), &error);
		if (status == HX_OS_EOF)
			return false;
		if (status != HX_OS_OK)
			return Failure(status, error);

		return true;
	}

	Result<void> skipLine() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_line_reader_skip_line(m_reader_, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_line_reader_close(&m_reader_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_line_reader *m_reader_;
};
} // namespace hx
#endif
