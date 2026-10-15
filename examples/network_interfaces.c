#include <hx_os.h>
#include "network_demo.h"

int main(void)
{
	hx_os_interface_list *interfaces = NULL;
	hx_os_error error = {0};

	hx_os_status status =
		hx_os_interfaces_query(NULL, &interfaces, &error);
	if (!net_demo_check(status, &error, "query interfaces"))
		return 1;

	printf("IP-visible interfaces: %zu\n",
		hx_os_interface_list_count(interfaces));
	for (size_t i = 0; i < hx_os_interface_list_count(interfaces); ++i) {
		const hx_os_interface_info *info =
			hx_os_interface_list_get(interfaces, i);

		printf("%s (%s)\n", info->name, info->display_name);
		printf("  IPv4 index=%u, IPv6 index=%u, MTU=%u\n",
			(unsigned int)info->ipv4_index,
			(unsigned int)info->ipv6_index,
			(unsigned int)info->mtu);
		printf("  up=%d, multicast=%d, loopback=%d\n",
			!!(info->flags & HX_OS_INTERFACE_UP),
			!!(info->flags & HX_OS_INTERFACE_MULTICAST),
			!!(info->flags & HX_OS_INTERFACE_LOOPBACK));

		for (size_t j = 0; j < info->address_count; ++j) {
			const hx_os_interface_address *entry =
				&info->addresses[j];
			char text[64];

			status = hx_os_address_format(
				&entry->address, text, sizeof(text), &error);
			if (!net_demo_check(status, &error, "format address"))
				break;

			printf("  %s", text);
			if (entry->prefix_length == 255)
				printf("/unknown");
			else
				printf("/%u",
					(unsigned int)entry->prefix_length);
			printf(" (IPv%u, scope=%u)\n",
				(unsigned int)entry->address.family,
				(unsigned int)entry->address.scope_id);
		}
		if (status != HX_OS_OK)
			break;
	}

	hx_os_interface_list_destroy(interfaces);

	return status == HX_OS_OK ? 0 : 1;
}
