#ifndef HX_C_PLATFORM_TEXT_LOOKUP_H
#define HX_C_PLATFORM_TEXT_LOOKUP_H

#include "hx_string.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_text_lookup hx_os_text_lookup;
typedef struct hx_os_text_lookup_options {
	uint32_t size;
	uint32_t version;
	size_t max_entries;
	size_t max_text_bytes;
} hx_os_text_lookup_options;
#define HX_OS_TEXT_LOOKUP_OPTIONS_INIT                                         \
	{                                                                      \
		sizeof(hx_os_text_lookup_options), 1, 4096, 1024 * 1024        \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_options_init(
	hx_os_text_lookup_options *options, size_t capacity);

/**
 * \brief Independent mutable table.
 *
 * \details Default limits: 4096 entries, 1 MiB combined key/value bytes
 * including NULs. Allocates on demand; metadata is additional. Concurrent reads
 * allowed; host excludes mutation/destruction. No locks, background workers,
 * global locale or process-wide catalog.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_create(
	hx_os_context *context, hx_os_text_lookup **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_create_with_options(
	hx_os_context *context, const hx_os_text_lookup_options *options,
	hx_os_text_lookup **out, hx_os_error *error);

/**
 * \brief Copies strict UTF-8 key and translation.
 *
 * \details Keys are exact, case-sensitive UTF-8 bytes (no normalization).
 * Replaces an existing key transactionally. Empty strings are valid.
 * Replacement may temporarily retain old+new text, at most twice
 * max_text_bytes. Each input is bounded by max_text_bytes.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_add(
	hx_os_text_lookup *lookup, const char *fallback, const char *translated,
	hx_os_error *error);

/**
 * \brief A missing key returns fallback itself, with OK.
 *
 * \details Only absence falls back; invalid UTF-8 and output
 * allocation/capacity errors are reported. required includes NUL; insufficient
 * capacity preserves buffer. Inputs and buffer must not overlap. get_string
 * uses out's own context and preserves out on failure. Returned data is owned
 * by the caller.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_get(
	const hx_os_text_lookup *lookup, const char *fallback, char *buffer,
	size_t capacity, size_t *required, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_get_string(
	const hx_os_text_lookup *lookup, const char *fallback, hx_string *out,
	hx_os_error *error);

/**
 * \brief Allocation-free view.
 *
 * \details A hit borrows the table until any mutation/destroy; a missing key
 * borrows fallback itself. Exclude mutation during use. Failure preserves out;
 * the caller keeps fallback alive when it is used.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_get_view(
	const hx_os_text_lookup *lookup, const char *fallback,
	hx_string_view *out, hx_os_error *error);

/**
 * \brief OBS-style flat language catalog: UTF-8/BOM, key=value, # comments,
 * quoted strings with JSON escapes, or trimmed unquoted text.
 *
 * \details No sections, includes, interpolation or process locale changes.
 * Duplicate keys use last. Overlay is transactional; syntax/I/O/limit failures
 * preserve every entry. Uses LEXER; file entries also need FILE_UTILS. Input
 * limit equals the table text budget. Old and candidate tables may coexist
 * during the call.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_load(hx_os_context *context,
	const char *path, hx_os_text_lookup **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_add_file(
	hx_os_text_lookup *lookup, const char *path, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_text_lookup_add_text(
	hx_os_text_lookup *lookup, hx_string_view text, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_text_lookup_destroy(hx_os_text_lookup **lookup);
#ifdef __cplusplus
}
#endif
#endif
