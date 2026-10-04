#ifndef HX_C_PLATFORM_CPP_STRINGS_HPP
#define HX_C_PLATFORM_CPP_STRINGS_HPP

#include <hx/Values.hpp>

namespace hx {

class StringParts {
public:
	StringParts() noexcept
	    : m_parts_(NULL)
	{
	}

	StringParts(const StringParts &) = delete;

	StringParts &operator=(const StringParts &) = delete;

	StringParts(StringParts &&other) noexcept
	    : m_parts_(other.m_parts_)
	{
		other.m_parts_ = NULL;
	}

	StringParts &operator=(StringParts &&other) noexcept
	{
		std::swap(m_parts_, other.m_parts_);

		return *this;
	}

	~StringParts() noexcept
	{
		hx_string_parts_destroy(&m_parts_);
	}

	static StringParts adopt(hx_string_parts *parts) noexcept
	{
		StringParts value;

		value.m_parts_ = parts;

		return value;
	}

	size_t size() const noexcept
	{
		return hx_string_parts_count(m_parts_);
	}

	/* Returned view borrows this parts object, not the original input
	 * string. */
	Result<StringView> at(size_t index) const noexcept
	{
		hx_string_view value = {};

		auto status = hx_string_parts_at(m_parts_, index, &value);
		if (status != HX_OS_OK)
			return Failure(status);

		return StringView(value);
	}

private:
	hx_string_parts *m_parts_;
};

class Strings {
	typedef hx_os_status(HX_OS_CALL *Mutation)(hx_string *, hx_os_error *);

	static Result<String> transform(StringView input, Mutation function,
		hx_os_context *context) noexcept
	{
		auto result = String::copy(input, context);
		if (!result)
			return Failure(result.error());

		hx_os_error error = {};

		auto status = function(result.value().native(), &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

public:
	static Result<String> trim(
		StringView input, hx_os_context *context = NULL) noexcept
	{
		return transform(input, hx_string_trim_ascii, context);
	}

	static Result<String> lowerAscii(
		StringView input, hx_os_context *context = NULL) noexcept
	{
		return transform(input, hx_string_lower_ascii, context);
	}

	static Result<String> upperAscii(
		StringView input, hx_os_context *context = NULL) noexcept
	{
		return transform(input, hx_string_upper_ascii, context);
	}

	static Result<String> replace(StringView input, StringView needle,
		StringView replacement, hx_os_context *context = NULL) noexcept
	{
		auto result = String::copy(input, context);
		if (!result)
			return Failure(result.error());

		hx_os_error error = {};

		auto status = hx_string_replace_all(result.value().native(),
			needle.native(), replacement.native(), NULL, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	static Result<StringParts> split(StringView input, StringView separator,
		bool keepEmpty = true, size_t maxParts = 4096,
		hx_os_context *context = NULL) noexcept
	{
		auto text = String::copy(input, context);
		if (!text)
			return Failure(text.error());

		hx_string_parts *parts = NULL;
		hx_os_error error = {};

		auto status = hx_string_split_ex(text.value().native(),
			separator.native(), maxParts, keepEmpty ? 1 : 0, &parts,
			&error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringParts::adopt(parts);
	}

	static Result<String> join(const StringView *parts, size_t count,
		StringView separator, hx_os_context *context = NULL) noexcept
	{
		if ((!parts && count) ||
			count > SIZE_MAX / sizeof(hx_string_view))
			return Failure(HX_OS_INVALID);
		auto storage = Buffer::allocate(
			count * sizeof(hx_string_view), context);
		if (!storage)
			return Failure(storage.error());

		auto views = reinterpret_cast<hx_string_view *>(
			storage.value().data());

		for (size_t i = 0; i < count; ++i)
			new (&views[i]) hx_string_view(parts[i].native());

		return detail::StringQuery(
			[&](hx_string *out, hx_os_error *error) noexcept {
				return hx_string_join(out, views, count,
					separator.native(), error);
			},
			context);
	}
};
} // namespace hx
#endif
