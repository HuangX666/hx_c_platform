#ifndef HX_C_PLATFORM_CPP_FORMAT_HPP
#define HX_C_PLATFORM_CPP_FORMAT_HPP

#include <hx/Values.hpp>
#include <hx_format.h>
#include <array>
#include <string>

namespace hx {
/**
 * \brief Specialize format(value, specification, out, limit, error) noexcept
 * for application types.
 *
 * \details out is a borrowed UTF-8 hx_string owned by the engine.
 */
template <typename T> struct Formatter;

namespace format_detail {
inline hx_format_arg argument(bool value) noexcept
{
	return hx_fmt_bool(value);
}

inline hx_format_arg argument(char value) noexcept
{
	return hx_fmt_char(static_cast<unsigned char>(value));
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && std::is_signed<T>::value,
	hx_format_arg>::type
argument(T value) noexcept
{
	return hx_fmt_i64(static_cast<int64_t>(value));
}

template <typename T>
typename std::enable_if<std::is_integral<T>::value && !std::is_signed<T>::value,
	hx_format_arg>::type
argument(T value) noexcept
{
	return hx_fmt_u64(static_cast<uint64_t>(value));
}

inline hx_format_arg argument(float value) noexcept
{
	return hx_fmt_f32(value);
}

inline hx_format_arg argument(double value) noexcept
{
	return hx_fmt_f64(value);
}

inline hx_format_arg argument(long double value) noexcept
{
	return hx_fmt_real(value);
}

inline hx_format_arg argument(const char *value) noexcept
{
	return hx_fmt_str(value);
}

inline hx_format_arg argument(char *value) noexcept
{
	return hx_fmt_str(value);
}

inline hx_format_arg argument(const void *value) noexcept
{
	return hx_fmt_pointer(value);
}

template <typename T>
typename std::enable_if<!std::is_function<T>::value &&
		!std::is_same<typename std::remove_cv<T>::type, char>::value,
	hx_format_arg>::type
argument(T *value) noexcept
{
	return hx_fmt_pointer(const_cast<const void *>(
		static_cast<const volatile void *>(value)));
}

template <typename T>
typename std::enable_if<std::is_enum<T>::value, hx_format_arg>::type argument(
	T value) noexcept
{
	return argument(
		static_cast<typename std::underlying_type<T>::type>(value));
}

inline hx_format_arg argument(std::nullptr_t) noexcept
{
	return hx_fmt_pointer(NULL);
}

inline hx_format_arg argument(StringView value) noexcept
{
	return hx_fmt_view(value.native());
}

inline hx_format_arg argument(const String &value) noexcept
{
	return hx_fmt_view(value.view().native());
}

inline hx_format_arg argument(const std::string &value) noexcept
{
	return hx_fmt_view({value.data(), value.size()});
}

inline hx_format_arg argument(hx_string_view value) noexcept
{
	return hx_fmt_view(value);
}

inline hx_format_arg argument(const hx_string &value) noexcept
{
	return hx_fmt_view(hx_string_view_of(&value));
}

inline hx_format_arg argument(hx_format_arg value) noexcept
{
	return value;
}

template <typename T>
hx_os_status HX_OS_CALL custom(const void *value, hx_string_view spec,
	hx_string *out, size_t limit, hx_os_error *error) noexcept
{
	static_assert(
		noexcept(Formatter<T>::format(*static_cast<const T *>(value),
			StringView(spec), out, limit, error)),
		"Custom formatters must be noexcept");

	return Formatter<T>::format(*static_cast<const T *>(value),
		StringView(spec), out, limit, error);
}

template <typename T>
typename std::enable_if<!std::is_arithmetic<T>::value &&
		!std::is_pointer<T>::value && !std::is_enum<T>::value &&
		!std::is_convertible<T, const char *>::value,
	hx_format_arg>::type
argument(const T &value) noexcept
{
	return hx_fmt_custom(&value, custom<T>);
}
} // namespace format_detail

// Named arguments are call-borrowed; do not store one past its value's life.
template <typename T>
hx_format_arg named(const char *name, const T &value) noexcept
{
	return hx_fmt_named(name, format_detail::argument(value));
}

template <typename... A>
Result<void> formatTo(String &out, StringView pattern, A &&...values) noexcept
{
	std::array<hx_format_arg, sizeof...(A)> args = {
		{format_detail::argument(std::forward<A>(values))...}};
	hx_os_error error = {};

	auto status = hx_format(out.native(), pattern.native(), args.data(),
		args.size(), &error);

	return detail::Status(status, error);
}

template <typename... A>
Result<String> format(StringView pattern, A &&...values) noexcept
{
	String out;

	auto result = formatTo(out, pattern, std::forward<A>(values)...);
	if (!result)
		return Failure(result.error());

	return out;
}

template <typename... A>
Result<void> printTo(
	int stream, bool newline, StringView pattern, A &&...values) noexcept
{
	std::array<hx_format_arg, sizeof...(A)> args = {
		{format_detail::argument(std::forward<A>(values))...}};
	hx_os_error error = {};

	auto status = hx_print_args(stream, newline, pattern.native(),
		args.data(), args.size(), &error);

	return detail::Status(status, error);
}

template <typename... A>
Result<void> print(StringView pattern, A &&...values) noexcept
{
	return printTo(
		HX_PRINT_STDOUT, false, pattern, std::forward<A>(values)...);
}

template <typename... A>
Result<void> println(StringView pattern, A &&...values) noexcept
{
	return printTo(
		HX_PRINT_STDOUT, true, pattern, std::forward<A>(values)...);
}
} // namespace hx
#endif
