#ifndef HX_C_PLATFORM_SETTINGS_H
#define HX_C_PLATFORM_SETTINGS_H

#include "hx_config.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_settings hx_settings;
typedef struct hx_settings_value hx_settings_value;

/**
 * \brief Fixed-layout lease of immutable UTF-8 bytes.
 *
 * \details Initialize before first use; no raw assignment copies. A getter
 * replaces a previous lease on success, preserving it on failure. Release/copy
 * through these APIs. The value keeps its allocator alive and survives settings
 * mutation/close. Exclude concurrent changes to the lease itself. Future
 * extensions require a new type.
 */
typedef struct hx_settings_string_ref {
	hx_settings_value *value;
	hx_string_view text;
} hx_settings_string_ref;

#define HX_SETTINGS_STRING_REF_INIT                                            \
	{                                                                      \
		NULL,                                                          \
		{                                                              \
			NULL, 0                                                \
		}                                                              \
	}
HX_OS_API void HX_OS_CALL hx_settings_string_ref_destroy(
	hx_settings_string_ref *ref);

HX_OS_API void HX_OS_CALL hx_settings_string_ref_copy(
	hx_settings_string_ref *out, const hx_settings_string_ref *source);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_string_ref(
	const hx_settings_value *value, hx_settings_string_ref *out,
	hx_os_error *error);

/**
 * \brief Fixed-layout immediate update cursor.
 *
 * \details Each successful setter commits and notifies normally. First failure
 * stops subsequent setters and preserves its native error. No rollback or
 * deferred work; arguments are call-borrowed. Target stays alive through all
 * setters; do not modify these storage fields.
 */
typedef struct hx_settings_update {
	hx_os_error error;
	hx_settings *settings;
	size_t applied;
} hx_settings_update;

HX_OS_API hx_settings_update HX_OS_CALL hx_settings_update_begin(
	hx_settings *settings);

HX_OS_API void HX_OS_CALL hx_settings_update_set_bool(
	hx_settings_update *update, const char *key, int value);

HX_OS_API void HX_OS_CALL hx_settings_update_set_i64(
	hx_settings_update *update, const char *key, int64_t value);

HX_OS_API void HX_OS_CALL hx_settings_update_set_double(
	hx_settings_update *update, const char *key, double value);

HX_OS_API void HX_OS_CALL hx_settings_update_set_string(
	hx_settings_update *update, const char *key, const char *value);

HX_OS_API void HX_OS_CALL hx_settings_update_set_string_view(
	hx_settings_update *update, hx_string_view key, hx_string_view value);

HX_OS_API void HX_OS_CALL hx_settings_update_set_value(
	hx_settings_update *update, hx_string_view key,
	const hx_settings_value *value);

HX_OS_API void HX_OS_CALL hx_settings_update_unset(
	hx_settings_update *update, hx_string_view key);

/**
 * \brief Copies the first error and successful-operation count, including
 * failure.
 *
 * \details Pure inspection; repeated finish is allowed. applied saturates at
 * SIZE_MAX.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_update_finish(
	const hx_settings_update *update, size_t *applied, hx_os_error *error);

enum {
	HX_SETTINGS_NULL,
	HX_SETTINGS_BOOL,
	HX_SETTINGS_I64,
	HX_SETTINGS_DOUBLE,
	HX_SETTINGS_STRING,
	HX_SETTINGS_OBJECT,
	HX_SETTINGS_ARRAY,
	HX_SETTINGS_POINTER
};

enum { HX_SETTINGS_EXPLICIT, HX_SETTINGS_DEFAULTS, HX_SETTINGS_EFFECTIVE };

enum { HX_SETTINGS_SET, HX_SETTINGS_UNSET, HX_SETTINGS_REPLACE };

typedef struct hx_settings_pair {
	hx_string_view name;
	const hx_settings_value *value;
} hx_settings_pair;

/**
 * \brief Immutable owned values.
 *
 * \details Factories deep-copy borrowed input, including array/object members,
 * into context. Hard construction bounds: 65536 nodes, 16 MiB including
 * allocator overhead, depth 64. A retained value may outlive its mutable
 * settings owner and pins its allocator context until release. Keys/strings are
 * exact UTF-8 bytes and allow NUL; no normalization/coercion. Array order is
 * preserved; object iteration uses byte-sorted keys. User pointers are
 * borrowed, never freed; retaining a snapshot does not extend the pointed-to
 * object's lifetime. Serialization rejects them.
 */
HX_OS_API void HX_OS_CALL hx_settings_value_retain(hx_settings_value *value);

HX_OS_API void HX_OS_CALL hx_settings_value_release(hx_settings_value *value);

HX_OS_API int HX_OS_CALL hx_settings_value_kind(const hx_settings_value *value);

HX_OS_API size_t HX_OS_CALL hx_settings_value_count(
	const hx_settings_value *value);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_null(
	hx_os_context *context, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_bool(hx_os_context *context,
	int value, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_i64(hx_os_context *context,
	int64_t value, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_double(
	hx_os_context *context, double value, hx_settings_value **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_string(
	hx_os_context *context, hx_string_view value, hx_settings_value **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_pointer(
	hx_os_context *context, void *value, hx_settings_value **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_array(
	hx_os_context *context, const hx_settings_value *const *items,
	size_t count, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_object(
	hx_os_context *context, const hx_settings_pair *items, size_t count,
	hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_copy(hx_os_context *context,
	const hx_settings_value *value, hx_settings_value **out,
	hx_os_error *error);

/**
 * \brief Strict typed getters; failure preserves output.
 *
 * \details String output uses its own context. at/member return retained
 * values. name_at returns copied object keys using out's context. Scalar count
 * is zero (string length via getter).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_bool(
	const hx_settings_value *value, int *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_i64(
	const hx_settings_value *value, int64_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_double(
	const hx_settings_value *value, double *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_string(
	const hx_settings_value *value, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_get_pointer(
	const hx_settings_value *value, void **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_at(
	const hx_settings_value *value, size_t index, hx_settings_value **out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_member(
	const hx_settings_value *value, hx_string_view name,
	hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_value_name_at(
	const hx_settings_value *value, size_t index, hx_string *out,
	hx_os_error *error);

/**
 * \brief Fixed callback payload; future extensions use a new event type.
 */
typedef struct hx_settings_change {
	hx_string_view key;
	const hx_settings_value *before;
	const hx_settings_value *after;
	uint64_t revision;
	uint32_t kind;
	uint32_t layer;
} hx_settings_change;

/**
 * \brief Synchronous observer after successful commit, outside internal locks.
 *
 * \details All event fields borrow storage through return. before/after are
 * NULL for missing values; REPLACE carries entire explicit roots and an empty
 * key. Concurrent writers may invoke observers concurrently/out of revision
 * order. The event's snapshots remain exact; a new query can see a later
 * revision. Reads may re-enter; nested mutation is bounded. Observer/cleanup
 * must return normally, without throwing, longjmp, exiting the thread or
 * switching fibers. Observers report no operation failure and cannot roll back
 * a commit.
 */
typedef void(HX_OS_CALL *hx_settings_change_fn)(
	hx_settings *settings, const hx_settings_change *change, void *user);
typedef void(HX_OS_CALL *hx_settings_cleanup_fn)(void *user);

typedef struct hx_settings_options {
	uint32_t size;
	uint32_t version;
	size_t max_nodes;
	size_t max_memory_bytes;
	size_t max_depth;
	size_t max_active_operations;
	size_t max_retries;
	size_t max_callback_depth;
	hx_settings_change_fn on_change;
	hx_settings_cleanup_fn cleanup;
	void *user;
} hx_settings_options;

#define HX_SETTINGS_OPTIONS_INIT                                               \
	{                                                                      \
		sizeof(hx_settings_options), 1, 4096, 4 * 1024 * 1024, 32, 64, \
			8, 16, NULL, NULL, NULL                                \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_settings_options_init(
	hx_settings_options *options, size_t capacity);

/**
 * \brief Independent mutable object with separate explicit/default roots.
 *
 * \details Defaults apply only when the literal key is absent, never to wrong
 * types or nested object members. No logging, global runtime, background thread
 * or locale changes. Objects are thread-safe; raw handle lifetime still needs
 * exclusion. Each root obeys the options' limits. Old/new caller-owned
 * snapshots may coexist; limits are per tree, not a process-wide memory quota.
 * Updates/merges use bounded temporary pair arrays in addition to retained tree
 * storage. max_callback_depth bounds observer nesting independently. Acceptance
 * adopts optional cleanup once; rejected creation does not call it.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_create(
	hx_os_context *context, hx_settings **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_create_with_options(
	hx_os_context *context, const hx_settings_options *options,
	hx_settings **out, hx_os_error *error);

/**
 * \brief Deep copy both layers into context; observer/cleanup are not copied.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_copy(hx_os_context *context,
	hx_settings *source, hx_settings **out, hx_os_error *error);

/**
 * \brief Overlay explicit top-level keys from a stable source snapshot.
 *
 * \details Unmentioned target keys and target defaults remain unchanged. Nested
 * objects/arrays are replaced as complete values, not recursively merged.
 * Values are deep-copied into the target context; user pointers remain
 * borrowed. Self-apply is valid. Each key commits and notifies separately, in
 * byte-sorted key order. Stop at the first failure and preserve prior commits;
 * applied (optional) is always initialized and reports successful keys even on
 * failure. Concurrent writers and reentrant observers retain the normal setter
 * contract, not a transaction. apply_value requires an immutable object; a
 * scalar/array is TYPE_MISMATCH.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_apply(hx_settings *settings,
	hx_settings *source, size_t *applied, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_apply_value(hx_settings *settings,
	const hx_settings_value *object, size_t *applied, hx_os_error *error);

/**
 * \brief Generic key-view entries also represent empty/NUL-containing JSON
 * keys.
 *
 * \details Values are copied into the settings allocator; getters return
 * retained immutable snapshots. Typed conveniences below take NUL-terminated
 * keys. Transactional mutation failure leaves both layers unchanged. Contending
 * updates retry at most max_retries, then return BUSY without losing updates.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_value(hx_settings *settings,
	hx_string_view key, const hx_settings_value *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_value(
	hx_settings *settings, hx_string_view key,
	const hx_settings_value *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_value(hx_settings *settings,
	hx_string_view key, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_string_ref(
	hx_settings *settings, const char *key, hx_settings_string_ref *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_string_ref_view(
	hx_settings *settings, hx_string_view key, hx_settings_string_ref *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_unset(hx_settings *settings,
	hx_string_view key, int layer, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_snapshot(hx_settings *settings,
	int layer, hx_settings_value **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_i64(hx_settings *settings,
	const char *key, int64_t value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_double(hx_settings *settings,
	const char *key, double value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_string(hx_settings *settings,
	const char *key, const char *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_string_view(
	hx_settings *settings, hx_string_view key, hx_string_view value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_pointer(hx_settings *settings,
	const char *key, void *value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_null(
	hx_settings *settings, const char *key, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_array(hx_settings *settings,
	const char *key, const hx_settings_value *const *items, size_t count,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_object(hx_settings *settings,
	const char *key, const hx_settings_value *object, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_bool(
	hx_settings *settings, const char *key, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_i64(
	hx_settings *settings, const char *key, int64_t value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_double(
	hx_settings *settings, const char *key, double value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_string(
	hx_settings *settings, const char *key, const char *value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_string_view(
	hx_settings *settings, hx_string_view key, hx_string_view value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_pointer(
	hx_settings *settings, const char *key, void *value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_null(
	hx_settings *settings, const char *key, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_array(
	hx_settings *settings, const char *key,
	const hx_settings_value *const *items, size_t count,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_set_default_object(
	hx_settings *settings, const char *key, const hx_settings_value *object,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_bool(
	hx_settings *settings, const char *key, int *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_i64(hx_settings *settings,
	const char *key, int64_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_double(hx_settings *settings,
	const char *key, double *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_string(hx_settings *settings,
	const char *key, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_get_pointer(
	hx_settings *settings, const char *key, void **out, hx_os_error *error);

/**
 * \brief Monotonic close waits for accepted operations and observers, then
 * invokes cleanup outside locks and consumes the object.
 *
 * \details NOW/INFINITE are allowed. Timeout preserves *settings; self-close
 * returns DEADLOCK. The owner excludes other API entrants and parallel close
 * during the final lifetime transition. Caller-retained immutable snapshots
 * remain valid after successful close.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_close_until(
	hx_settings **settings, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_destroy(
	hx_settings **settings, hx_os_error *error);

/**
 * \brief Configuration integration requires CONFIG; serialization additionally
 * needs CONFIG_SERIALIZE and save needs FILE_UTILS.
 *
 * \details Read-only hx_config is unchanged. Load/import build a complete
 * explicit object before publishing, preserve defaults, and leave state
 * unchanged on failure. Incompatible XML/datetime kinds or out-of-range
 * integers fail without conversion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_import(hx_settings *settings,
	const hx_os_config_document *document, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_to_config(hx_settings *settings,
	int layer, const hx_os_config_options *options,
	hx_os_config_document **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_load(hx_settings *settings,
	int format, const char *path, const hx_os_config_options *options,
	hx_os_config_error *error);

/**
 * \brief Default serialization/save uses explicit values only.
 *
 * \details Defaults stay in the program. Formats follow config's
 * representability rules; XML needs an element tree and is unrepresentable for
 * settings object roots; user pointers fail, never disappear. Save uses atomic
 * file publication with the existing durability/partial-native-replacement
 * contract.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_settings_serialize(hx_settings *settings,
	int format, hx_string *out, const hx_os_config_options *options,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_save(hx_settings *settings,
	int format, const char *path, const hx_os_config_options *options,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_to_json(
	hx_settings *settings, hx_string *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_save_json(
	hx_settings *settings, const char *path, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_settings_load_json(
	hx_settings *settings, const char *path, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
