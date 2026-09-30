#ifndef HX_C_PLATFORM_FILE_H
#define HX_C_PLATFORM_FILE_H

#include "hx_os_base.h"

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

/*
 * Paths are borrowed, NUL-terminated UTF-8. Native names not representable
 * as Unicode are rejected. No normalization or process-locale changes.
 * POSIX creation uses 0600 (files), 0700 (directories), subject to umask.
 * Open supports regular files. Pipes and devices need separate adapters.
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
HX_OS_API void HX_OS_CALL hx_os_directory_close(hx_os_directory *directory);

#ifdef __cplusplus
}
#endif
#endif
