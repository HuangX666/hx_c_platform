#include "net_interfaces_private.h"

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>

static uint8_t hx_os_interface_prefix(const struct sockaddr *mask)
{
	hx_os_address address;
	size_t i, bytes;
	unsigned bit, count = 0;
	int zero = 0;

	if (!mask || !hx_os_i_address_public(mask, &address))
		return 255;

	bytes = address.family == HX_OS_IPV4 ? 4 : 16;
	for (i = 0; i < bytes; ++i) {
		for (bit = 128; bit; bit >>= 1) {
			if (address.bytes[i] & bit) {
				if (zero)
					return 255;
				++count;
			}
			else
				zero = 1;
		}
	}

	return (uint8_t)count;
}

hx_os_status hx_os_i_interfaces_native(hx_os_context *context,
	hx_os_interface_list *list, hx_os_error *error)
{
	struct ifaddrs *head = NULL, *item;
	hx_os_status status = HX_OS_OK;
	size_t native_bytes = 0;
	int query;

	(void)context;
	if (getifaddrs(&head))
		return hx_os_i_errno(error, errno);

#if defined(SOCK_CLOEXEC)
	query = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
#else
	query = socket(AF_INET, SOCK_DGRAM, 0);
	if (query >= 0 && fcntl(query, F_SETFD, FD_CLOEXEC)) {
		close(query);
		query = -1;
	}
#endif
	for (item = head; item; item = item->ifa_next) {
		hx_os_interface_info info = {0};
		hx_os_interface_address address = {0};
		struct ifreq request;
		unsigned index;

		native_bytes += sizeof(*item) +
			3 * sizeof(struct sockaddr_storage);
		if (native_bytes > HX_OS_I_INTERFACE_BYTES) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}

		if (!item->ifa_addr || !item->ifa_name ||
			!hx_os_i_address_public(
				item->ifa_addr, &address.address))
			continue;

		index = if_nametoindex(item->ifa_name);
		if (!index) {
			status = hx_os_i_errno(error, errno ? errno : ENODEV);
			break;
		}

		address.address.port = 0;
		address.prefix_length =
			hx_os_interface_prefix(item->ifa_netmask);
		info.name = item->ifa_name;
		info.display_name = item->ifa_name;
		info.ipv4_index = index;
		info.ipv6_index = index;
		info.flags = hx_os_i_interface_flags(item->ifa_flags);

		memset(&request, 0, sizeof(request));
		strncpy(request.ifr_name, item->ifa_name, IFNAMSIZ - 1);
		if (query >= 0 && !ioctl(query, SIOCGIFMTU, &request) &&
			request.ifr_mtu > 0)
			info.mtu = (uint32_t)request.ifr_mtu;

		status = hx_os_i_interface_append(list, &info, &address, error);
		if (status != HX_OS_OK)
			break;
	}
	if (query >= 0)
		close(query);
	freeifaddrs(head);

	return status;
}
#endif
