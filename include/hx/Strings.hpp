#ifndef HX_C_PLATFORM_CPP_STRINGS_HPP
#define HX_C_PLATFORM_CPP_STRINGS_HPP
#include <hx_string.h>
#include <hx/Status.hpp>
#include <memory>
#include <vector>
namespace hx {
class Strings {
	class Value {
	public:
		explicit Value(const std::string &input = std::string())
		    : value{}
		{
			hx_os_error error;
			hx_string_view view = {input.data(), input.size()};
			detail::CheckStatus(
				hx_string_assign(&value, view, &error), error);
		}
		~Value()
		{
			hx_string_destroy(&value);
		}
		Value(const Value &) = delete;
		Value &operator=(const Value &) = delete;
		std::string string() const
		{
			return std::string(hx_string_c_str(&value), value.size);
		}
		hx_string value;
	};
	static hx_string_view view(const std::string &text)
	{
		hx_string_view result = {text.data(), text.size()};
		return result;
	}

public:
	static std::string trim(const std::string &input)
	{
		Value value(input);
		hx_os_error error;
		detail::CheckStatus(
			hx_string_trim_ascii(&value.value, &error), error);
		return value.string();
	}
	static std::string lowerAscii(const std::string &input)
	{
		Value value(input);
		hx_os_error error;
		detail::CheckStatus(
			hx_string_lower_ascii(&value.value, &error), error);
		return value.string();
	}
	static std::string upperAscii(const std::string &input)
	{
		Value value(input);
		hx_os_error error;
		detail::CheckStatus(
			hx_string_upper_ascii(&value.value, &error), error);
		return value.string();
	}
	static std::string replace(const std::string &input,
		const std::string &needle, const std::string &replacement)
	{
		Value value(input);
		hx_os_error error;
		detail::CheckStatus(
			hx_string_replace_all(&value.value, view(needle),
				view(replacement), NULL, &error),
			error);
		return value.string();
	}
	static std::vector<std::string> split(const std::string &input,
		const std::string &separator, bool keepEmpty = true,
		size_t maxParts = 4096)
	{
		Value value(input);
		hx_string_parts *parts = NULL;
		hx_os_error error;
		detail::CheckStatus(
			hx_string_split_ex(&value.value, view(separator),
				maxParts, keepEmpty ? 1 : 0, &parts, &error),
			error);
		struct Guard {
			hx_string_parts *parts;
			~Guard()
			{
				hx_string_parts_destroy(&parts);
			}
		} guard = {parts};
		std::vector<std::string> result;
		result.reserve(hx_string_parts_count(parts));
		for (size_t i = 0; i < hx_string_parts_count(parts); ++i) {
			hx_string_view piece;
			(void)hx_string_parts_at(parts, i, &piece);
			result.emplace_back(piece.data, piece.size);
		}
		return result;
	}
	static std::string join(const std::vector<std::string> &parts,
		const std::string &separator)
	{
		std::vector<hx_string_view> views;
		views.reserve(parts.size());
		for (const auto &part : parts)
			views.push_back(view(part));
		Value value;
		hx_os_error error;
		detail::CheckStatus(
			hx_string_join(&value.value, views.data(), views.size(),
				view(separator), &error),
			error);
		return value.string();
	}
};
} // namespace hx
#endif
