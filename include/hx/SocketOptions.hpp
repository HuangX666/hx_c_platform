#ifndef HX_C_PLATFORM_CPP_SOCKET_OPTIONS_HPP
#define HX_C_PLATFORM_CPP_SOCKET_OPTIONS_HPP

#include <hx_os_net.h>
#include <hx/Status.hpp>
#include <type_traits>

namespace hx {
/* Borrowed view. It never changes ownership or closes the socket. */
class SocketOptions {
public:
	explicit SocketOptions(hx_os_socket *socket) noexcept
	    : m_socket_(socket)
	{
	}
	void noDelay(bool enabled = true) const
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_set_nodelay(m_socket_, enabled, &error),
			error);
	}
	void keepAlive(bool enabled = true) const
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_set_keepalive(m_socket_, enabled, &error),
			error);
	}
	void reuseAddress(bool enabled = true) const
	{
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_set_reuseaddr(m_socket_, enabled, &error),
			error);
	}
	hx_os_address peerAddress() const
	{
		hx_os_address address;
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_peer_address(m_socket_, &address, &error),
			error);
		return address;
	}
	template <typename Value> Value get(int level, int name) const
	{
		static_assert(std::is_trivially_copyable<Value>::value,
			"Native option values must be trivially copyable");
		Value value{};
		size_t bytes = sizeof(value);
		hx_os_error error;
		detail::CheckStatus(hx_os_socket_option_get(m_socket_, level,
					    name, &value, &bytes, &error),
			error);
		if (bytes != sizeof(value))
			throw StatusError(HX_OS_RANGE);
		return value;
	}
	template <typename Value>
	void set(int level, int name, const Value &value) const
	{
		static_assert(std::is_trivially_copyable<Value>::value,
			"Native option values must be trivially copyable");
		hx_os_error error;
		detail::CheckStatus(
			hx_os_socket_option_set(m_socket_, level, name, &value,
				sizeof(value), &error),
			error);
	}

private:
	hx_os_socket *m_socket_;
};
} // namespace hx
#endif
