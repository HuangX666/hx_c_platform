#include "net_interfaces_private.h"

#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
#include <iphlpapi.h>

static hx_os_status hx_os_interface_display(hx_os_context *context,
	const wchar_t *name, char **out, hx_os_error *error)
{
	int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		name ? name : L"", -1, NULL, 0, NULL, NULL);

	if (!count)
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING,
			HX_OS_ERROR_WIN32, GetLastError());

	if (count > 4097)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	*out = hx_os_i_alloc(context, (size_t)count, 0);
	if (!*out)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		name ? name : L"", -1, *out, count, NULL, NULL))
		return hx_os_i_error(error, HX_OS_INVALID_ENCODING,
			HX_OS_ERROR_WIN32, GetLastError());

	return HX_OS_OK;
}

static hx_os_status hx_os_interface_adapter(hx_os_context *context,
	hx_os_interface_list *list, const IP_ADAPTER_ADDRESSES *adapter,
	hx_os_error *error)
{
	const IP_ADAPTER_UNICAST_ADDRESS *unicast;
	hx_os_interface_info info = {0};
	hx_os_status status;
	char *display = NULL;

	if (!adapter->FirstUnicastAddress)
		return HX_OS_OK;

	status = hx_os_interface_display(context, adapter->FriendlyName,
		&display, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(display);

		return status;
	}

	info.name = adapter->AdapterName;
	info.display_name = display;
	info.ipv4_index = adapter->IfIndex;
	info.ipv6_index = adapter->Ipv6IfIndex;
	info.mtu = adapter->Mtu;
	if (adapter->OperStatus == IfOperStatusUp)
		info.flags |= HX_OS_INTERFACE_UP;
	if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
		info.flags |= HX_OS_INTERFACE_LOOPBACK;
	if (!(adapter->Flags & IP_ADAPTER_NO_MULTICAST))
		info.flags |= HX_OS_INTERFACE_MULTICAST;
	if (adapter->IfType == IF_TYPE_PPP)
		info.flags |= HX_OS_INTERFACE_POINT_TO_POINT;
	if (adapter->IfType == IF_TYPE_ETHERNET_CSMACD ||
		adapter->IfType == IF_TYPE_IEEE80211)
		info.flags |= HX_OS_INTERFACE_BROADCAST;

	for (unicast = adapter->FirstUnicastAddress; unicast;
		unicast = unicast->Next) {
		hx_os_interface_address address = {0};
		unsigned limit;

		if (!unicast->Address.lpSockaddr ||
			!hx_os_i_address_public(unicast->Address.lpSockaddr,
				&address.address))
			continue;

		address.address.port = 0;
		limit = address.address.family == HX_OS_IPV4 ? 32u : 128u;
		address.prefix_length = unicast->OnLinkPrefixLength <= limit
			? unicast->OnLinkPrefixLength : 255;
		status = hx_os_i_interface_append(list, &info, &address, error);
		if (status != HX_OS_OK)
			break;
	}
	hx_os_memory_free(display);

	return status;
}

hx_os_status hx_os_i_interfaces_native(hx_os_context *context,
	hx_os_interface_list *list, hx_os_error *error)
{
	IP_ADAPTER_ADDRESSES *buffer = NULL, *adapter;
	ULONG bytes = 15 * 1024, code = ERROR_BUFFER_OVERFLOW;
	ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
		GAA_FLAG_SKIP_DNS_SERVER;
	hx_os_status status = HX_OS_OK;
	unsigned attempt;

	for (attempt = 0; attempt < 4; ++attempt) {
		if (bytes > HX_OS_I_INTERFACE_BYTES) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			break;
		}

		buffer = hx_os_i_alloc(context, bytes, 0);
		if (!buffer) {
			status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
			break;
		}

		code = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, buffer,
			&bytes);
		if (code != ERROR_BUFFER_OVERFLOW)
			break;

		hx_os_memory_free(buffer);
		buffer = NULL;
	}
	if (status == HX_OS_OK && code == ERROR_BUFFER_OVERFLOW)
		status = hx_os_i_error(error, HX_OS_BUSY,
			HX_OS_ERROR_WIN32, code);
	else if (status == HX_OS_OK && code != NO_ERROR &&
		code != ERROR_NO_DATA)
		status = hx_os_i_win32(error, code);

	if (status == HX_OS_OK && code == NO_ERROR) {
		for (adapter = buffer; adapter; adapter = adapter->Next) {
			status = hx_os_interface_adapter(
				context, list, adapter, error);
			if (status != HX_OS_OK)
				break;
		}
	}
	hx_os_memory_free(buffer);

	return status;
}
#endif
