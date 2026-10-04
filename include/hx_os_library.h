#ifndef HX_C_PLATFORM_LIBRARY_H
#define HX_C_PLATFORM_LIBRARY_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_library hx_os_library;

enum { HX_OS_LIBRARY_LOCAL = 0, HX_OS_LIBRARY_GLOBAL = 1 };

/*
 * Loads a borrowed UTF-8 filesystem path, relative to cwd when not absolute.
 * POSIX resolves relocations immediately; GLOBAL publishes loaded symbols.
 * Windows ignores LOCAL/GLOBAL, using its native dependency search policy.
 * Never changes the process loader search path. out is NULL on failure.
 * Serialize use/close of a handle. Symbols borrow the loaded module until
 * close; finish every call, object, callback and TLS destructor before unload.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_library_open(hx_os_context *context,
	const char *path, unsigned flags, hx_os_library **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_library_symbol(hx_os_library *library,
	const char *name, void **address, hx_os_error *error);

/* Writes a native pointer into pointer storage; unchanged on failure. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_library_bind(hx_os_library *library,
	const char *name, void *pointer, size_t bytes, hx_os_error *error);

/* Consumes *library even on native close failure; NULL handles are harmless. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_library_close(
	hx_os_library **library, hx_os_error *error);

/*
 * Optional initialized diagnostic receives call-owned UTF-8, not dlerror's
 * borrowed storage. Captures up to 2047 native bytes with a truncation marker;
 * invalid UTF-8 bytes are escaped. Text allocation is best effort: the
 * operation's primary status is never replaced by a diagnostic failure.
 * Cleared on entry. No process-global or thread-local last-error storage.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_library_open_ex(hx_os_context *context,
	const char *path, unsigned flags, hx_os_library **out,
	hx_string *diagnostic, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_library_symbol_ex(
	hx_os_library *library, const char *name, void **address,
	hx_string *diagnostic, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_library_close_ex(
	hx_os_library **library, hx_string *diagnostic, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_library_bind_ex(hx_os_library *library,
	const char *name, void *pointer, size_t bytes, hx_string *diagnostic,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
