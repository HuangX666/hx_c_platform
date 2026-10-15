#ifndef HX_C_PLATFORM_CPP_SOCKET_OPTIONS_HPP
#define HX_C_PLATFORM_CPP_SOCKET_OPTIONS_HPP

#include <hx_os_net.h>
#include <hx_os_net_extra.h>
#include <hx/Status.hpp>

namespace hx {

/**
 * \brief Borrowed socket; this facade never closes it.
 */
class SocketOptions {
public:
	explicit SocketOptions(hx_os_socket *socket) noexcept
	    : m_socket_(socket)
	{
	}

	Result<void> noDelay(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_set_nodelay(m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> keepAlive(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_set_keepalive(m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> reuseAddress(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_set_reuseaddr(m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> keepAlive(uint32_t idleSeconds, uint32_t intervalSeconds,
		uint32_t probeCount = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_configure_keepalive(m_socket_,
			idleSeconds, intervalSeconds, probeCount, &error);

		return detail::Status(status, error);
	}

	Result<void> dualStack(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_set_dual_stack(m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> broadcast(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status =
			hx_os_socket_set_broadcast(m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> joinMulticast(const hx_os_address &group,
		uint32_t interfaceIndex = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_join_multicast(
			m_socket_, &group, interfaceIndex, &error);

		return detail::Status(status, error);
	}

	Result<void> leaveMulticast(const hx_os_address &group,
		uint32_t interfaceIndex = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_leave_multicast(
			m_socket_, &group, interfaceIndex, &error);

		return detail::Status(status, error);
	}

	Result<void> multicastInterface(
		uint32_t interfaceIndex = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_set_multicast_interface(
			m_socket_, interfaceIndex, &error);

		return detail::Status(status, error);
	}

	Result<void> multicastHops(uint32_t hops) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_set_multicast_hops(
			m_socket_, hops, &error);

		return detail::Status(status, error);
	}

	Result<void> multicastLoopback(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_set_multicast_loopback(
			m_socket_, enabled, &error);

		return detail::Status(status, error);
	}

	Result<void> buffers(
		uint32_t sendBytes, uint32_t receiveBytes) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_set_buffers(
			m_socket_, sendBytes, receiveBytes, &error);

		return detail::Status(status, error);
	}

	struct BufferSizes {
		uint32_t sendBytes, receiveBytes;
	};

	Result<BufferSizes> buffers() const noexcept
	{
		BufferSizes value = {};
		hx_os_error error = {};
		auto status = hx_os_socket_get_buffers(m_socket_,
			&value.sendBytes, &value.receiveBytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	Result<void> joinMulticastSource(const hx_os_address &group,
		const hx_os_address &source,
		uint32_t interfaceIndex = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_join_multicast_source(
			m_socket_, &group, &source, interfaceIndex, &error);

		return detail::Status(status, error);
	}

	Result<void> leaveMulticastSource(const hx_os_address &group,
		const hx_os_address &source,
		uint32_t interfaceIndex = 0) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_leave_multicast_source(
			m_socket_, &group, &source, interfaceIndex, &error);

		return detail::Status(status, error);
	}

	Result<hx_os_address> peerAddress() const noexcept
	{
		hx_os_address value = {};
		hx_os_error error = {};
		auto status =
			hx_os_socket_peer_address(m_socket_, &value, &error);

		if (status != HX_OS_OK)
			return Failure(status, error);

		return value;
	}

	template <typename T> Result<T> get(int level, int name) const noexcept
	{
		static_assert(std::is_trivially_copyable<T>::value,
			"Use a native POD value");

		T value{};
		size_t bytes = sizeof(value);
		hx_os_error error = {};

		auto status = hx_os_socket_option_get(
			m_socket_, level, name, &value, &bytes, &error);
		if (status != HX_OS_OK)
			return Failure(status, error);

		if (bytes != sizeof(value))
			return Failure(HX_OS_RANGE);

		return value;
	}

	template <typename T>
	Result<void> set(int level, int name, const T &value) const noexcept
	{
		static_assert(std::is_trivially_copyable<T>::value,
			"Use a native POD value");

		hx_os_error error = {};

		auto status = hx_os_socket_option_set(
			m_socket_, level, name, &value, sizeof(value), &error);

		return detail::Status(status, error);
	}

private:
	hx_os_socket *m_socket_;
};
} // namespace hx
#endif
