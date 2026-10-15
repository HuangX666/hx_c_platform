#include <hx_os.h>
#include "network_demo.h"

static int print_interfaces(void)
{
	hx_os_interface_list *interfaces = NULL;
	hx_os_error error = {0};

	hx_os_status status = hx_os_interfaces_query(NULL, &interfaces, &error);
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

static int print_route(const char *destination)
{
	hx_os_network *network = NULL;
	hx_os_address target;
	hx_os_route_info route;
	hx_os_error error = {0};
	char source[96];
	hx_os_status status =
		hx_os_endpoint_parse(destination, &target, &error);
	int ok;

	if (status == HX_OS_OK)
		status = hx_os_network_create(NULL, &network, &error);
	if (status == HX_OS_OK)
		status = hx_os_route_query(network, &target, &route, &error);
	if (status == HX_OS_OK)
		status = hx_os_endpoint_format(
			&route.source, source, sizeof(source), &error);
	ok = net_demo_check(status, &error, "select route");
	if (ok)
		printf("Destination %s uses %s on interface %u\n", destination,
			source, (unsigned)route.interface_index);
	if (!net_demo_check(hx_os_network_destroy(&network, &error), &error,
		    "release network"))
		ok = 0;

	return ok ? 0 : 1;
}

static int watch_interfaces(void)
{
	hx_os_interface_watch *watch = NULL;
	hx_os_error error = {0};
	hx_os_status status =
		hx_os_interface_watch_create(NULL, &watch, &error);
	int ok = net_demo_check(
		status, &error, "subscribe to interface changes");

	if (ok)
		ok = print_interfaces() == 0;
	if (ok) {
		puts("Waiting up to 30 seconds for one rescan hint...");
		fflush(stdout);
		status = hx_os_interface_watch_wait_until(
			watch, hx_os_deadline_after_ms(30000), &error);
		if (status == HX_OS_TIMEOUT)
			puts("No interface changes within 30 seconds.");
		else if (net_demo_check(status, &error, "wait for change")) {
			puts("Network changed; querying a fresh snapshot:");
			ok = print_interfaces() == 0;
		}
		else
			ok = 0;
	}
	if (!net_demo_check(hx_os_interface_watch_close(&watch, &error), &error,
		    "unsubscribe interface changes"))
		ok = 0;

	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc == 1)
		return print_interfaces();
	if (argc == 2 && !strcmp(argv[1], "--watch"))
		return watch_interfaces();
	if (argc == 3 && !strcmp(argv[1], "--route"))
		return print_route(argv[2]);

	fputs("Usage: network_interfaces [--watch | --route ENDPOINT]\n",
		stderr);

	return 2;
}
