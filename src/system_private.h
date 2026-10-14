#ifndef HX_C_PLATFORM_SYSTEM_PRIVATE_H
#define HX_C_PLATFORM_SYSTEM_PRIVATE_H

#include "internal.h"
#include "hx_os_system.h"

#define HX_OS_I_SYSTEM_BYTES (4u * 1024u * 1024u)
#define HX_OS_I_SYSTEM_ITEMS 65536u

struct hx_os_string_list {
	size_t count;
	const char **items;
};

hx_os_status hx_os_i_strings_from_block(hx_os_context *context,
	const char *block, size_t bytes, int validate_utf8,
	hx_os_string_list **out, hx_os_error *error);

hx_os_status hx_os_i_arguments(
	hx_os_context *context, hx_os_string_list **out, hx_os_error *error);

hx_os_status hx_os_i_host_name(hx_string *out, hx_os_error *error);

hx_os_status hx_os_i_emulated(int *out, hx_os_error *error);

uint64_t hx_os_i_process_id(void);

uint64_t hx_os_i_cpu_native_features(void);

/**
 * \brief malloc-owned NUL-separated entries, bytes excludes any extra sentinel.
 *
 * \details POSIX inheritance preserves raw bytes; public enumeration validates
 * UTF-8.
 */
hx_os_status hx_os_i_env_snapshot(
	char **block, size_t *bytes, hx_os_error *error);
#endif
