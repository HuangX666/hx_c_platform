#ifndef HX_C_PLATFORM_CPP_UTILITIES_HPP
#define HX_C_PLATFORM_CPP_UTILITIES_HPP
#include <hx_os_wait.h>
#include <hx_os_wide.h>
#include <hx/Status.hpp>
#include <functional>
namespace hx {
class Wide {
public:
	static uint64_t multiplyDivide(uint64_t a, uint64_t b, uint64_t divisor,
		int rounding = HX_OS_ROUND_DOWN)
	{
		uint64_t result;
		hx_os_error error;
		detail::CheckStatus(hx_os_muldiv_u64(a, b, divisor, rounding,
					    &result, &error),
			error);
		return result;
	}
};
class Wait {
public:
	static void precise(hx_os_deadline deadline, uint64_t spinNs = 200000)
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_sleep_until_precise_ex(deadline, spinNs, &error),
			error);
	}
	/* Synchronous; callback returns false to stop normally. */
	static size_t repeat(uint64_t intervalMs, size_t ticks,
		const std::function<bool()> &callback,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE)
	{
		if (!callback)
			throw StatusError(HX_OS_INVALID);
		auto owned = callback;
		size_t completed;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_repeat_until(intervalMs, ticks, invoke, &owned,
				deadline, &completed, &error),
			error);
		return completed;
	}

private:
	static hx_os_status HX_OS_CALL invoke(void *user, hx_os_error *error)
	{
		try {
			return (*static_cast<std::function<bool()> *>(user))()
				? HX_OS_OK
				: HX_OS_EOF;
		} catch (const StatusError &failure) {
			*error = failure.error();
			return failure.status();
		} catch (...) {
			return HX_OS_IO_ERROR;
		}
	}
};
} // namespace hx
#endif
