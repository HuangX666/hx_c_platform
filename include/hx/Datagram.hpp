#ifndef HX_C_PLATFORM_CPP_DATAGRAM_HPP
#define HX_C_PLATFORM_CPP_DATAGRAM_HPP

#include <hx_os_datagram.h>
#include <hx/Status.hpp>

namespace hx {
struct DatagramPacket {
	hx_os_address peer;
	hx_os_packet_info info;
	size_t bytes;
};

/**
 * \brief Borrowed UDP socket; buffers remain caller-owned.
 */
class Datagram {
public:
	explicit Datagram(hx_os_socket *socket) noexcept
	    : m_socket_(socket)
	{
	}

	Result<void> packetInfo(bool enabled = true) const noexcept
	{
		hx_os_error error = {};
		auto status = hx_os_socket_set_packet_info(
			m_socket_, enabled ? 1 : 0, &error);

		return detail::Status(status, error);
	}

	Result<size_t> sendTo(const hx_os_address &peer, const void *data,
		size_t bytes, hx_os_deadline deadline,
		const hx_os_packet_info *route = NULL) const noexcept
	{
		size_t completed = 0;
		hx_os_error error = {};
		auto status = hx_os_socket_send_packet_until(m_socket_, &peer,
			route, data, bytes, deadline, &completed, &error);

		return Result<size_t>::withValue(completed, status, error);
	}

	Result<DatagramPacket> receive(void *data, size_t capacity,
		hx_os_deadline deadline) const noexcept
	{
		DatagramPacket packet = {};
		hx_os_error error = {};
		auto status = hx_os_socket_receive_packet_until(m_socket_,
			&packet.peer, &packet.info, data, capacity, deadline,
			&packet.bytes, &error);

		return Result<DatagramPacket>::withValue(packet, status, error);
	}

private:
	hx_os_socket *m_socket_;
};
} // namespace hx
#endif
