#ifndef HX_C_PLATFORM_TEST_CPP_CHECK_HPP
#define HX_C_PLATFORM_TEST_CPP_CHECK_HPP

#include <hx/Values.hpp>
#include <hx_os_atomic.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__EXCEPTIONS) || defined(_CPPUNWIND) || defined(__cpp_exceptions)
#error C++ contract tests require exceptions disabled
#endif

class CaptureProbe {
public:
	explicit CaptureProbe(hx_os_atomic &count) noexcept
	    : m_count_(&count)
	{
		hx_os_atomic_fetch_add(m_count_, 1, HX_OS_RELAXED);
	}

	CaptureProbe(const CaptureProbe &other) noexcept
	    : m_count_(other.m_count_)
	{
		if (m_count_)
			hx_os_atomic_fetch_add(m_count_, 1, HX_OS_RELAXED);
	}

	CaptureProbe(CaptureProbe &&other) noexcept
	    : m_count_(other.m_count_)
	{
		other.m_count_ = NULL;
	}

	~CaptureProbe() noexcept
	{
		if (m_count_)
			hx_os_atomic_fetch_sub(m_count_, 1, HX_OS_RELEASE);
	}

private:
	hx_os_atomic *m_count_;
};

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			std::fprintf(                                          \
				stderr, "line %d: %s\n", __LINE__, #value);    \
			std::exit(1);                                          \
		}                                                              \
	} while (0)

template <typename T>
T TakeAt(hx::Result<T> &&result, const char *file, int line) noexcept
{
	if (!result) {
		std::fprintf(stderr, "%s:%d: unexpected status: %s\n", file,
			line, hx_os_status_name(result.status()));
		std::exit(1);
	}

	return std::move(result).value();
}

inline void TakeAt(hx::Result<void> result, const char *file, int line) noexcept
{
	if (!result) {
		std::fprintf(stderr, "%s:%d: status: %s (native %lld)\n", file,
			line, hx_os_status_name(result.status()),
			static_cast<long long>(result.error().native_code));
		std::exit(1);
	}
}
#define Take(...) TakeAt((__VA_ARGS__), __FILE__, __LINE__)

/* Only for cleanup after releasing controlled test work. Platforms without
 * native timed join need an explicitly selected infinite final join.
 */
inline hx_os_deadline TestJoinDeadline() noexcept
{
	return hx_os_capabilities() & HX_OS_CAP_TIMED_JOIN
		? hx_os_deadline_after_ms(2000)
		: HX_OS_DEADLINE_INFINITE;
}

inline bool Equal(const hx::String &value, hx::StringView expected) noexcept
{
	return value.size() == expected.size() &&
		(!value.size() ||
			!std::memcmp(
				value.data(), expected.data(), value.size()));
}

inline bool Equal(
	hx::Result<hx::String> result, hx::StringView expected) noexcept
{
	return result.ok() && Equal(result.value(), expected);
}
#endif
