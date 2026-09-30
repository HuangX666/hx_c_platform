#ifndef HX_C_PLATFORM_CPP_STATUS_HPP
#define HX_C_PLATFORM_CPP_STATUS_HPP

#include <hx_os_base.h>
#include <exception>

namespace hx {

class StatusError : public std::exception {
public:
	explicit StatusError(hx_os_status status) noexcept
	    : m_error_{status, HX_OS_ERROR_NONE, 0, 0}
	{
	}
	explicit StatusError(const hx_os_error &error) noexcept
	    : m_error_(error)
	{
	}
	const char *what() const noexcept override
	{
		return hx_os_status_name(m_error_.status);
	}
	hx_os_status status() const noexcept
	{
		return m_error_.status;
	}
	const hx_os_error &error() const noexcept
	{
		return m_error_;
	}

private:
	hx_os_error m_error_;
};

namespace detail {

inline void CheckStatus(hx_os_status status, const hx_os_error &error)
{
	if (status != HX_OS_OK)
		throw StatusError(error);
}

} // namespace detail
} // namespace hx
#endif
