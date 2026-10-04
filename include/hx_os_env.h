#ifndef HX_C_PLATFORM_ENV_H
#define HX_C_PLATFORM_ENV_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Names and values are borrowed NUL-terminated UTF-8. Names cannot be empty
 * or contain '='. An empty value is distinct from a missing name (NOT_FOUND).
 * required includes NUL. String outputs retain their value on failure.
 * Library environment operations serialize with each other. The host must
 * additionally exclude concurrent native/CRT/other-library mutations.
 * Changes affect the process and future children, not an existing child or
 * the parent process. Name case follows the OS. No locale is changed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_env_get(const char *name, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_env_get_string(
	const char *name, hx_string *out, hx_os_error *error);

/* replace=0 preserves an existing value and succeeds. NULL is not a value. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_env_set(
	const char *name, const char *value, int replace, hx_os_error *error);

/* Removing a missing name succeeds. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_env_remove(
	const char *name, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_env_visit_fn)(
	const char *name, const char *value, void *user);

/* Snapshot bounded to 4 MiB/65536 entries. Validate before calling visitors;
 * callbacks run outside the environment lock and may modify the environment.
 * Name/value pointers are borrowed only for that invocation. A callback's
 * non-OK status stops iteration. Windows hidden '=X:' drive entries are
 * omitted. External native/CRT environment mutation still needs exclusion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_env_enumerate(hx_os_context *context,
	hx_os_env_visit_fn function, void *user, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
