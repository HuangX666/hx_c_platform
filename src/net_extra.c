#include "net_private.h"
#include "hx_os_net_extra.h"
#include <stdio.h>
#if defined(_WIN32)
#include <iphlpapi.h>
#else
#include <net/if.h>
#endif

static int hx_os_address_valid(const hx_os_address *address)
{
	return address &&
		(address->family == HX_OS_IPV4 ||
			address->family == HX_OS_IPV6);
}

int hx_os_address_is_ipv4_mapped(const hx_os_address *address)
{
	static const unsigned char prefix[12] = {
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

	return address && address->family == HX_OS_IPV6 &&
		!memcmp(address->bytes, prefix, sizeof(prefix));
}

hx_os_status hx_os_address_map_ipv4(
	const hx_os_address *address, hx_os_address *out, hx_os_error *error)
{
	hx_os_address result = {0};

	if (!address || !out ||
		(address->family != HX_OS_IPV4 &&
			!hx_os_address_is_ipv4_mapped(address)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (address->family == HX_OS_IPV6)
		result = *address;
	else {
		result.family = HX_OS_IPV6;
		result.port = address->port;
		result.bytes[10] = result.bytes[11] = 255;
		memcpy(result.bytes + 12, address->bytes, 4);
	}
	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_address_unmap_ipv4(
	const hx_os_address *address, hx_os_address *out, hx_os_error *error)
{
	hx_os_address result = {0};

	if (!address || !out ||
		(address->family != HX_OS_IPV4 &&
			(!hx_os_address_is_ipv4_mapped(address) ||
				address->scope_id)))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	result.family = HX_OS_IPV4;
	result.port = address->port;
	memcpy(result.bytes,
		address->bytes + (address->family == HX_OS_IPV6 ? 12 : 0), 4);
	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

int hx_os_address_equal(const hx_os_address *left, const hx_os_address *right,
	int normalize_mapped)
{
	hx_os_address a, b;

	if (!hx_os_address_valid(left) || !hx_os_address_valid(right))
		return 0;

	a = *left;
	b = *right;
	if (normalize_mapped) {
		(void)hx_os_address_unmap_ipv4(&a, &a, NULL);
		(void)hx_os_address_unmap_ipv4(&b, &b, NULL);
	}

	return a.family == b.family && a.port == b.port &&
		(a.family == HX_OS_IPV4 || a.scope_id == b.scope_id) &&
		!memcmp(a.bytes, b.bytes, a.family == HX_OS_IPV4 ? 4 : 16);
}

#if HX_OS_ENABLE_NETWORK
static int hx_os_endpoint_number(
	const char *text, uint32_t maximum, uint32_t *out)
{
	uint32_t value = 0;

	if (!*text)
		return 0;
	for (; *text; ++text) {
		unsigned digit = (unsigned char)*text - (unsigned)'0';

		if (digit > 9 || value > (maximum - digit) / 10)
			return 0;
		value = value * 10 + digit;
	}
	*out = value;

	return 1;
}

#endif

hx_os_status hx_os_endpoint_parse(
	const char *text, hx_os_address *out, hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK
	char copy[256], *host, *port, *zone;
	hx_os_address result;
	size_t length = 0;
	uint32_t number, index = 0;
	int bracketed;
	hx_os_status status;

	if (!text || !out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	while (length < sizeof(copy) && text[length])
		++length;
	if (length == sizeof(copy))
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	memcpy(copy, text, length + 1);
	bracketed = copy[0] == '[';
	host = copy + bracketed;
	port = bracketed ? strchr(host, ']') : strchr(host, ':');
	if (!port || (bracketed && port[1] != ':'))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*port = 0;
	port += bracketed ? 2 : 1;
	if (!hx_os_endpoint_number(port, UINT16_MAX, &number))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	zone = strchr(host, '%');
	if (zone) {
		*zone++ = 0;
		if (!bracketed || !*zone)
			return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
		if (!hx_os_endpoint_number(zone, UINT32_MAX, &index)) {
			if (strspn(zone, "0123456789") == strlen(zone))
				return hx_os_i_error(
					error, HX_OS_INVALID, 0, 0);
#if HX_OS_ENABLE_NET_OPTIONS
			index = if_nametoindex(zone);
			if (!index)
#if defined(_WIN32)
				return hx_os_i_error(error, HX_OS_NOT_FOUND,
					HX_OS_ERROR_WIN32, GetLastError());
#else
				return hx_os_i_error(error, HX_OS_NOT_FOUND,
					HX_OS_ERROR_ERRNO, errno);
#endif
#else
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
		}
	}
	status = hx_os_address_parse(host, (uint16_t)number, &result, error);
	if (status != HX_OS_OK)
		return status;
	if ((result.family == HX_OS_IPV6) != bracketed)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	result.scope_id = index;
	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)text;
	(void)out;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_endpoint_format(const hx_os_address *address, char *buffer,
	size_t capacity, hx_os_error *error)
{
	char host[64], result[96];
	int count;
	hx_os_status status;

	if (!buffer || !capacity)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	status = hx_os_address_format(address, host, sizeof(host), error);
	if (status != HX_OS_OK)
		return status;
	if (address->family == HX_OS_IPV4)
		count = snprintf(result, sizeof(result), "%s:%u", host,
			(unsigned)address->port);
	else if (address->scope_id)
		count = snprintf(result, sizeof(result), "[%s%%%u]:%u", host,
			(unsigned)address->scope_id, (unsigned)address->port);
	else
		count = snprintf(result, sizeof(result), "[%s]:%u", host,
			(unsigned)address->port);

	if (count < 0 || (size_t)count >= sizeof(result))
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);
	if ((size_t)count >= capacity)
		return hx_os_i_error(error, HX_OS_OUTPUT_FULL, 0, 0);

	memcpy(buffer, result, (size_t)count + 1);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_route_query(hx_os_network *network,
	const hx_os_address *destination, hx_os_route_info *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_NETWORK && HX_OS_ENABLE_NET_OPTIONS
	hx_os_route_info result = {0};
	hx_os_address target;

	if (!network || !out || !hx_os_address_valid(destination))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	target = *destination;
	(void)hx_os_address_unmap_ipv4(&target, &target, NULL);

#if defined(_WIN32)
	{
		struct sockaddr_storage native;
		hx_os_i_socklen length;
		MIB_IPFORWARD_ROW2 route;
		SOCKADDR_INET source;
		DWORD code;

		(void)hx_os_i_address_native(&target, &native, &length);
		code = GetBestRoute2(NULL, 0, NULL, (SOCKADDR_INET *)&native, 0,
			&route, &source);
		if (code)
			return hx_os_i_win32(error, code);
		if (!hx_os_i_address_public(
			    (struct sockaddr *)&source, &result.source))
			return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
		result.interface_index = route.InterfaceIndex;
	}
#else
	{
		hx_os_socket *socket = NULL;
		hx_os_interface_list *list = NULL;
		hx_os_status status;
		size_t i, j;

		if (!target.port)
			target.port = 9;
		status = hx_os_socket_create(
			network, target.family, HX_OS_UDP, &socket, error);
		if (status != HX_OS_OK)
			return status;

		status = hx_os_socket_connect(socket, &target, error);
		if (status == HX_OS_OK)
			status = hx_os_socket_local_address(
				socket, &result.source, error);
		hx_os_i_socket_discard(&socket);
		if (status != HX_OS_OK)
			return status;

		result.source.port = 0;
		status = hx_os_interfaces_query(network->context, &list, error);
		if (status != HX_OS_OK)
			return status;

		for (i = 0; i < hx_os_interface_list_count(list); ++i) {
			const hx_os_interface_info *info =
				hx_os_interface_list_get(list, i);
			uint32_t index = result.source.family == HX_OS_IPV4
				? info->ipv4_index
				: info->ipv6_index;

			for (j = 0; j < info->address_count; ++j) {
				if (!hx_os_address_equal(&result.source,
					    &info->addresses[j].address, 0))
					continue;
				if (result.interface_index &&
					result.interface_index != index)
					status = HX_OS_NOT_SUPPORTED;
				result.interface_index = index;
			}
		}
		hx_os_interface_list_destroy(list);
		if (status != HX_OS_OK || !result.interface_index)
			return hx_os_i_error(error,
				status != HX_OS_OK ? status : HX_OS_NOT_FOUND,
				0, 0);
	}
#endif
	result.source.port = 0;
	*out = result;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)network;
	(void)destination;
	(void)out;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
