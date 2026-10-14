#ifndef HX_C_PLATFORM_SHARED_MEMORY_H
#define HX_C_PLATFORM_SHARED_MEMORY_H

#include "hx_os_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_shared_memory hx_os_shared_memory;

/**
 * \brief Name: 1..64 ASCII letters/digits/_/.-.
 *
 * \details Exclusive creation, 1..64 MiB. Default: Windows Local namespace
 * mapping; POSIX /hxcp_NAME shm object. Android lacks shm_open; use
 * create_at/open_at with an explicitly authorized app-private/shared directory.
 * _at uses a file-backed mapping on all targets. No implicit permissions
 * elevation, directory creation, queue or locking. Windows default inherits
 * token ACLs; POSIX creation uses 0600. A 64-byte versioned header precedes the
 * zero-initialized payload. Metadata is published before returning create, not
 * application data. Users must synchronize their payload and keep the
 * header/backing size immutable.
 */
HX_OS_API int HX_OS_CALL hx_os_shared_memory_available(void);
HX_OS_API int HX_OS_CALL hx_os_shared_memory_native_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_create(
	hx_os_context *context, const char *name, size_t bytes,
	hx_os_shared_memory **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_open(
	hx_os_context *context, const char *name, hx_os_shared_memory **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_create_at(
	hx_os_context *context, const char *directory, const char *name,
	size_t bytes, hx_os_shared_memory **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_open_at(
	hx_os_context *context, const char *directory, const char *name,
	hx_os_shared_memory **out, hx_os_error *error);
/**
 * \brief Borrowed, writable, 64-byte-aligned payload until close.
 *
 * \details Opening while the creator has not published metadata returns
 * WOULD_BLOCK for retry. No hard waits or automatic stale-object takeover.
 * Cross-process atomics require native lock-free operation; otherwise this
 * module is NOT_SUPPORTED.
 */
HX_OS_API void *HX_OS_CALL hx_os_shared_memory_data(
	hx_os_shared_memory *memory);
HX_OS_API size_t HX_OS_CALL hx_os_shared_memory_size(
	const hx_os_shared_memory *memory);
/**
 * \brief POSIX names persist until unlink; live mappings survive unlink.
 *
 * \details Windows native names vanish after the last handle closes; explicit
 * unlink returns NOT_SUPPORTED. File-backed names persist and unlink reports
 * native errors.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_unlink(
	const char *name, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_unlink_at(
	const char *directory, const char *name, hx_os_error *error);
/**
 * \brief Exclude view users.
 *
 * \details Failed unmap retains owner; after successful unmap native
 * descriptor-close errors may consume owner. Never implies disk sync.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_shared_memory_close(
	hx_os_shared_memory **memory, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
