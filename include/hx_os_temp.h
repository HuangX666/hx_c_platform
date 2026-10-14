#ifndef HX_C_PLATFORM_TEMP_H
#define HX_C_PLATFORM_TEMP_H

#include "hx_os_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_temp hx_os_temp;

/**
 * \brief Atomically creates a random exclusive file/directory in a trusted
 * parent.
 *
 * \details parent=NULL uses the native temporary directory (requires PATHS);
 * prefix NULL uses "hx", otherwise at most 64 UTF-8 bytes, no separators/colon.
 * Does not create parent directories. POSIX files/directories use 0600/0700;
 * Windows inherits parent ACLs. Not a sandbox for an adversarial parent.
 * Externally serialize calls and borrowed file I/O with cleanup/publication.
 */
HX_OS_API int HX_OS_CALL hx_os_temp_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_temp_create_file(hx_os_context *context,
	const char *parent, const char *prefix, hx_os_temp **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_temp_create_directory(
	hx_os_context *context, const char *parent, const char *prefix,
	hx_os_temp **out, hx_os_error *error);
HX_OS_API const char *HX_OS_CALL hx_os_temp_path(const hx_os_temp *temp);
HX_OS_API hx_os_file *HX_OS_CALL hx_os_temp_file(const hx_os_temp *temp);
/**
 * \brief keep suppresses removal at close, without moving or syncing anything.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_temp_keep(
	hx_os_temp *temp, hx_os_error *error);
/**
 * \brief Closes file, then same-filesystem rename.
 *
 * \details No copy fallback or implicit durability. Failure retains owner/path
 * for cleanup or publication retry; the file handle may already have been
 * consumed. Success keeps destination.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_temp_publish(hx_os_temp *temp,
	const char *destination, int replace, hx_os_error *error);
/**
 * \brief Closes and removes unless kept.
 *
 * \details Directory cleanup uses the existing bounded no-follow recursive
 * removal. Failure keeps owner for retry; some entries may already be removed.
 * Always check before losing the last owner.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_temp_close(
	hx_os_temp **temp, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
