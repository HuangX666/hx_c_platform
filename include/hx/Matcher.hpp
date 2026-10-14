#ifndef HX_C_PLATFORM_CPP_MATCHER_HPP
#define HX_C_PLATFORM_CPP_MATCHER_HPP

#include <hx_search.h>
#include <hx/Status.hpp>

namespace hx {
class Matcher {
public:
	Matcher() noexcept
	    : m_matcher_(NULL)
	{
	}

	Matcher(Matcher &&other) noexcept
	    : m_matcher_(other.m_matcher_)
	{
		other.m_matcher_ = NULL;
	}

	Matcher &operator=(Matcher &&other) noexcept
	{
		std::swap(m_matcher_, other.m_matcher_);
		return *this;
	}

	Matcher(const Matcher &) = delete;
	Matcher &operator=(const Matcher &) = delete;

	~Matcher() noexcept
	{
		hx_os_matcher_destroy(&m_matcher_);
	}

	static Result<Matcher> compile(const hx_os_bytes_view *patterns,
		size_t count, size_t maxBytes = 0,
		hx_os_context *context = NULL) noexcept
	{
		Matcher result;
		hx_os_error error = {};
		auto status = hx_os_matcher_create(context, patterns, count,
			maxBytes, &result.m_matcher_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	/**
	 * \brief Use the C count output when progress on callback failure is
	 * needed.
	 */
	Result<size_t> scan(hx_os_bytes_view input, hx_os_match_fn callback,
		void *user = NULL, size_t maxMatches = 0) const noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_matcher_scan(m_matcher_, input, maxMatches,
			callback, user, &completed, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return completed;
	}

private:
	hx_os_matcher *m_matcher_;
};
} // namespace hx
#endif
