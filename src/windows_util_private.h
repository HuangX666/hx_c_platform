#ifndef HX_C_PLATFORM_WINDOWS_UTIL_PRIVATE_H
#define HX_C_PLATFORM_WINDOWS_UTIL_PRIVATE_H

#include "internal.h"
#include "hx_os_windows.h"

#if defined(_WIN32)
#include <windows.h>

hx_os_status hx_os_i_windows_version_query(
	const char *path, hx_os_windows_version_info *out, hx_os_error *error);

hx_os_status hx_os_i_windows_version_string(const char *path, const char *field,
	uint16_t language, uint16_t code_page, hx_string *out,
	hx_os_error *error);

hx_os_status hx_os_i_windows_window_query(
	uintptr_t window, hx_os_windows_window_info *out, hx_os_error *error);

hx_os_status hx_os_i_windows_window_text(
	uintptr_t window, int kind, hx_string *out, hx_os_error *error);

hx_os_status hx_os_i_windows_monitor_query(
	uintptr_t monitor, hx_os_windows_monitor_info *out, hx_os_error *error);

hx_os_status hx_os_i_windows_gpu_query(uint32_t index, uintptr_t monitor,
	hx_os_windows_gpu_info *out, hx_os_error *error);
#endif
#endif
