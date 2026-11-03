#ifndef HX_C_PLATFORM_LOCAL_DUMPS_PRIVATE_H
#define HX_C_PLATFORM_LOCAL_DUMPS_PRIVATE_H

#include "internal.h"
#include "hx_os_local_dumps.h"

#if defined(_WIN32) && HX_OS_ENABLE_LOCAL_DUMPS
hx_os_status hx_os_i_local_dumps_change(const char *application,
	const hx_os_local_dumps_options *options, int remove,
	hx_os_error *error);
hx_os_status hx_os_i_local_dumps_query(const char *application,
	unsigned registry_view, hx_os_local_dumps_info *out, hx_string *folder,
	hx_os_error *error);
#endif

#endif
