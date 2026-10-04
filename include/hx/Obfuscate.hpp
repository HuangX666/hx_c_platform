#ifndef HX_C_PLATFORM_CPP_OBFUSCATE_HPP
#define HX_C_PLATFORM_CPP_OBFUSCATE_HPP

#include <hx_os_obfuscate.h>
#include <hx/Values.hpp>
#include <array>

namespace hx {
namespace detail {
template <size_t... Values> struct ObfuscationIndices {};
template <size_t Count, size_t... Values>
struct ObfuscationSequence
    : ObfuscationSequence<Count - 1, Count - 1, Values...> {};
template <size_t... Values> struct ObfuscationSequence<0, Values...> {
	typedef ObfuscationIndices<Values...> type;
};
} // namespace detail

template <size_t Size, uint8_t Key> class ObfuscatedString {
public:
	static_assert(
		Size > 0 && Key != 0, "Use a nonzero XOR key and a literal");
	template <size_t... Indices>
	constexpr ObfuscatedString(const char (&text)[Size],
		detail::ObfuscationIndices<Indices...>)
	    : m_bytes_{{static_cast<unsigned char>(text[Indices] ^ Key)...}}
	{
	}

	hx_os_obfuscated view() const noexcept
	{
		return {m_bytes_.data(), Size - 1, Key};
	}

	Result<String> decode(hx_os_context *context = NULL) const noexcept
	{
		auto encoded = view();

		return detail::StringQuery(
			[&](hx_string *out, hx_os_error *error) noexcept {
				return hx_os_obfuscate_string(
					&encoded, out, error);
			},
			context);
	}

private:
	std::array<unsigned char, Size - 1> m_bytes_;
};

/* Declare the result constexpr to request compile-time encoding. Toolchains
 * can retain source/debug strings; this is reversible obfuscation.
 */
template <uint8_t Key, size_t Size>
constexpr ObfuscatedString<Size, Key> obfuscate(const char (&text)[Size])
{
	return ObfuscatedString<Size, Key>(
		text, typename detail::ObfuscationSequence<Size - 1>::type());
}
} // namespace hx
#endif
