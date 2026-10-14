#ifndef HX_C_PLATFORM_CPP_REGEX_HPP
#define HX_C_PLATFORM_CPP_REGEX_HPP

#include <hx_regex.h>
#include <hx/Values.hpp>

namespace hx {
class RegexMatch {
public:
	RegexMatch(hx_string_view subject,
		const hx_os_regex_result &result) noexcept
	    : m_subject_(subject)
	    , m_result_(result)
	{
	}

	size_t count() const noexcept
	{
		return m_result_.count;
	}

	Result<StringView> group(size_t index = 0) const noexcept
	{
		if (index >= m_result_.count)
			return Failure(HX_OS_RANGE);
		const auto &span = m_result_.captures[index];
		if (span.offset == SIZE_MAX)
			return Failure(HX_OS_NOT_FOUND);
		return StringView(
			m_subject_.data ? m_subject_.data + span.offset : "",
			span.size);
	}

	size_t offset() const noexcept
	{
		return m_result_.captures[0].offset;
	}

private:
	hx_string_view m_subject_;
	hx_os_regex_result m_result_;
};

class Regex {
public:
	Regex() noexcept
	    : m_regex_(NULL)
	{
	}

	Regex(Regex &&other) noexcept
	    : m_regex_(other.m_regex_)
	{
		other.m_regex_ = NULL;
	}

	Regex &operator=(Regex &&other) noexcept
	{
		std::swap(m_regex_, other.m_regex_);
		return *this;
	}

	Regex(const Regex &) = delete;
	Regex &operator=(const Regex &) = delete;

	~Regex() noexcept
	{
		hx_os_regex_destroy(&m_regex_);
	}

	static Result<Regex> compile(StringView pattern,
		const hx_os_regex_options *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		Regex result;
		hx_os_error error = {};
		auto status = hx_os_regex_compile(context,
			hx_string_view{pattern.data(), pattern.size()}, options,
			&result.m_regex_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	static Result<bool> matches(
		const char *pattern, StringView subject) noexcept
	{
		int matched = 0;
		hx_os_error error = {};
		auto status = hx_os_regex_matches(pattern,
			hx_string_view{subject.data(), subject.size()},
			&matched, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return matched != 0;
	}

	Result<bool> fullMatch(StringView subject,
		const hx_os_regex_limits *limits = NULL) const noexcept
	{
		int matched = 0;
		hx_os_error error = {};
		auto status = hx_os_regex_full_match(m_regex_,
			hx_string_view{subject.data(), subject.size()}, limits,
			&matched, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return matched != 0;
	}

	/**
	 * \brief Returned groups borrow subject.
	 *
	 * \details Keep its storage unchanged/alive.
	 */
	Result<RegexMatch> find(StringView subject, size_t start = 0,
		const hx_os_regex_limits *limits = NULL) const noexcept
	{
		hx_os_regex_result result;
		hx_os_error error = {};
		auto status = hx_os_regex_find_with_limits(m_regex_,
			hx_string_view{subject.data(), subject.size()}, start,
			limits, &result, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return RegexMatch(
			hx_string_view{subject.data(), subject.size()}, result);
	}

	Result<size_t> captureIndex(const char *name) const noexcept
	{
		size_t index = 0;
		hx_os_error error = {};
		auto status = hx_os_regex_capture_index(
			m_regex_, name, &index, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return index;
	}

	Result<String> replace(StringView subject, StringView replacement,
		const hx_os_regex_limits *limits = NULL,
		hx_os_context *context = NULL) const noexcept
	{
		String result(context);
		hx_os_error error = {};
		auto status = hx_os_regex_replace(m_regex_,
			hx_string_view{subject.data(), subject.size()},
			hx_string_view{replacement.data(), replacement.size()},
			limits, result.native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<size_t> split(StringView subject, hx_os_regex_split_fn callback,
		void *user = NULL,
		const hx_os_regex_limits *limits = NULL) const noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_regex_split(m_regex_,
			hx_string_view{subject.data(), subject.size()}, limits,
			callback, user, &completed, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return completed;
	}

private:
	hx_os_regex *m_regex_;
};
} // namespace hx
#endif
