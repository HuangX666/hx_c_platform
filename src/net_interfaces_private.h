#ifndef HX_C_PLATFORM_NET_INTERFACES_PRIVATE_H
#define HX_C_PLATFORM_NET_INTERFACES_PRIVATE_H

#include "net_private.h"
#include "hx_os_net_interfaces.h"

#define HX_OS_I_INTERFACE_BYTES (16u * 1024u * 1024u)

hx_os_status hx_os_i_interfaces_native(hx_os_context *context,
	hx_os_interface_list *list, hx_os_error *error);

hx_os_status hx_os_i_interface_append(hx_os_interface_list *list,
	const hx_os_interface_info *info,
	const hx_os_interface_address *address, hx_os_error *error);

#if !defined(_WIN32)
uint32_t hx_os_i_interface_flags(unsigned flags);
#endif

#endif
