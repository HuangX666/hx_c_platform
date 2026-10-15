#ifndef HX_C_PLATFORM_CPP_DIAL_HPP
#define HX_C_PLATFORM_CPP_DIAL_HPP

#include <hx_os_dial.h>
#include <hx/Status.hpp>

namespace hx {
/**
 * \brief Borrowed facade over the caller's socket owner and network lease.
 *
 * \details Failed dial keeps the owner NULL. Explicit close preserves it on
 * a retryable native close failure; facade destruction never discards it.
 */
class TcpDial {
public:
	explicit TcpDial(hx_os_socket *&owner) noexcept
	    : m_owner_(&owner)
	{
	}

	Result<void> connect(hx_os_network *network, const char *host,
		uint16_t port, const hx_os_dial_options *options = NULL,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tcp_dial_until(network, host, port, options,
			deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> connectAddresses(hx_os_network *network,
		const hx_os_address *addresses, size_t count,
		const hx_os_dial_options *options = NULL,
		hx_os_deadline deadline = hx_os_deadline_after_ms(
			5000)) noexcept
	{
		if (*m_owner_)
			return Failure(HX_OS_BUSY);

		hx_os_error error = {};
		auto status = hx_os_tcp_dial_addresses_until(network, addresses,
			count, options, deadline, m_owner_, &error);

		return detail::Status(status, error);
	}

	Result<void> close() noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_close(m_owner_, &error);

		return detail::Status(status, error);
	}

	hx_os_socket *get() const noexcept
	{
		return *m_owner_;
	}

private:
	hx_os_socket **m_owner_;
};
} // namespace hx
#endif
