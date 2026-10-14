#ifndef HX_C_PLATFORM_SEARCH_H
#define HX_C_PLATFORM_SEARCH_H

#include "hx_os_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* KMP, arbitrary bytes including NUL, first match at/after start. Empty
 * needle matches start. NOT_FOUND preserves *offset. Scratch: size_t per
 * needle byte, default 16 MiB including allocation overhead. O(n+m).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_bytes_find(hx_os_context *context,
	hx_os_bytes_view input, hx_os_bytes_view needle, size_t start,
	size_t max_scratch_bytes, size_t *offset, hx_os_error *error);

typedef struct hx_os_matcher hx_os_matcher;
typedef hx_os_status(HX_OS_CALL *hx_os_match_fn)(
	size_t pattern, size_t offset, size_t bytes, void *user);

/* Immutable Aho-Corasick byte matcher. Up to 4096 nonempty patterns and
 * 1 MiB aggregate pattern bytes, within max_bytes (default 16 MiB).
 * Creation copies the trie and lengths; input patterns are call-borrowed.
 * Reports overlapping/duplicate matches by original pattern index. Different
 * calls can share a matcher. Callback runs synchronously without locks and
 * must not destroy the matcher or throw/longjmp. Scan limits callback count
 * (default 65536), returns LIMIT rather than silently dropping matches.
 * offsets are byte offsets, no Unicode normalization or implicit casefold.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_matcher_create(hx_os_context *context,
	const hx_os_bytes_view *patterns, size_t count, size_t max_bytes,
	hx_os_matcher **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_matcher_scan(
	const hx_os_matcher *matcher, hx_os_bytes_view input,
	size_t max_matches, hx_os_match_fn callback, void *user,
	size_t *completed, hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_matcher_destroy(hx_os_matcher **matcher);

#ifdef __cplusplus
}
#endif
#endif
