#ifndef HX_C_PLATFORM_CONFIG_FILE_H
#define HX_C_PLATFORM_CONFIG_FILE_H

#include "hx_config.h"
#include "hx_os_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_config_backup_result {
	hx_os_file_save_result backup;
	hx_os_file_save_result primary;
} hx_os_config_backup_result;

/* Backup path is exactly path + ".bak". First serialize the new document,
 * then read/validate the existing primary within the options' limits. If it
 * exists and is valid, atomically publish its exact bytes to the backup, then
 * atomically publish the new primary. A missing primary leaves any backup
 * unchanged. A corrupt primary fails without overwriting a valid old backup.
 * Caller excludes concurrent mutation of these paths, including aliases.
 * These are two publications, not one transaction; no implicit rollback.
 * result is optional, initialized, and records each completed publication
 * and its directory-sync diagnostic even when a later step fails.
 * Requires CONFIG + CONFIG_SERIALIZE + FILE_UTILS. NULL options use defaults.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_save_backup(
	const hx_os_config_document *document, int format, const char *path,
	const hx_os_config_options *options, hx_os_config_backup_result *result,
	hx_os_config_error *error);

/* Explicit recovery: read and parse path + ".bak", then atomically publish
 * those exact bytes to path. No fallback is added to ordinary config_load.
 * Invalid/missing backup leaves the primary unchanged. Success owns *out;
 * failure sets it NULL. Backup itself is retained. result is optional and
 * reports publication/directory-sync separately. Requires CONFIG+FILE_UTILS.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_config_restore_backup(
	hx_os_context *context, int format, const char *path,
	const hx_os_config_options *options, hx_os_config_document **out,
	hx_os_file_save_result *result, hx_os_config_error *error);

#ifdef __cplusplus
}
#endif
#endif
