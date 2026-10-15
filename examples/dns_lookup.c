#include <hx_os.h>
#include "network_demo.h"

int main(int argc, char **argv)
{
	hx_os_resolver_options options = HX_OS_RESOLVER_OPTIONS_INIT;
	hx_os_resolver *resolver = NULL;
	hx_os_address_list *addresses = NULL;
	hx_os_error error = {0};
	hx_os_status status;
	size_t i;
	int ok;

	if (argc < 2 || argc > 3) {
		fputs("Usage: dns_lookup HOST [DNS_IP:PORT]\n", stderr);
		return 2;
	}
	if (argc == 3)
		options.servers = argv[2];
	status = hx_os_resolver_create(NULL, &options, &resolver, &error);
	if (status == HX_OS_OK)
		status = hx_os_resolver_resolve_until(resolver, argv[1], 443,
			HX_OS_TCP, hx_os_deadline_after_ms(2000), NULL,
			&addresses, &error);
	ok = net_demo_check(status, &error, "resolve host");
	for (i = 0; ok && i < hx_os_address_list_count(addresses); ++i) {
		const hx_os_address *address =
			hx_os_address_list_get(addresses, i);
		char text[64];

		status = hx_os_address_format(
			address, text, sizeof(text), &error);
		ok = net_demo_check(status, &error, "format address");
		if (ok)
			printf("IPv%u %s port=%u\n",
				(unsigned int)address->family, text,
				(unsigned int)address->port);
	}
	hx_os_address_list_destroy(addresses);
	if (resolver) {
		status = hx_os_resolver_destroy(&resolver, &error);
		if (!net_demo_check(status, &error, "close resolver"))
			ok = 0;
	}

	return ok ? 0 : 1;
}
