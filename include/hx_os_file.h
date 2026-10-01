#ifndef HX_C_PLATFORM_FILE_H
#define HX_C_PLATFORM_FILE_H

#include "hx_os_base.h"
#include "hx_string.h"
#include "hx_container.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_file hx_os_file;
typedef struct hx_os_directory hx_os_directory;

enum {
	HX_OS_FILE_READ = 1u,
	HX_OS_FILE_WRITE = 2u,
	HX_OS_FILE_CREATE = 4u,
	HX_OS_FILE_TRUNCATE = 8u,
	HX_OS_FILE_APPEND = 16u,
	HX_OS_FILE_EXCLUSIVE = 32u
};
enum {
	HX_OS_FILE_REGULAR,
	HX_OS_FILE_DIRECTORY,
	HX_OS_FILE_LINK,
	HX_OS_FILE_OTHER
};
enum { HX_OS_SEEK_BEGIN, HX_OS_SEEK_CURRENT, HX_OS_SEEK_END };

typedef struct hx_os_file_info {
	uint64_t size;
	int64_t modified_seconds;
	uint32_t modified_nanoseconds;
	uint32_t kind;
} hx_os_file_info;

enum { HX_OS_PERMISSION_OWNER_READ = 1u, HX_OS_PERMISSION_OWNER_WRITE = 2u };
typedef struct hx_os_file_info_extended {
	uint32_t size;
	uint32_t version;
	hx_os_file_info basic;
	int64_t accessed_seconds;
	int64_t changed_seconds;
	uint32_t accessed_nanoseconds;
	uint32_t changed_nanoseconds;
	uint32_t read_only;
	uint32_t owner_permissions;
	uint32_t known_permissions;
} hx_os_file_info_extended;
#define HX_OS_FILE_INFO_EXTENDED_INIT                                          \
	{                                                                      \
		sizeof(hx_os_file_info_extended), 1, {0, 0, 0, 0}, 0, 0, 0, 0, \
			0, 0, 0                                                \
	}

/*
 * Times use Unix seconds plus nanoseconds. changed is metadata change,
 * not birth time. Accuracy/update policy follows the filesystem.
 * read_only is the Windows attribute or absence of POSIX owner-write mode.
 * known_permissions marks valid POSIX mode-summary bits (zero on Windows).
 * Neither mode summaries nor read_only are ACL/access-permission checks.
 * Initialize capacity before querying; failure preserves the output.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_info_extended_init(
	hx_os_file_info_extended *info, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_stat_extended(const char *path,
	int follow_links, hx_os_file_info_extended *info, hx_os_error *error);

/*
 * Paths are borrowed, NUL-terminated UTF-8. Native names not representable
 * as Unicode are rejected. No normalization or process-locale changes.
 * POSIX creation uses 0600 (files), 0700 (directories), subject to umask.
 * Open supports regular files. Anonymous pipe factories produce file
 * endpoints for synchronous I/O; opening arbitrary devices is unsupported.
 * File position operations require caller serialization. I/O can block.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_open(hx_os_context *context,
	const char *path, uint32_t flags, hx_os_file **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_read_some(hx_os_file *file,
	void *data, size_t bytes, size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_write_some(hx_os_file *file,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_write_all(hx_os_file *file,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);
/*
 * Positioned single-chunk I/O, at most INT_MAX bytes per call. Offset and
 * transfer size are bytes. Short progress is OK; read with no bytes is EOF.
 * Zero requested bytes is OK. These calls do not modify the shared position
 * and may run concurrently with each other. Coordinate overlapping writes,
 * truncation and close externally. Append-mode writes return NOT_SUPPORTED.
 * Calls block until native completion; there is no cancellation/deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_read_at(hx_os_file *file,
	uint64_t offset, void *data, size_t bytes, size_t *completed,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_write_at(hx_os_file *file,
	uint64_t offset, const void *data, size_t bytes, size_t *completed,
	hx_os_error *error);
/*
 * No user-space buffer is owned by this object. sync requests persistence
 * through fsync/FlushFileBuffers; it does not promise hardware durability.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_sync(
	hx_os_file *file, hx_os_error *error);
/* Consumes *file, including on a close error; never retry the native close. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_close(
	hx_os_file **file, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_size(
	hx_os_file *file, uint64_t *size, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_seek(hx_os_file *file,
	int64_t offset, int origin, uint64_t *position, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_truncate(
	hx_os_file *file, uint64_t size, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_stat(const char *path,
	int follow_links, hx_os_file_info *info, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_remove(
	const char *path, hx_os_error *error);
/* Same-filesystem rename. No implicit copy. Unsupported atomic modes fail. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_move(
	const char *from, const char *to, int replace, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_create(
	const char *path, int parents, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_remove(
	const char *path, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_open(hx_os_context *context,
	const char *path, hx_os_directory **out, hx_os_error *error);
/*
 * Names include a terminating NUL; required includes it. OUTPUT_FULL
 * retains the entry for retry. Invalid-encoding entries are consumed and
 * reported; callers may explicitly continue to the next entry.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_next(
	hx_os_directory *directory, char *name, size_t capacity,
	size_t *required, hx_os_error *error);
/* Like next, with the entry kind without following links. OUTPUT_FULL keeps
 * the pending entry and kind output unchanged. Native lookup failures also
 * retain the entry; legacy next may be used to skip such an entry.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_next_info(
	hx_os_directory *directory, char *name, size_t capacity,
	size_t *required, unsigned *kind, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_directory_close(hx_os_directory *directory);

/*
 * File conveniences are synchronous. Copy/save can leave partial output on
 * failure; copy does not promise metadata preservation or a source snapshot.
 * exists reports OK/0 for a missing target; other failures are not hidden.
 * load owns *data (free with hx_os_memory_free), with no NUL terminator.
 * max_bytes bounds input, including BOM. Failed loads return NULL/0.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_copy(
	const char *from, const char *to, int replace, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_exists(
	const char *path, int *exists, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_load(hx_os_context *context,
	const char *path, size_t max_bytes, void **data, size_t *bytes,
	hx_os_error *error);
/* Output uses its own allocator and is unchanged on decoding/I/O failure. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_load_utf8(hx_os_context *context,
	const char *path, size_t max_bytes, hx_string *out, int strip_bom,
	hx_os_error *error);
/* Strict UTF-8, removes UTF-8 BOM, 16 MiB input limit, output's context. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_read_text(
	const char *path, hx_string *out, hx_os_error *error);
/* Explicit encoding, 16 MiB input limit, transactional UTF-8 output.
 * With BOM enabled, a matching leading BOM is removed and a conflicting BOM
 * is INVALID_ENCODING; BOM=OFF preserves it through the selected converter.
 * Unsupported legacy encodings return NOT_SUPPORTED. No ANSI guessing.
 */
/* Ambiguous UTF-16LE BOM + NUL uses the explicit encoding, not guessing. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_read_text_encoded(const char *path,
	hx_os_encoding encoding, hx_string *out, hx_os_error *error);
/* Publishes an initialized UTF-8 string using the atomic-save contract. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_write_text(
	const char *path, const hx_string *text, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_save(const char *path,
	const void *data, size_t bytes, int replace, hx_os_error *error);

typedef struct hx_os_file_save_result {
	int published;
	int directory_synced;
	hx_os_error durability_error;
} hx_os_file_save_result;
/*
 * Same-directory temporary, complete write, file sync, replace rename.
 * Success means publication, not power-loss durability. ex optionally reports
 * the subsequent parent-directory sync result (unsupported on Windows).
 * Failure before publication removes the temporary where possible; a cleanup
 * failure leaves it for recovery. No implicit cross-filesystem copying.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_save_atomic(
	const char *path, const void *data, size_t bytes, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_save_atomic_ex(const char *path,
	const void *data, size_t bytes, hx_os_file_save_result *result,
	hx_os_error *error);
/*
 * replace uses native replacement on Windows; POSIX backup + replacement is
 * two separate renames. Backup collisions fail before replacing it. There is
 * no rollback: after a later failure inspect the retained backup and source.
 * Caller excludes concurrent mutation of these paths. backup may be NULL.
 * backup() is a copy, not atomic or durable; replace controls its destination.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_replace(const char *target,
	const char *source, const char *backup, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_backup(const char *path,
	const char *backup_path, int replace, hx_os_error *error);

/* UTF-8 results; required includes NUL. No directory is created. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_current(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_current_string(
	hx_string *out, hx_os_error *error);
/* Explicit process-global change. Coordinate with all relative-path users. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_change_current(
	const char *path, hx_os_error *error);
/* Resolves an existing target, including links, to an absolute native path. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_absolute(const char *path,
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_absolute_string(
	const char *path, hx_string *out, hx_os_error *error);
/*
 * Pure lexical views: recognize / and backslash on every platform. Views
 * borrow path or static dot/empty text. Trailing separators are ignored except
 * roots. extension includes the dot; a leading-only dot is not an extension.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_basename(
	const char *path, hx_string_view *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_dirname(
	const char *path, hx_string_view *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_extension(
	const char *path, hx_string_view *out, hx_os_error *error);

typedef struct hx_os_walk_options {
	uint32_t size;
	uint32_t version;
	size_t max_depth;
	size_t max_entries;
	size_t max_results;
	size_t max_bytes;
} hx_os_walk_options;
#define HX_OS_WALK_OPTIONS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_walk_options), 1, 64, 1000000, 65536,             \
			16 * 1024 * 1024                                       \
	}
/*
 * Glob: case-sensitive UTF-8 * and ? per component (? = one codepoint).
 * No [] classes, escaping or ** recursion; leading dots require a literal dot.
 * Returns an owned vector<hx_string>, including an empty vector for no match.
 * Order is unspecified. Container storage and retained text each use max_bytes.
 * Requires both FILE_UTILS and CONTAINERS. NULL options selects the defaults.
 * Removal never descends through links; concurrent external mutation can cause
 * partial removal/failure. Literal filesystem roots, dot and dot-dot are
 * rejected. Limits also cause partial removal, with no rollback. Max depth 256.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_glob(hx_os_context *context,
	const char *pattern, hx_os_vector **matches, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_glob_with_options(
	hx_os_context *context, const char *pattern,
	const hx_os_walk_options *options, hx_os_vector **matches,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_remove_recursive(
	const char *path, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_directory_remove_recursive_with_options(
	const char *path, const hx_os_walk_options *options,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
