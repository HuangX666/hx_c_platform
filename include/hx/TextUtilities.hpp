#ifndef HX_C_PLATFORM_CPP_TEXT_UTILITIES_HPP
#define HX_C_PLATFORM_CPP_TEXT_UTILITIES_HPP

#include <hx_text_utils.h>
#include <hx/Values.hpp>

namespace hx {
/**
 * \brief The source and delimiter are borrowed.
 *
 * \details Copies have independent positions, but share the same source
 * lifetime. Mutating their storage invalidates them.
 */
class SplitView {
public:
	SplitView() noexcept
	    : m_split_{}
	{
		m_split_.finished = 1;
	}

	static Result<SplitView> create(StringView input, StringView delimiter,
		bool keepEmpty = true) noexcept
	{
		SplitView result;
		hx_os_error error = {};
		auto status = hx_os_text_split_init(&result.m_split_,
			input.native(), delimiter.native(), keepEmpty ? 1 : 0,
			&error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<StringView> next() noexcept
	{
		hx_string_view part = {};
		hx_os_error error = {};
		auto status = hx_os_text_split_next(&m_split_, &part, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return StringView(part);
	}

private:
	hx_os_text_split m_split_;
};

class TextUtilities {
public:
	static Result<int64_t> parseI64(
		StringView input, unsigned base = 10) noexcept
	{
		int64_t value = 0;
		hx_os_error error = {};
		auto status = hx_os_text_parse_i64(
			input.native(), base, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static Result<uint64_t> parseU64(
		StringView input, unsigned base = 10) noexcept
	{
		uint64_t value = 0;
		hx_os_error error = {};
		auto status = hx_os_text_parse_u64(
			input.native(), base, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	static int compareNatural(StringView left, StringView right) noexcept
	{
		return hx_os_text_compare_natural(
			left.native(), right.native());
	}
};
} // namespace hx
#endif
