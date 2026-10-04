#ifndef HX_C_PLATFORM_VM_H
#define HX_C_PLATFORM_VM_H

#include "hx_os_file.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_pages hx_os_pages;
typedef struct hx_os_file_map hx_os_file_map;

enum {
	HX_OS_PAGE_NONE = 0,
	HX_OS_PAGE_READ = 1u,
	HX_OS_PAGE_WRITE = 2u,
	HX_OS_PAGE_EXECUTE = 4u
};

enum { HX_OS_MAP_READ, HX_OS_MAP_SHARED_WRITE, HX_OS_MAP_PRIVATE_COPY };

/*
 * Positive bytes round up to native page size; accessible pages start zeroed.
 * Metadata uses context, native memory is outside its allocation accounting.
 * WRITE/EXECUTE require READ in the portable interface. Native policy can
 * reject executable memory; no implicit fallback weakens the request.
 * Permissions are OS requests, not a sandbox/security-isolation guarantee.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pages_alloc(hx_os_context *context,
	size_t bytes, unsigned protection, hx_os_pages **out,
	hx_os_error *error);

HX_OS_API void *HX_OS_CALL hx_os_pages_data(hx_os_pages *pages);

HX_OS_API size_t HX_OS_CALL hx_os_pages_size(const hx_os_pages *pages);

/* Whole allocation. Caller excludes access during protect/free. Failure
 * may leave native partial changes: restore known permissions before use.
 * Enabling EXECUTE includes instruction-cache synchronization for this range.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pages_protect(
	hx_os_pages *pages, unsigned protection, hx_os_error *error);

/* Free retains the object if native unmapping fails; NULL owner is OK. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pages_free(
	hx_os_pages **pages, hx_os_error *error);

/*
 * Maps a regular file independently of its cursor/lifetime. bytes=0 means
 * remaining file content; an empty range returns EOF with *out=NULL.
 * Unaligned offsets are handled internally. No extension beyond EOF.
 * All modes require READ; SHARED_WRITE also requires WRITE, without APPEND.
 * PRIVATE_COPY changes never write back. READ has no writable view.
 * Creation borrows file only through return; the map owns native resources.
 * External truncation/storage faults can fault on access (e.g. SIGBUS);
 * coordinate file size, updates and view access outside this API.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_map_create(hx_os_context *context,
	hx_os_file *file, uint64_t offset, uint64_t bytes, int mode,
	hx_os_file_map **out, hx_os_error *error);

/* One-call whole-file mapping with default context, UTF-8 path, no creation. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_map_open(
	const char *path, int mode, hx_os_file_map **out, hx_os_error *error);

/* Borrowed views are invalid after destruction. NULL handle yields NULL/0. */
HX_OS_API const void *HX_OS_CALL hx_os_file_map_view(const hx_os_file_map *map);

HX_OS_API void *HX_OS_CALL hx_os_file_map_write_view(hx_os_file_map *map);

HX_OS_API size_t HX_OS_CALL hx_os_file_map_size(const hx_os_file_map *map);

/* SHARED_WRITE only. Waits for writeback; durable=1 additionally requests
 * native file sync. No hardware durability or hard deadline promise.
 * Failure retains map and data for retry; destroy does not implicitly sync.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_map_flush(
	hx_os_file_map *map, int durable, hx_os_error *error);

/* Native unmap failure retains the object. After successful unmap consumes
 * it, even when reporting a descriptor-close error. Exclude all view users.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_file_map_destroy(
	hx_os_file_map **map, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
