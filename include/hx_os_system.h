#ifndef HX_C_PLATFORM_SYSTEM_H
#define HX_C_PLATFORM_SYSTEM_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_string_list hx_os_string_list;

/* Immutable owned UTF-8 snapshot. Borrowed items/NULL-terminated array live
 * until destroy. Empty strings remain distinct entries. Safe shared reads
 * require the owner to remain alive; destruction requires exclusion.
 */
HX_OS_API size_t HX_OS_CALL hx_os_string_list_count(
	const hx_os_string_list *list);

HX_OS_API const char *HX_OS_CALL hx_os_string_list_at(
	const hx_os_string_list *list, size_t index);

HX_OS_API const char *const *HX_OS_CALL hx_os_string_list_data(
	const hx_os_string_list *list);

HX_OS_API void HX_OS_CALL hx_os_string_list_destroy(hx_os_string_list **list);

/*
 * Native argument snapshot, including argv[0], strict UTF-8. Windows reads
 * the Unicode command line rather than CRT narrow argv; Linux reads procfs.
 * Under an interpreter/native bridge, procfs can describe its launcher.
 * Prefer main's argv for the application's POSIX command-line arguments.
 * Bounds: 4 MiB source bytes and 65536 entries. No process locale changes.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_process_arguments(
	hx_os_context *context, hx_os_string_list **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_process_id(uint64_t *identifier);

/* Native host name, not a DNS lookup/FQDN guarantee. required includes NUL.
 * String output uses its context and remains unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_host_name(
	char *buffer, size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_host_name_string(
	hx_string *out, hx_os_error *error);
#define HX_OS_CPU_SSE (UINT64_C(1) << 0)
#define HX_OS_CPU_SSE2 (UINT64_C(1) << 1)
#define HX_OS_CPU_SSE41 (UINT64_C(1) << 2)
#define HX_OS_CPU_AVX (UINT64_C(1) << 3)
#define HX_OS_CPU_AVX2 (UINT64_C(1) << 4)
#define HX_OS_CPU_NEON (UINT64_C(1) << 5)

/* Only advertised usable features; no completeness or permanent-cache
 * guarantee. AVX requires OSXSAVE and enabled XMM/YMM state in XCR0.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_cpu_features_query(
	uint64_t *features, hx_os_error *error);

/* Convenience: zero if unavailable/disabled; never authorizes an instruction.
 */
HX_OS_API uint64_t HX_OS_CALL hx_os_cpu_features(void);

/* OS compatibility/translation report (WOW64/Rosetta), not VM detection.
 * Unknown is NOT_SUPPORTED and preserves output, including generic Linux.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_sys_emulated(
	int *emulated, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
