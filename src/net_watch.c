#include "net_private.h"
#include "hx_os_net_interfaces.h"

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
#if defined(_WIN32)
#include <iphlpapi.h>
#elif defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#elif defined(__APPLE__)
#include <net/route.h>
#endif

struct hx_os_interface_watch {
#if defined(_WIN32)
	HANDLE event, address_change, interface_change;
#else
	int native;
#endif
};

#if defined(_WIN32)
static void CALLBACK hx_os_interface_address_changed(void *context,
	MIB_UNICASTIPADDRESS_ROW *row, MIB_NOTIFICATION_TYPE type)
{
	hx_os_interface_watch *watch = context;

	(void)row;
	(void)type;
	(void)SetEvent(watch->event);
}

static void CALLBACK hx_os_interface_changed(
	void *context, MIB_IPINTERFACE_ROW *row, MIB_NOTIFICATION_TYPE type)
{
	hx_os_interface_watch *watch = context;

	(void)row;
	(void)type;
	(void)SetEvent(watch->event);
}
#endif
#endif

hx_os_status hx_os_interface_watch_create(
	hx_os_context *context, hx_os_interface_watch **out, hx_os_error *error)
{
	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	{
		hx_os_interface_watch *watch =
			hx_os_i_zero(context, sizeof(*watch));
		hx_os_status status = HX_OS_OK;

		if (!watch)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
#if defined(_WIN32)
		{
			DWORD code;

			watch->event = CreateEventW(NULL, FALSE, FALSE, NULL);
			if (!watch->event) {
				status = hx_os_i_win32(error, GetLastError());
				hx_os_memory_free(watch);

				return status;
			}
			code = NotifyUnicastIpAddressChange(AF_UNSPEC,
				hx_os_interface_address_changed, watch, FALSE,
				&watch->address_change);
			if (!code)
				code = NotifyIpInterfaceChange(AF_UNSPEC,
					hx_os_interface_changed, watch, FALSE,
					&watch->interface_change);
			if (code) {
				status = hx_os_interface_watch_close(
					&watch, error);
				if (status != HX_OS_OK) {
					*out = watch;

					return status;
				}
				return hx_os_i_win32(error, code);
			}
		}
#else
#if defined(__linux__)
		watch->native = socket(AF_NETLINK,
			SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
#else
		watch->native = socket(PF_ROUTE, SOCK_RAW, 0);
#endif
		if (watch->native < 0)
			status = hx_os_i_errno(error, errno);
#if defined(__linux__)
		else {
			struct sockaddr_nl address;

			memset(&address, 0, sizeof(address));
			address.nl_family = AF_NETLINK;
			address.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR |
				RTMGRP_IPV6_IFADDR;
			if (bind(watch->native, (struct sockaddr *)&address,
				    sizeof(address)))
				status = hx_os_i_errno(error, errno);
		}
#else
		else if (fcntl(watch->native, F_SETFL, O_NONBLOCK) ||
			fcntl(watch->native, F_SETFD, FD_CLOEXEC))
			status = hx_os_i_errno(error, errno);
#endif
		if (status != HX_OS_OK && watch->native >= 0)
			(void)close(watch->native);
#endif
		if (status != HX_OS_OK) {
			hx_os_memory_free(watch);

			return status;
		}
		*out = watch;

		return hx_os_i_error(error, HX_OS_OK, 0, 0);
	}
#else
	(void)context;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_interface_watch_wait_until(hx_os_interface_watch *watch,
	hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	if (!watch)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	for (;;) {
#if defined(_WIN32)
		DWORD result = WaitForSingleObject(
			watch->event, hx_os_i_wait_ms(deadline));

		if (result == WAIT_OBJECT_0)
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (result == WAIT_FAILED)
			return hx_os_i_win32(error, GetLastError());
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
#else
		unsigned char data[4096];
		ssize_t count;
		hx_os_status status = hx_os_i_socket_wait(
			watch->native, HX_OS_WAIT_READ, deadline, error);

		if (status != HX_OS_OK)
			return status;
		count = recv(watch->native, data, sizeof(data), 0);
		// A bounded hint suffices; native payloads never cross the API.
		if (count > 0 || (count < 0 && errno == ENOBUFS))
			return hx_os_i_error(error, HX_OS_OK, 0, 0);
		if (!count)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
			return hx_os_i_errno(error, errno);
		if (hx_os_deadline_expired(deadline))
			return hx_os_i_error(error, HX_OS_TIMEOUT, 0, 0);
#endif
	}
#else
	(void)watch;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_interface_watch_close(
	hx_os_interface_watch **owner, hx_os_error *error)
{
	if (!owner)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (!*owner)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	{
		hx_os_interface_watch *watch = *owner;
#if defined(_WIN32)
		DWORD code;

		if (watch->address_change) {
			code = CancelMibChangeNotify2(watch->address_change);
			if (code)
				return hx_os_i_win32(error, code);
			watch->address_change = NULL;
		}
		if (watch->interface_change) {
			code = CancelMibChangeNotify2(watch->interface_change);
			if (code)
				return hx_os_i_win32(error, code);
			watch->interface_change = NULL;
		}
		if (!CloseHandle(watch->event))
			return hx_os_i_win32(error, GetLastError());
#else
		int code = close(watch->native) ? errno : 0;
#endif
		hx_os_memory_free(watch);
		*owner = NULL;
#if !defined(_WIN32)
		if (code)
			return hx_os_i_errno(error, code);
#endif
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
