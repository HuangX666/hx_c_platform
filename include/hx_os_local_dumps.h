#ifndef HX_C_PLATFORM_LOCAL_DUMPS_H
#define HX_C_PLATFORM_LOCAL_DUMPS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { HX_OS_LOCAL_DUMPS_MINI = 1, HX_OS_LOCAL_DUMPS_FULL = 2 };

enum {
	HX_OS_LOCAL_DUMPS_VIEW_DEFAULT = 0,
	HX_OS_LOCAL_DUMPS_VIEW32 = 1,
	HX_OS_LOCAL_DUMPS_VIEW64 = 2
};

typedef struct hx_os_local_dumps_options {
	uint32_t size, version;
	uint32_t dump_type, dump_count;
	uint32_t registry_view;
	const char *dump_folder;
} hx_os_local_dumps_options;

enum {
	HX_OS_LOCAL_DUMPS_HAS_TYPE = 1u,
	HX_OS_LOCAL_DUMPS_HAS_COUNT = 2u,
	HX_OS_LOCAL_DUMPS_HAS_FOLDER = 4u
};

typedef struct hx_os_local_dumps_info {
	uint32_t size, version;
	uint32_t fields;
	uint32_t dump_type, dump_count;
	uint32_t managed;
} hx_os_local_dumps_info;

/**
 * \brief Compiled Windows adapter, not a permission or dump-delivery check.
 * \returns Nonzero on Windows with LOCAL_DUMPS enabled, zero otherwise.
 */
HX_OS_API int HX_OS_CALL hx_os_local_dumps_available(void);

/**
 * \brief Initialize the known prefix; preserve extra caller bytes.
 * \param[out] options Receives version 1 defaults.
 * \param[in] capacity Writable bytes at options; at least sizeof(*options).
 * \returns OK, or INVALID without writing insufficient/NULL storage.
 * \details Defaults: full dumps, count 3, calling-process registry view,
 * dump_folder=NULL. NULL selects literal %LOCALAPPDATA%\CrashDumps, expanded
 * by Windows for the crashing account, not by this call. A custom folder is
 * strict UTF-8: an absolute Windows path or a leading %VARIABLE% reference
 * that expands to one. No directory, ACL or environment variable is created.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_dumps_options_init(
	hx_os_local_dumps_options *options, size_t capacity);

/**
 * \brief Explicitly configure persistent, per-executable WER LocalDumps.
 * \param[in] application UTF-8 EXE basename (not a path/DLL); NULL uses the
 * current host EXE, even when called through a DLL. Empty is invalid.
 * \param[in] options Borrowed for this call; NULL selects defaults.
 * \param[out] error Optional native error details.
 * \returns OK when installed or already identical and library-managed;
 * EXISTS for any other existing application key, ACCESS_DENIED on insufficient
 * rights, NOT_SUPPORTED off Windows/module-off, or another explicit error.
 * \details Writes only HKLM\Software\Microsoft\Windows\Windows Error
 * Reporting\LocalDumps\<application> in the selected registry view. All EXEs
 * with that basename in that view share the setting. dump_count must be >0;
 * only MINI/FULL are accepted. No automatic elevation, handler, worker, dump
 * upload, global WER value change or forced termination. Usually called by an
 * elevated installer, not at every ordinary application startup.
 * \warning Settings survive return/process exit. Exclude other writers to
 * this application key across processes. Registry writes are not a transaction:
 * failed creation attempts remove only the newly created leaf; cleanup failure
 * is error.cause_code (Win32) and can leave a partial key for manual
 * inspection. Process interruption can also leave partial settings. The caller
 * supplies directory write permissions and verifies actual dump collection.
 * This API does not diagnose hangs, detect memory errors or guarantee every
 * crash dump. Do not call from DllMain or an exception/signal handler.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_dumps_enable(
	const char *application, const hx_os_local_dumps_options *options,
	hx_os_error *error);

/**
 * \brief Read explicit per-application settings without changing them.
 * \param[in] application Same basename/NULL convention as enable.
 * \param[in] registry_view DEFAULT, VIEW32 or VIEW64.
 * \param[out] out Receives a complete v1 record; extra bytes stay unchanged.
 * \param[in] capacity Writable bytes at out.
 * \param[in,out] folder Initialized string receiving literal, unexpanded
 * DumpFolder; empty when absent. Uses its own allocation context.
 * \param[out] error Optional error details.
 * \returns OK, NOT_FOUND when the application key is absent, OUTPUT_FULL for
 * insufficient record capacity, or another error. Both outputs stay unchanged
 * on failure. Off Windows/module-off returns NOT_SUPPORTED.
 * \details fields identifies present values; missing numeric values are zero.
 * managed means a complete library-marked layout with no extra values/subkeys,
 * not proof that nobody has edited its values. Other keys are read-only here;
 * malformed value types/encoding fail explicitly. Global WER settings and OS
 * defaults are NOT merged: NOT_FOUND does not mean collection is disabled.
 * Observations are not atomic against external registry edits. A successful
 * query does not prove permissions, OS policy or actual dump delivery.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_dumps_query(
	const char *application, unsigned registry_view,
	hx_os_local_dumps_info *out, size_t capacity, hx_string *folder,
	hx_os_error *error);

/**
 * \brief Remove only a complete library-managed configuration that still
 * exactly matches expected (NULL selects the same defaults as enable).
 * \returns OK when removed/already absent; BUSY for foreign, edited, partial
 * or extended keys, leaving them intact. Native access failures are retained.
 * \details Same writer exclusion and application/view rules as enable. Never
 * recursively deletes a key, touches parent/global values, or deletes dump
 * files. Removing an override can expose global WER settings; this is NOT a
 * guarantee that all dump collection is disabled. Pass the original options,
 * including the literal folder, to avoid removing a changed configuration.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_dumps_remove(
	const char *application, const hx_os_local_dumps_options *expected,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
