#ifndef HX_C_PLATFORM_CPP_ONCE_HPP
#define HX_C_PLATFORM_CPP_ONCE_HPP

#include <hx_os_once.h>
#include <hx/Status.hpp>

namespace hx {
class Once {
public:
	Once() noexcept
	    : m_once_(NULL)
	{
	}

	Once(Once &&other) noexcept
	    : m_once_(other.m_once_)
	{
		other.m_once_ = NULL;
	}

	Once &operator=(Once &&other) noexcept
	{
		std::swap(m_once_, other.m_once_);
		return *this;
	}

	Once(const Once &) = delete;
	Once &operator=(const Once &) = delete;

	~Once() noexcept
	{
		(void)hx_os_once_destroy(&m_once_, NULL);
	}

	static Result<Once> create(hx_os_context *context = NULL) noexcept
	{
		Once result;
		hx_os_error error = {};
		auto status =
			hx_os_once_create(context, &result.m_once_, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	/* Callable returns void or Result<void>; it is borrowed through this
	 * call. */
	template <typename Function>
	Result<void> run(Function &&function,
		hx_os_deadline deadline = HX_OS_DEADLINE_INFINITE,
		hx_os_cancel_token *cancel = NULL) noexcept
	{
		using Callable = typename std::remove_reference<Function>::type;
		Callable *callback = &function;
		hx_os_error error = {};
		auto status = hx_os_once_run_until(m_once_, invoke<Callable>,
			&callback, deadline, cancel, &error);
		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_once_destroy(&m_once_, &error);
		return detail::Status(status, error);
	}

private:
	hx_os_once *m_once_;

	template <typename Function>
	static hx_os_status HX_OS_CALL invoke(void *user, hx_os_error *error)
	{
		Function &function = **static_cast<Function **>(user);
		return call(
			function, error, std::is_void<decltype(function())>());
	}

	template <typename Function>
	static hx_os_status call(
		Function &function, hx_os_error *error, std::true_type)
	{
		(void)error;
		function();
		return HX_OS_OK;
	}

	template <typename Function>
	static hx_os_status call(
		Function &function, hx_os_error *error, std::false_type)
	{
		auto result = function();
		if (!result.ok()) {
			if (error)
				*error = result.error();
			return result.status();
		}
		return HX_OS_OK;
	}
};
} // namespace hx
#endif
