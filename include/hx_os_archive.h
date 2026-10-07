#ifndef HX_C_PLATFORM_ARCHIVE_H
#define HX_C_PLATFORM_ARCHIVE_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_OS_ARCHIVE_AUTO,
	HX_OS_ARCHIVE_ZIP,
	HX_OS_ARCHIVE_TAR,
	HX_OS_ARCHIVE_TAR_GZIP,
	HX_OS_ARCHIVE_TAR_XZ,
	HX_OS_ARCHIVE_TAR_ZSTD,
	HX_OS_ARCHIVE_7ZIP,
	HX_OS_ARCHIVE_GZIP,
	HX_OS_ARCHIVE_XZ,
	HX_OS_ARCHIVE_ZSTD
};

enum { HX_OS_ARCHIVE_BALANCED, HX_OS_ARCHIVE_FAST, HX_OS_ARCHIVE_SMALL };

enum {
	HX_OS_ARCHIVE_OVERWRITE = 1,
	HX_OS_ARCHIVE_SYNC_FILES = 2,
	HX_OS_ARCHIVE_ENCRYPT_NAMES = 4
};

enum {
	HX_OS_ARCHIVE_FILE,
	HX_OS_ARCHIVE_DIRECTORY,
	HX_OS_ARCHIVE_LINK,
	HX_OS_ARCHIVE_OTHER
};

enum {
	HX_OS_ARCHIVE_SIZE_KNOWN = 1,
	HX_OS_ARCHIVE_ENCRYPTED = 2,
	HX_OS_ARCHIVE_MTIME_KNOWN = 4
};

typedef struct hx_os_archive_entry {
	hx_string_view path;
	uint64_t bytes;
	int64_t modified_seconds;
	uint32_t modified_nanoseconds;
	uint32_t kind;
	uint32_t flags;
} hx_os_archive_entry;

typedef struct hx_os_archive_result {
	uint32_t size, version;
	uint64_t entries;
	uint64_t files;
	uint64_t directories;
	uint64_t content_bytes;
	uint64_t archive_bytes;
	uint32_t published;
} hx_os_archive_result;

/* Called within the operation without library locks. Default libarchive
 * callbacks use the calling thread; the optional 7-Zip backend may report
 * progress from a native codec thread. Progress calls never overlap and
 * intermediate notifications may coalesce. Paths/entry borrow storage
 * for this callback only. Return OK to continue, CANCELLED (or another
 * failure) to stop; fill error for extra diagnostics. No callback may retain
 * internal pointers, unwind an exception or longjmp through this library.
 */
typedef hx_os_status(HX_OS_CALL *hx_os_archive_visit_fn)(
	const hx_os_archive_entry *entry, void *user, hx_os_error *error);
typedef hx_os_status(HX_OS_CALL *hx_os_archive_progress_fn)(
	const hx_os_archive_result *progress, void *user, hx_os_error *error);

typedef struct hx_os_archive_options {
	uint32_t size, version;
	uint64_t max_archive_bytes;
	uint64_t max_content_bytes;
	uint64_t max_decoded_bytes;
	hx_os_deadline deadline;
	size_t max_entries;
	size_t max_depth;
	size_t max_path_bytes;
	size_t max_memory_bytes;
	const char *password;
	const char *seven_zip_library;
	const char *seven_zip_adapter;
	hx_string *diagnostic;
	hx_os_archive_progress_fn progress;
	void *user;
	uint32_t format;
	uint32_t preset;
	uint32_t flags;
	uint32_t workers;
} hx_os_archive_options;

#define HX_OS_ARCHIVE_OPTIONS_INIT                                             \
	{                                                                      \
		sizeof(hx_os_archive_options), 1, UINT64_C(4294967296),        \
			UINT64_C(4294967296), UINT64_C(8589934592),            \
			HX_OS_DEADLINE_INFINITE, 65536, 64, 65536,             \
			16 * 1024 * 1024, NULL, NULL, NULL, NULL, NULL, NULL,  \
			HX_OS_ARCHIVE_AUTO, HX_OS_ARCHIVE_BALANCED, 0, 1       \
	}

/* Compiled adapters; 7zip_available does not probe a runtime DLL/so. */
HX_OS_API int HX_OS_CALL hx_os_archive_available(void);
HX_OS_API int HX_OS_CALL hx_os_archive_7zip_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_options_init(
	hx_os_archive_options *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_result_init(
	hx_os_archive_result *result, size_t capacity);

/* Synchronous, bounded conveniences. NULL options selects the defaults.
 * A directory packs its contents relative to that directory; one input file
 * uses its basename. AUTO infers the output format from its extension.
 * Raw .gz/.xz/.zst streams hold one file; use .tar.gz/.tar.xz/.tar.zst for
 * directories. ZIP passwords use AES-256. Password/header-encrypted 7z needs
 * the separate optional adapter and an explicit absolute 7-Zip DLL/so path.
 * The adapter defaults to a sibling of this library/executable; its absolute
 * path can also be given explicitly. No PATH search or helper executable.
 *
 * All paths/passwords are UTF-8. Options/strings/callback user are borrowed
 * until return; exclude mutation of inputs and outputs during an operation.
 * Files must stay stable: these calls do not create a filesystem snapshot.
 * Ordinary calls need no global initialization and start no owned workers.
 * workers limits codec parallelism (default one); a codec can also use a
 * fixed number of helper threads. No operation outlives its synchronous call.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_pack(
	const char *source, const char *archive_path, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_unpack(
	const char *archive_path, const char *destination, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_extract_file(
	const char *archive_path, const char *entry_path,
	const char *destination_file, hx_os_error *error);

/* Pack uses a temporary output and atomic publication. Existing targets
 * remain intact on failure before publication; result.published records a
 * completed publication. Pack syncs the file; parent-directory sync is best
 * effort, like file_save_atomic. SYNC_FILES requests file sync on extraction
 * and parent sync on POSIX; a later failure retains published > 0.
 * Packing output inside the source directory is rejected.
 *
 * Extraction defaults to no overwrite, no link/device restoration and no
 * owner/ACL/permission restoration. Regular files publish individually after
 * their decoder checks; previously published files/new directories remain
 * on a later error. There is no whole-directory transaction or automatic
 * rollback. Current-file temporaries are removed on failure where possible.
 * Traversal is anchored under the selected destination and does not follow
 * existing links/reparse points beneath it. Callers exclude concurrent tree
 * mutation, including changing Windows directory reparse metadata. Invalid
 * or escaping path components
 * fail explicitly. OVERWRITE replaces regular files only, never follows a
 * link. Multiple archive records targeting one name cannot be represented
 * separately; without OVERWRITE the second publication returns EXISTS.
 * Encrypted input requires private plaintext staging; filesystems incapable
 * of enforcing that protection can report NOT_SUPPORTED.
 *
 * max_memory_bytes bounds first-party working/metadata storage, not vendor
 * decoder allocations. Input dictionaries/solid blocks can require additional
 * native memory. max_decoded_bytes limits the outer Gzip/XZ/Zstd stream,
 * including container headers, padding and skipped contents (default 8 GiB).
 * Output limits are not hard CPU/native-memory limits. Listing/skipping an
 * entry does not certify its contents. CRC is not cryptographic authenticity.
 * Deadlines/progress cancellation are checked between native calls/chunks;
 * codec or filesystem work is not forcibly interruptible. Cleanup completes
 * before return; no hidden background owner is abandoned on timeout.
 *
 * Optional diagnostic is cleared at entry and filled with copied UTF-8
 * detail on failure when allocation permits. Its own context owns its data.
 * The error record remains valid independently of this string. Optional
 * result must be initialized with result_init; partial progress is retained.
 * entries/files/directories count inspected records; content_bytes counts
 * bytes processed for packing or extraction (zero for metadata-only listing).
 * archive_bytes is the largest input/output byte offset used; published counts
 * destination files successfully published (one for a completed pack).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_pack_ex(hx_os_context *context,
	const char *source, const char *archive_path,
	const hx_os_archive_options *options, hx_os_archive_result *result,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_unpack_ex(
	hx_os_context *context, const char *archive_path,
	const char *destination, const hx_os_archive_options *options,
	hx_os_archive_result *result, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_extract_file_ex(
	hx_os_context *context, const char *archive_path,
	const char *entry_path, const char *destination_file,
	const hx_os_archive_options *options, hx_os_archive_result *result,
	hx_os_error *error);

/* Metadata listing does not certify payload CRC/authentication. Native
 * metadata-only/skip operations may still decode solid/compressed streams.
 * A missing size is indicated by flags, never fabricated from another metric.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_visit(const char *archive_path,
	hx_os_archive_visit_fn visit, void *user, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_archive_visit_ex(hx_os_context *context,
	const char *archive_path, const hx_os_archive_options *options,
	hx_os_archive_visit_fn visit, void *user, hx_os_archive_result *result,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
