#ifndef HX_C_PLATFORM_PARAMS_H
#define HX_C_PLATFORM_PARAMS_H

#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_params hx_os_params;

typedef struct hx_os_bytes_view {
	const void *data;
	size_t size;
} hx_os_bytes_view;

enum {
	HX_OS_PARAM_BOOL = 1,
	HX_OS_PARAM_I64,
	HX_OS_PARAM_DOUBLE,
	HX_OS_PARAM_STRING,
	HX_OS_PARAM_POINTER,
	HX_OS_PARAM_BYTES,
	HX_OS_PARAM_U64
};

/**
 * \brief Borrowed value descriptor, never an owner.
 *
 * \details Select the corresponding union member; bool uses integer 0/1. String
 * lengths exclude the trailing NUL; byte spans have no encoding restriction.
 * Pointers remain borrowed.
 */
typedef struct hx_os_param_value {
	union {
		int64_t integer;
		uint64_t unsigned_integer;
		double real;
		void *pointer;
		hx_string_view string;
		hx_os_bytes_view bytes;
	} data;

	uint32_t kind;
} hx_os_param_value;

typedef struct hx_os_params_options {
	uint32_t size, version;
	size_t max_entries, max_bytes;
} hx_os_params_options;

#define HX_OS_PARAMS_OPTIONS_INIT                                              \
	{                                                                      \
		sizeof(hx_os_params_options), 1, 256, 1024 * 1024              \
	}

HX_OS_API int HX_OS_CALL hx_os_params_available(void);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_options_init(
	hx_os_params_options *options, size_t capacity);

/**
 * \brief Independent optional module; no global init, workers or
 * container/variant dependency.
 *
 * \details Default context is NULL. Defaults: 256 entries, 1 MiB retained
 * name/value storage; limits are 65535 entries and 64 MiB. Names are strict
 * case-sensitive UTF-8, 1..127 bytes. Strings allow embedded NUL; getters
 * expose a trailing NUL but retain the explicit length.
 *
 * Storage grows on demand. max_bytes bounds retained name/value capacities,
 * including terminators; the entry array and allocation headers are separate
 * and bounded by max_entries. Transactional replacement may temporarily hold
 * both allocations (up to twice these bounds). Smaller replacements reuse
 * capacity; remove/clear release it. Set failure preserves existing values.
 *
 * Serialize mutation/close against all access, including allocator callbacks.
 * Detected allocator reentry is DEADLOCK; this is not internal thread safety.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_params_create(
	hx_os_context *context, hx_os_params **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_create_with_options(
	hx_os_context *context, const hx_os_params_options *options,
	hx_os_params **out, hx_os_error *error);

/**
 * \brief Deep-copy names, strings and bytes; raw pointers remain borrowed.
 *
 * \details Uses the explicitly selected destination context (NULL = default),
 * and source limits. Clone requires exclusive source access for its allocator
 * reentry guard. Values are unchanged. Creation/clone failure sets *out to
 * NULL.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_params_clone(const hx_os_params *source,
	hx_os_context *context, hx_os_params **out, hx_os_error *error);

/**
 * \brief A synchronous signal/procedure borrow keeps the owner alive.
 *
 * \details Destruction during that borrow returns BUSY; keep the owner for
 * destruction afterward.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_params_destroy(
	hx_os_params **params, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_clear(
	hx_os_params *params, hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_params_count(const hx_os_params *params);

/**
 * \brief Retained name/value capacity; excludes entries and allocator metadata.
 *
 * \details Null or reentrant count/storage queries return zero.
 */
HX_OS_API size_t HX_OS_CALL hx_os_params_storage_bytes(
	const hx_os_params *params);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_remove(
	hx_os_params *params, const char *name, hx_os_error *error);

/**
 * \brief set copies input before invalidating any aliased borrowed bytes.
 *
 * \details Get/at preserve outputs on failure; returned names/values borrow
 * until mutation or destruction. at enumerates names in UTF-8 byte order, index
 * starts at zero. Missing key is NOT_FOUND, wrong typed getter is
 * TYPE_MISMATCH. No implicit numeric, string or encoding conversions. Nonfinite
 * doubles are preserved.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set(hx_os_params *params,
	const char *name, hx_os_param_value value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get(const hx_os_params *params,
	const char *name, hx_os_param_value *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_at(const hx_os_params *params,
	size_t index, hx_string_view *name, hx_os_param_value *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_bool(
	hx_os_params *params, const char *name, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_bool(
	const hx_os_params *params, const char *name, int *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_i64(hx_os_params *params,
	const char *name, int64_t value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_i64(
	const hx_os_params *params, const char *name, int64_t *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_u64(hx_os_params *params,
	const char *name, uint64_t value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_u64(
	const hx_os_params *params, const char *name, uint64_t *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_double(hx_os_params *params,
	const char *name, double value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_double(
	const hx_os_params *params, const char *name, double *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_pointer(hx_os_params *params,
	const char *name, void *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_pointer(
	const hx_os_params *params, const char *name, void **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_string(hx_os_params *params,
	const char *name, hx_string_view value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_string(
	const hx_os_params *params, const char *name, hx_string_view *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_bytes(hx_os_params *params,
	const char *name, const void *data, size_t size, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_get_bytes(
	const hx_os_params *params, const char *name, hx_os_bytes_view *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_set_cstr(hx_os_params *params,
	const char *name, const char *value, hx_os_error *error);

/**
 * \brief Immediate edit cursor; the first failure skips later setters.
 *
 * \details Earlier successful edits remain. It borrows the pack through all
 * setter calls.
 */
typedef struct hx_os_params_edit {
	hx_os_error error;
	hx_os_params *params;
} hx_os_params_edit;

HX_OS_API hx_os_params_edit HX_OS_CALL hx_os_params_edit_begin(
	hx_os_params *params);

HX_OS_API hx_os_status HX_OS_CALL hx_os_params_edit_finish(
	const hx_os_params_edit *edit, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_bool(
	hx_os_params_edit *edit, const char *name, int value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_i64(
	hx_os_params_edit *edit, const char *name, int64_t value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_u64(
	hx_os_params_edit *edit, const char *name, uint64_t value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_double(
	hx_os_params_edit *edit, const char *name, double value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_pointer(
	hx_os_params_edit *edit, const char *name, void *value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_string(
	hx_os_params_edit *edit, const char *name, hx_string_view value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_cstr(
	hx_os_params_edit *edit, const char *name, const char *value);

HX_OS_API void HX_OS_CALL hx_os_params_edit_set_bytes(hx_os_params_edit *edit,
	const char *name, const void *data, size_t size);

#ifdef __cplusplus
}
#endif
#endif
