#ifndef HX_C_PLATFORM_FILE_EXTRA_H
#define HX_C_PLATFORM_FILE_EXTRA_H

#include "hx_os_file.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Positive byte range [offset, offset+bytes), with signed-64-bit end.
 * shared=1 needs READ; exclusive needs WRITE. NOW attempts once, INFINITE
 * retries indefinitely. Conflicts use monotonic waits in at most 10 ms
 * requested increments; native calls and scheduling have no hard deadline.
 * Timeout leaves the file valid and acquires no new lock. Retry is allowed.
 * Match unlock to a successfully locked range, exclude concurrent close.
 *
 * POSIX record locks are process-owned/advisory: closing ANY descriptor
 * for the inode (including a map's duplicate) releases all process locks.
 * Windows locks are handle-owned and restrict ordinary conflicting I/O.
 * Neither provides thread mutex semantics or protects mapped-file access.
 * Windows positioned I/O uses another handle: use cursor I/O through the
 * locking handle while a region is held, or the I/O can conflict with it.
 * Do not overlap/upgrade/reenter ranges: native behavior differs.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_lock_at(hx_os_file *file,
	uint64_t offset, uint64_t bytes, int shared, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_unlock_at(
	hx_os_file *file, uint64_t offset, uint64_t bytes, hx_os_error *error);

enum { HX_OS_TIME_ACCESSED = 1u, HX_OS_TIME_MODIFIED = 2u };
enum { HX_OS_PERMISSION_OWNER_EXECUTE = 4u };
typedef struct hx_os_file_times {
	uint32_t size;
	uint32_t version;
	int64_t accessed_seconds;
	int64_t modified_seconds;
	uint32_t accessed_nanoseconds;
	uint32_t modified_nanoseconds;
	uint32_t fields;
	uint32_t reserved;
} hx_os_file_times;
#define HX_OS_FILE_TIMES_INIT                                                  \
	{                                                                      \
		sizeof(hx_os_file_times), 1, 0, 0, 0, 0, 0, 0                  \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_times_init(
	hx_os_file_times *times, size_t capacity);
/*
 * Unix seconds plus [0,1e9) ns. fields selects access/modify; omitted fields
 * remain unchanged, fields=0 is a no-op without an existence/access check.
 * Windows truncates to 100 ns; filesystem precision can be coarser.
 * Does not set creation or metadata-change time. follow_links is 0 or 1.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_set_times(const char *path,
	const hx_os_file_times *times, int follow_links, hx_os_error *error);
/*
 * POSIX: replace owner read/write/execute mode, preserve group/other mode.
 * Windows: READ is required, EXECUTE unsupported; WRITE toggles the
 * read-only attribute only. This never edits ACLs or guarantees access.
 * The v1 extended-stat permission summary remains read/write only.
 * Exclude concurrent path replacement or permission changes. Nofollow
 * updates may be unsupported on a target/filesystem, never silently follow.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_path_set_permissions(const char *path,
	unsigned permissions, int follow_links, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
