#ifndef HX_C_PLATFORM_CPP_GO_KEYWORDS_HPP
#define HX_C_PLATFORM_CPP_GO_KEYWORDS_HPP

/* Optional keyword compatibility. Include last: the global go macro can
 * collide with unrelated member names. Use runtime.submit() for Task<T>.
 * A native caller first enters Scope; coroutine callers inherit the runtime.
 */
#include <hx/Go.hpp>

template <typename T> using co_chan = ::hx::coro::Chan<T>;
#define go hx_go_here
#define co_sleep(duration)                                                     \
	::hx::coro::sleepFor(std::chrono::milliseconds(duration))
#if !defined(__cpp_impl_coroutine)
#define co_yield ::hx::coro::yield()
#endif
#endif
