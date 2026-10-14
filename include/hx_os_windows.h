#ifndef HX_C_PLATFORM_WINDOWS_H
#define HX_C_PLATFORM_WINDOWS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_windows_version_info {
	uint32_t size, version;
	uint16_t file[4], product[4];
	uint32_t flags, type, subtype;
} hx_os_windows_version_info;

/**
 * \brief These queries write a complete v1 structure only when capacity is
 * sufficient; they set size/version, leave any extra caller bytes untouched and
 * preserve all output on failure.
 *
 * \details Native HRESULT/Win32 errors are retained. Inputs are borrowed for
 * the synchronous call. No handle or global runtime is created for the caller.
 * Other platforms and disabled modules return NOT_SUPPORTED.
 */
HX_OS_API int HX_OS_CALL hx_os_windows_version_available(void);

/**
 * \brief UTF-8 filesystem path, resolved relative to the current directory (no
 * DLL search).
 *
 * \details Reads resources without loading/executing the target DLL/EXE.
 * Resource data is limited to 16 MiB. file/product contain major, minor, build,
 * revision; flags are native VS_FF_* masked by dwFileFlagsMask.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_version_query(const char *path,
	hx_os_windows_version_info *out, size_t capacity, hx_os_error *error);

/**
 * \brief For example FileDescription, ProductName, CompanyName or FileVersion.
 *
 * \details field is 1..64 ASCII letters/digits/underscore. The simple entry
 * selects the first resource translation containing this field, in resource
 * order. ex selects a language/code-page pair, or 0/0 for the same default
 * search. At most 256 translations are examined; missing data returns
 * NOT_FOUND. A string output uses its own context and is unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_version_string(const char *path,
	const char *field, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_version_string_ex(
	const char *path, const char *field, uint16_t language,
	uint16_t code_page, hx_string *out, hx_os_error *error);

typedef struct hx_os_windows_rect {
	int32_t left, top, right, bottom;
} hx_os_windows_rect;

enum {
	HX_OS_WINDOWS_WINDOW_VISIBLE = 1u,
	HX_OS_WINDOWS_WINDOW_MINIMIZED = 2u,
	HX_OS_WINDOWS_WINDOW_MAXIMIZED = 4u,
	HX_OS_WINDOWS_MONITOR_PRIMARY = 1u,
	HX_OS_WINDOWS_GPU_SOFTWARE = 1u
};

typedef struct hx_os_windows_window_info {
	uint32_t size, version;
	uintptr_t monitor;
	hx_os_windows_rect bounds;
	uint32_t process_id, thread_id, flags;
} hx_os_windows_window_info;

typedef struct hx_os_windows_monitor_info {
	uint32_t size, version;
	hx_os_windows_rect bounds, work_area;
	uint32_t flags;
	char device_name[128];
} hx_os_windows_monitor_info;

typedef struct hx_os_windows_gpu_info {
	uint32_t size, version;
	uint64_t luid;
	uint64_t dedicated_video_bytes, dedicated_system_bytes;
	uint64_t shared_system_bytes;
	uint32_t vendor_id, device_id, subsystem_id, revision, flags;
	char description[512];
} hx_os_windows_gpu_info;

HX_OS_API int HX_OS_CALL hx_os_windows_desktop_available(void);

/**
 * \brief window/monitor borrow HWND/HMONITOR cast to uintptr_t.
 *
 * \details Do not close them. Caller protects ownership/lifetime; these values
 * can be reused by Windows. Queries are observations, not an atomic snapshot of
 * changing UI state. Bounds use native screen coordinates, right/bottom
 * exclusive, including GetWindowRect DPI virtualization and possible invisible
 * resize borders. No process/thread DPI mode is changed. monitor selects the
 * nearest monitor, or zero when no monitor is available. VISIBLE does not imply
 * unobscured.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_window_query(uintptr_t window,
	hx_os_windows_window_info *out, size_t capacity, hx_os_error *error);

/**
 * \brief Own-process windows must be queried on their owning thread; otherwise
 * WOULD_BLOCK is returned without sending a message.
 *
 * \details The owning-thread call can invoke the window procedure and has no
 * hard deadline. For other processes this reads the caption, not arbitrary
 * control text. Empty titles succeed. The limit is 32767 UTF-16 units; possible
 * truncation is LIMIT.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_window_title(
	uintptr_t window, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_window_class(
	uintptr_t window, hx_string *out, hx_os_error *error);

/**
 * \brief Full executable path of the window's owning process; normal access
 * checks apply, with no privilege adjustment.
 *
 * \details Limited to 32767 UTF-16 units.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_window_process_path(
	uintptr_t window, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_monitor_query(uintptr_t monitor,
	hx_os_windows_monitor_info *out, size_t capacity, hx_os_error *error);

/**
 * \brief DXGI 1.1, supported by Windows 7.
 *
 * \details No rendering device or COM apartment is created. Do not call from
 * DllMain. Indices enumerate the current factory, 0..255, NOT_FOUND ends
 * enumeration. A new call can observe hot-plug changes. LUID identifies an
 * adapter within the current OS session, not across boots. Memory fields are
 * native capacities, not free memory. Software adapters are included. The
 * monitor query identifies the output adapter, which need not be the GPU used
 * to render an application's content on hybrid systems. Each call releases all
 * DXGI references before returning. Native work has no hard deadline. At most
 * 256 adapters/256 outputs per adapter are searched.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_gpu_query(uint32_t index,
	hx_os_windows_gpu_info *out, size_t capacity, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_windows_monitor_gpu_query(
	uintptr_t monitor, hx_os_windows_gpu_info *out, size_t capacity,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
