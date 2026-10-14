#ifndef HX_C_PLATFORM_CPP_CONTROL_HPP
#define HX_C_PLATFORM_CPP_CONTROL_HPP

#include <hx_os_control.h>
#include <hx/Status.hpp>

namespace hx {
class Stopwatch {
public:
	Stopwatch() noexcept
	    : m_watch_{}
	{
	}

	static Result<Stopwatch> start() noexcept
	{
		Stopwatch result;
		hx_os_error error = {};
		auto status = hx_os_stopwatch_start(&result.m_watch_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<uint64_t> elapsedNs() const noexcept
	{
		uint64_t elapsed = 0;
		hx_os_error error = {};
		auto status =
			hx_os_stopwatch_elapsed_ns(&m_watch_, &elapsed, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return elapsed;
	}

	Result<uint64_t> restart() noexcept
	{
		uint64_t elapsed = 0;
		hx_os_error error = {};
		auto status =
			hx_os_stopwatch_restart(&m_watch_, &elapsed, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return elapsed;
	}

private:
	hx_os_stopwatch m_watch_;
};

class Backoff {
public:
	Backoff() noexcept
	    : m_backoff_{}
	{
	}

	static Result<Backoff> create(
		const hx_os_backoff_options *options = NULL) noexcept
	{
		Backoff result;
		hx_os_error error = {};
		auto status =
			hx_os_backoff_init(&result.m_backoff_, options, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	Result<uint64_t> next() noexcept
	{
		uint64_t delay = 0;
		hx_os_error error = {};
		auto status = hx_os_backoff_next(&m_backoff_, &delay, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return delay;
	}

	Result<void> reset() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_backoff_reset(&m_backoff_, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_backoff m_backoff_;
};

class TokenBucket {
public:
	TokenBucket() noexcept
	    : m_bucket_{}
	{
	}

	static Result<TokenBucket> create(
		uint64_t capacity, uint32_t ratePerSecond) noexcept
	{
		TokenBucket result;
		hx_os_error error = {};
		auto status = hx_os_token_bucket_init(
			&result.m_bucket_, capacity, ratePerSecond, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return result;
	}

	/**
	 * \brief WOULD_BLOCK retains the suggested deadline as a Result value.
	 */
	Result<hx_os_deadline> tryTake(uint64_t amount = 1) noexcept
	{
		hx_os_deadline retry = HX_OS_DEADLINE_NOW;
		hx_os_error error = {};
		auto status = hx_os_token_bucket_try_take(
			&m_bucket_, amount, &retry, &error);
		if (status != HX_OS_OK && status != HX_OS_WOULD_BLOCK)
			return Failure(status, error);

		return Result<hx_os_deadline>::withValue(retry, status, error);
	}

private:
	hx_os_token_bucket m_bucket_;
};
} // namespace hx
#endif
