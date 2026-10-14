#ifndef HX_C_PLATFORM_SYSINFO_H
#define HX_C_PLATFORM_SYSINFO_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_cpu_usage hx_os_cpu_usage;
typedef struct hx_os_sys_disk_space_info {
	uint32_t size;
	uint32_t version;
	uint64_t total_bytes;
	uint64_t available_bytes;
	uint64_t free_bytes;
} hx_os_sys_disk_space_info;

typedef struct hx_os_sys_proc_memory_info {
	uint32_t size;
	uint32_t version;
	uint64_t resident_bytes;
	uint64_t virtual_bytes;
} hx_os_sys_proc_memory_info;

#define HX_OS_SYS_DISK_SPACE_INFO_INIT                                         \
	{                                                                      \
		sizeof(hx_os_sys_disk_space_info), 1, 0, 0, 0                  \
	}
#define HX_OS_SYS_PROC_MEMORY_INFO_INIT                                        \
	{                                                                      \
		sizeof(hx_os_sys_proc_memory_info), 1, 0, 0                    \
	}

/**
 * \brief Snapshots may change immediately.
 *
 * \details Byte counts are uint64_t on all targets. Outputs are unchanged on
 * failure. No call changes process configuration. CPU counts describe online
 * system CPUs, not affinity or container quotas. Physical topology missing from
 * the OS returns NOT_SUPPORTED.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_total_memory(
	uint64_t *bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_available_memory(
	uint64_t *bytes, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_physical_cores(
	int *count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_logical_cores(
	int *count, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_page_size(
	size_t *bytes, hx_os_error *error);

/**
 * \brief Initialize with the matching INIT macro.
 *
 * \details Paths are borrowed UTF-8.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_disk_space(
	const char *path, hx_os_sys_disk_space_info *info, hx_os_error *error);

/**
 * \brief virtual_bytes is committed + reserved address space, not commit
 * charge.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_proc_memory(
	hx_os_sys_proc_memory_info *info, hx_os_error *error);

/**
 * \brief required includes NUL; OUTPUT_FULL permits retry with a larger buffer.
 *
 * \details Linux/Android report the kernel release, not distribution/Android
 * API.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_version(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_version_string(
	hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_version_numbers(
	int *major, int *minor, int *patch, hx_os_error *error);

/**
 * \brief Owns a context allocation.
 *
 * \details Query/destroy require caller serialization. Samples current-process
 * user + kernel CPU, excluding child processes. Percent = CPU delta / monotonic
 * elapsed / creation-time logical CPUs. Each successful query advances the
 * sample. Zero elapsed returns WOULD_BLOCK and keeps it. No global sampler,
 * worker, or periodic timer. destroy accepts NULL, consumes the handle and does
 * not block.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cpu_usage_create(
	hx_os_context *context, hx_os_cpu_usage **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_cpu_usage_query(
	hx_os_cpu_usage *usage, double *percent, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_cpu_usage_destroy(hx_os_cpu_usage **usage);

#ifdef __cplusplus
}
#endif
#endif
