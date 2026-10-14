#ifndef HX_C_PLATFORM_CPP_UTILITIES_HPP
#define HX_C_PLATFORM_CPP_UTILITIES_HPP

#include <hx_os_wait.h>
#include <hx_os_wide.h>
#include <hx/Callback.hpp>

namespace hx {

class Wide {
public:
	static Result<uint64_t> multiplyDivide(uint64_t a, uint64_t b,
		uint64_t divisor, int rounding = HX_OS_ROUND_DOWN) noexcept
	{
		uint64_t value = 0;
		hx_os_error error = {};

		auto status = hx_os_muldiv_u64(
			a, b, divisor, rounding, &value, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}
};

class Wait {
public:
	static Result<void> precise(
		hx_os_deadline deadline, uint64_t spinNs = 200000) noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_sleep_until_precise_ex(deadline, spinNs, &error);

		return detail::Status(status, error);
	}

	// Synchronous: borrows the callable for this call. false stops
	// normally. Result<bool> additionally preserves a callback's explicit
	// error details.
	template <typename F>
	static Result<size_t> repeat(uint64_t intervalMs, size_t ticks,
		F &&callback,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE) noexcept
	{
		typedef typename std::remove_reference<F>::type Function;

		if (detail::NullCallable(callback, std::is_pointer<Function>()))
			return Failure(HX_OS_INVALID);

		size_t completed = 0;
		hx_os_error error = {};
		Borrowed<Function> borrowed = {&callback};
		auto status =
			hx_os_repeat_until(intervalMs, ticks, invoke<Function>,
				&borrowed, deadline, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

private:
	template <typename F> struct Borrowed {
		F *function;
	};

	static Result<bool> continuation(bool value) noexcept
	{
		return value;
	}

	static Result<bool> continuation(Result<bool> value) noexcept
	{
		return value;
	}

	template <typename F>
	static hx_os_status HX_OS_CALL invoke(
		void *user, hx_os_error *error) noexcept
	{
		auto &function = *static_cast<Borrowed<F> *>(user)->function;

		static_assert(std::is_pointer<F>::value ||
				std::is_function<F>::value ||
				noexcept(function()),
			"Repeat callbacks must be noexcept");

		auto result = continuation(function());
		if (!result) {
			if (error)
				*error = result.error();

			return result.status();
		}

		return result.value() ? HX_OS_OK : HX_OS_EOF;
	}
};
} // namespace hx
#endif
