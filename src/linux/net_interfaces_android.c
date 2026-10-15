#include "net_interfaces_private.h"

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#define HX_OS_I_INTERFACE_PACKET (128u * 1024u)

static hx_os_status hx_os_interface_address_message(int query,
	hx_os_interface_list *list, const struct nlmsghdr *header,
	hx_os_error *error)
{
	const struct ifaddrmsg *message;
	const unsigned char *cursor, *local = NULL, *address = NULL;
	hx_os_interface_info info = {0};
	hx_os_interface_address item = {0};
	struct ifreq request;
	char name[IFNAMSIZ];
	size_t remaining, bytes;

	if (header->nlmsg_len < NLMSG_LENGTH(sizeof(*message)))
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	message = (const struct ifaddrmsg *)NLMSG_DATA(header);
	if (message->ifa_family != AF_INET && message->ifa_family != AF_INET6)
		return HX_OS_OK;

	bytes = message->ifa_family == AF_INET ? 4 : 16;
	if (message->ifa_prefixlen > bytes * 8)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	cursor = (const unsigned char *)message + NLMSG_ALIGN(sizeof(*message));
	remaining = header->nlmsg_len - NLMSG_LENGTH(sizeof(*message));
	while (remaining) {
		const struct rtattr *attribute;
		size_t step;

		if (remaining < sizeof(*attribute))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		attribute = (const struct rtattr *)cursor;
		if (attribute->rta_len < RTA_LENGTH(0) ||
			attribute->rta_len > remaining)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		if (attribute->rta_type == IFA_ADDRESS ||
			attribute->rta_type == IFA_LOCAL) {
			if (RTA_PAYLOAD(attribute) < bytes)
				return hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);

			if (attribute->rta_type == IFA_LOCAL)
				local = RTA_DATA(attribute);
			else
				address = RTA_DATA(attribute);
		}

		step = RTA_ALIGN(attribute->rta_len);
		if (step > remaining) {
			if (attribute->rta_len != remaining)
				return hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);
			step = remaining;
		}
		cursor += step;
		remaining -= step;
	}

	// IFA_ADDRESS may be the peer of a point-to-point IPv4 interface.
	if (local)
		address = local;
	if (!address)
		return HX_OS_OK;

	if (!if_indextoname(message->ifa_index, name))
		return hx_os_i_errno(error, errno ? errno : ENODEV);

	memset(&request, 0, sizeof(request));
	memcpy(request.ifr_name, name, strlen(name) + 1);
	if (ioctl(query, SIOCGIFFLAGS, &request))
		return hx_os_i_errno(error, errno);

	info.flags = hx_os_i_interface_flags((unsigned short)request.ifr_flags);
	if (!ioctl(query, SIOCGIFMTU, &request) && request.ifr_mtu > 0)
		info.mtu = (uint32_t)request.ifr_mtu;
	info.name = name;
	info.display_name = name;
	info.ipv4_index = message->ifa_index;
	info.ipv6_index = message->ifa_index;
	item.address.family = bytes == 4 ? HX_OS_IPV4 : HX_OS_IPV6;
	item.prefix_length = message->ifa_prefixlen;
	memcpy(item.address.bytes, address, bytes);
	if (bytes == 16 && address[0] == 0xfe &&
		(address[1] & 0xc0) == 0x80)
		item.address.scope_id = message->ifa_index;

	return hx_os_i_interface_append(list, &info, &item, error);
}

static hx_os_status hx_os_interface_packet(int query,
	hx_os_interface_list *list, const unsigned char *packet, size_t bytes,
	int *done, hx_os_error *error)
{
	while (bytes) {
		const struct nlmsghdr *header;
		hx_os_status status;
		size_t step;
		int code;

		if (bytes < sizeof(*header))
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		header = (const struct nlmsghdr *)packet;
		if (header->nlmsg_len < sizeof(*header) ||
			header->nlmsg_len > bytes || header->nlmsg_seq != 1)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		if (header->nlmsg_flags & NLM_F_DUMP_INTR)
			return hx_os_i_error(error, HX_OS_BUSY, 0, 0);

		if (header->nlmsg_type == NLMSG_DONE ||
			header->nlmsg_type == NLMSG_ERROR) {
			code = 0;
			if (header->nlmsg_len >= NLMSG_LENGTH(sizeof(code)))
				memcpy(&code, NLMSG_DATA(header), sizeof(code));
			else if (header->nlmsg_type == NLMSG_ERROR)
				return hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);

			if (code > 0 || code == INT_MIN)
				return hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);
			if (code < 0)
				return hx_os_i_errno(error, -code);
			if (header->nlmsg_type == NLMSG_DONE) {
				*done = 1;

				return HX_OS_OK;
			}
		}
		else if (header->nlmsg_type == NLMSG_OVERRUN)
			return hx_os_i_errno(error, ENOBUFS);
		else if (header->nlmsg_type == RTM_NEWADDR) {
			status = hx_os_interface_address_message(
				query, list, header, error);
			if (status != HX_OS_OK)
				return status;
		}

		step = NLMSG_ALIGN(header->nlmsg_len);
		if (step > bytes) {
			if (header->nlmsg_len != bytes)
				return hx_os_i_error(
					error, HX_OS_IO_ERROR, 0, 0);
			step = bytes;
		}
		packet += step;
		bytes -= step;
	}

	return HX_OS_OK;
}

static hx_os_status hx_os_interface_receive(int netlink, int query,
	hx_os_interface_list *list, unsigned char *packet, hx_os_error *error)
{
	hx_os_deadline deadline = hx_os_deadline_after_ms(2000);
	size_t total = 0;
	int done = 0;

	while (!done) {
		struct sockaddr_nl sender;
		struct iovec vector;
		struct msghdr message;
		ssize_t received;
		hx_os_status status;

		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);

		status = hx_os_i_socket_wait(
			netlink, HX_OS_WAIT_READ, deadline, error);
		if (status != HX_OS_OK)
			return status;

		memset(&sender, 0, sizeof(sender));
		memset(&message, 0, sizeof(message));
		vector.iov_base = packet;
		vector.iov_len = HX_OS_I_INTERFACE_PACKET;
		message.msg_name = &sender;
		message.msg_namelen = sizeof(sender);
		message.msg_iov = &vector;
		message.msg_iovlen = 1;
		received = recvmsg(netlink, &message, 0);
		if (received < 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;

			return hx_os_i_errno(error, errno);
		}
		if (message.msg_flags & MSG_TRUNC)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		if (!received ||
			message.msg_namelen < (socklen_t)sizeof(sender) ||
			sender.nl_family != AF_NETLINK || sender.nl_pid != 0)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		total += (size_t)received;
		if (total > HX_OS_I_INTERFACE_BYTES)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		status = hx_os_interface_packet(query, list, packet,
			(size_t)received, &done, error);
		if (status != HX_OS_OK)
			return status;
	}

	return HX_OS_OK;
}

hx_os_status hx_os_i_interfaces_native(hx_os_context *context,
	hx_os_interface_list *list, hx_os_error *error)
{
	struct {
		struct nlmsghdr header;
		struct ifaddrmsg address;
	} request;
	struct sockaddr_nl kernel;
	unsigned char *packet = NULL;
	hx_os_status status;
	int netlink, query;

	netlink = socket(AF_NETLINK,
		SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
	if (netlink < 0)
		return hx_os_i_errno(error, errno);

	query = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (query < 0) {
		status = hx_os_i_errno(error, errno);
		goto done;
	}

	// GETLINK is restricted for Android applications; GETADDR plus ioctl
	// supplies IP-visible interfaces without API 24's getifaddrs symbol.
	memset(&request, 0, sizeof(request));
	memset(&kernel, 0, sizeof(kernel));
	request.header.nlmsg_len = NLMSG_LENGTH(sizeof(request.address));
	request.header.nlmsg_type = RTM_GETADDR;
	request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	request.header.nlmsg_seq = 1;
	request.address.ifa_family = AF_UNSPEC;
	kernel.nl_family = AF_NETLINK;
	if (sendto(netlink, &request, request.header.nlmsg_len, 0,
		(const struct sockaddr *)&kernel, sizeof(kernel)) < 0) {
		status = hx_os_i_errno(error, errno);
		goto done;
	}

	packet = hx_os_i_alloc(context, HX_OS_I_INTERFACE_PACKET, 0);
	if (!packet) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}
	status = hx_os_interface_receive(netlink, query, list, packet, error);

done:
	hx_os_memory_free(packet);
	if (query >= 0)
		close(query);
	close(netlink);

	return status;
}
#endif
