#ifndef HX_C_PLATFORM_REGISTRY_H
#define HX_C_PLATFORM_REGISTRY_H

#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_registry hx_os_registry;

enum {
	HX_OS_REGISTRY_CURRENT_USER,
	HX_OS_REGISTRY_LOCAL_MACHINE,
	HX_OS_REGISTRY_CLASSES_ROOT,
	HX_OS_REGISTRY_USERS,
	HX_OS_REGISTRY_CURRENT_CONFIG
};

enum {
	HX_OS_REGISTRY_READ = 1,
	HX_OS_REGISTRY_WRITE = 2,
	HX_OS_REGISTRY_CREATE = 4,
	HX_OS_REGISTRY_VIEW32 = 8,
	HX_OS_REGISTRY_VIEW64 = 16,
	HX_OS_REGISTRY_EXCLUSIVE = 32
};

/**
 * \brief Windows extension; other systems/module-off return NOT_SUPPORTED.
 *
 * \details Paths use '\' separators relative to the chosen hive, never
 * filesystem paths. Inputs are strict UTF-8, borrowed for the call. NULL/empty
 * value names select the default value. Flags require READ and/or WRITE; CREATE
 * additionally requires WRITE, EXCLUSIVE requires CREATE. EXCLUSIVE fails with
 * EXISTS for an existing key. VIEW32/VIEW64 are mutually exclusive. No
 * elevation, implicit creation, flush, expansion or process-global view change.
 * Native creation can create parent keys; it is not a transaction. Serialize
 * operations on a key; exclude close. Native ACLs still apply.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_open(hx_os_context *context,
	int root, const char *path, unsigned flags, hx_os_registry **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_open_user(
	hx_os_context *context, const char *path, unsigned flags,
	hx_os_registry **out, hx_os_error *error);

/**
 * \brief REG_SZ/REG_EXPAND_SZ are returned literally, with optional native
 * terminator removed.
 *
 * \details Embedded NUL, odd UTF-16 length and invalid encoding fail. Native
 * string values are bounded to 1 MiB, including a terminator when writing.
 * Reads allow 8 capacity-race retries. Output uses its own context and is
 * unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_read_string(
	hx_os_registry *key, const char *name, hx_string *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_read_u32(hx_os_registry *key,
	const char *name, uint32_t *out, hx_os_error *error);

/**
 * \brief Two-pass convenience over the same strict decoder.
 *
 * \details required includes NUL; insufficient capacity leaves buffer
 * unchanged. A value may change between calls; retry OUTPUT_FULL using the new
 * required size.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_read_string_buffer(
	hx_os_registry *key, const char *name, char *buffer, size_t capacity,
	size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_write_u32(hx_os_registry *key,
	const char *name, uint32_t value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_write_string(
	hx_os_registry *key, const char *name, const char *value,
	hx_os_error *error);

/**
 * \brief Consumes *key even on native close error, as file_close does.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_registry_close(
	hx_os_registry **key, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
