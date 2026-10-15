#ifndef HX_C_PLATFORM_RESOLVER_PRIVATE_H
#define HX_C_PLATFORM_RESOLVER_PRIVATE_H

#include "hx_os_resolver.h"

hx_os_status hx_os_i_resolver_resolve_until(hx_os_resolver *resolver,
	const char *host, uint16_t port, int protocol, hx_os_deadline deadline,
	hx_os_cancel_token *cancel, int (*cancelled)(void *), void *user,
	hx_os_address_list **out, hx_os_error *error);

#endif
