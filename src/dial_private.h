#ifndef HX_C_PLATFORM_DIAL_PRIVATE_H
#define HX_C_PLATFORM_DIAL_PRIVATE_H

#include "net_private.h"
#include "hx_os_dial.h"

hx_os_status hx_os_i_tcp_options_validate(
	const hx_os_tcp_options *options, hx_os_error *error);
hx_os_status hx_os_i_tcp_dial_addresses_until(hx_os_network *network,
	const hx_os_address *addresses, size_t count,
	const hx_os_dial_options *options, hx_os_deadline deadline,
	int (*cancelled)(void *), void *user, hx_os_socket **out,
	hx_os_error *error);

#endif
