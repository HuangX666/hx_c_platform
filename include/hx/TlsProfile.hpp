#ifndef HX_C_PLATFORM_CPP_TLS_PROFILE_HPP
#define HX_C_PLATFORM_CPP_TLS_PROFILE_HPP

#include <hx_os_tls.h>
#include <hx/Status.hpp>

namespace hx {
class TlsProfile {
public:
	TlsProfile() noexcept
	    : m_profile_(NULL)
	{
	}

	TlsProfile(TlsProfile &&other) noexcept
	    : m_profile_(other.m_profile_)
	{
		other.m_profile_ = NULL;
	}

	TlsProfile &operator=(TlsProfile &&other) noexcept
	{
		std::swap(m_profile_, other.m_profile_);
		return *this;
	}

	TlsProfile(const TlsProfile &) = delete;
	TlsProfile &operator=(const TlsProfile &) = delete;

	~TlsProfile() noexcept
	{
		hx_os_tls_profile_release(m_profile_);
	}

	static Result<TlsProfile> create(const char *host, uint16_t port,
		const hx_os_tls_options_ex *options = NULL,
		hx_os_context *context = NULL) noexcept
	{
		TlsProfile result;
		hx_os_error error = {};
		auto status = hx_os_tls_profile_create(context, host, port,
			options, &result.m_profile_, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);
		return result;
	}

	Result<void> connect(hx_os_tls *&owner,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) const noexcept
	{
		if (owner)
			return Failure(HX_OS_BUSY);
		hx_os_error error = {};
		auto status = hx_os_tls_profile_connect_until(
			m_profile_, deadline, &owner, &error);

		return detail::Status(status, error);
	}

private:
	hx_os_tls_profile *m_profile_;
};
} // namespace hx
#endif
