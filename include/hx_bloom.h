#ifndef HX_C_PLATFORM_BLOOM_H
#define HX_C_PLATFORM_BLOOM_H

#include "hx_os_params.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_bloom hx_os_bloom;
/* expected_items>0, false_positive_rate in [1e-9,0.5]. The bit array is a
 * power of two, at most max_bytes (default 16 MiB). No allocation on insert
 * or query. Not cryptographic, no per-item deletion, externally serialized.
 * Query false means definitely absent; true means maybe present. Inserting
 * more than expected_items increases false positives. Clearing resets all.
 * Inputs are arbitrary borrowed bytes (NULL only for size=0).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bloom_create(hx_os_context *context,
	size_t expected_items, double false_positive_rate, size_t max_bytes,
	hx_os_bloom **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_bloom_insert(
	hx_os_bloom *bloom, hx_os_bytes_view value, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_bloom_contains(const hx_os_bloom *bloom,
	hx_os_bytes_view value, int *maybe, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_bloom_clear(hx_os_bloom *bloom);
HX_OS_API size_t HX_OS_CALL hx_os_bloom_size_bytes(const hx_os_bloom *bloom);
HX_OS_API void HX_OS_CALL hx_os_bloom_destroy(hx_os_bloom **bloom);

#ifdef __cplusplus
}
#endif
#endif
