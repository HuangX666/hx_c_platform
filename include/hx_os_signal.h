#ifndef HX_C_PLATFORM_SIGNAL_H
#define HX_C_PLATFORM_SIGNAL_H

#include "hx_string.h"
#include "hx_os_params.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {
	HX_OS_CALLDATA_BOOL = 1,
	HX_OS_CALLDATA_I64,
	HX_OS_CALLDATA_DOUBLE,
	HX_OS_CALLDATA_STRING,
	HX_OS_CALLDATA_POINTER
};

/**
 * \brief Fixed-layout stack parameter pack: 16 entries, 512 combined
 * name/string bytes including terminators.
 *
 * \details Use only init/set/get; fields are storage. Names are copied strict
 * UTF-8, 1..127 bytes. Strings copy UTF-8 bytes and allow embedded NUL.
 * Pointers are borrowed, never freed. Assignment copies the pack safely because
 * string locations are offsets, not self-pointers. Set is transactional. No
 * allocation. Exclude mutation while borrowed views or concurrent reads are in
 * use. Future extensions require new types.
 */
typedef union hx_os_calldata_value {
	int64_t integer;
	double real;
	void *pointer;

	struct {
		uint16_t offset, bytes;
	} text;
} hx_os_calldata_value;

typedef struct hx_os_calldata_entry {
	hx_os_calldata_value value;
	uint16_t name_offset;
	uint16_t name_bytes;
	uint32_t kind;
} hx_os_calldata_entry;

typedef struct hx_os_calldata {
	hx_os_calldata_entry entries[16];
	unsigned char text[512];
	uint32_t count;
	uint32_t text_bytes;
} hx_os_calldata;

#define HX_OS_CALLDATA_INIT                                                    \
	{                                                                      \
		0                                                              \
	}

/**
 * \brief Immediate, allocation-free edit cursor over an initialized pack.
 *
 * \details First failure stops later setters; earlier successful edits remain.
 * The pack outlives the setters. Finish copies the first error without further
 * work. Storage layout is fixed; do not modify these members directly.
 */
typedef struct hx_os_calldata_edit {
	hx_os_error error;
	hx_os_calldata *data;
} hx_os_calldata_edit;

HX_OS_API hx_os_calldata_edit HX_OS_CALL hx_os_calldata_edit_begin(
	hx_os_calldata *data);

HX_OS_API void HX_OS_CALL hx_os_calldata_edit_set_bool(
	hx_os_calldata_edit *edit, const char *name, int value);

HX_OS_API void HX_OS_CALL hx_os_calldata_edit_set_i64(
	hx_os_calldata_edit *edit, const char *name, int64_t value);

HX_OS_API void HX_OS_CALL hx_os_calldata_edit_set_double(
	hx_os_calldata_edit *edit, const char *name, double value);

HX_OS_API void HX_OS_CALL hx_os_calldata_edit_set_string(
	hx_os_calldata_edit *edit, const char *name, hx_string_view value);

HX_OS_API void HX_OS_CALL hx_os_calldata_edit_set_pointer(
	hx_os_calldata_edit *edit, const char *name, void *value);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_edit_finish(
	const hx_os_calldata_edit *edit, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_init(
	hx_os_calldata *data, size_t capacity);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_set_bool(
	hx_os_calldata *data, const char *name, int value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_set_i64(hx_os_calldata *data,
	const char *name, int64_t value, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_set_double(
	hx_os_calldata *data, const char *name, double value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_set_string(
	hx_os_calldata *data, const char *name, hx_string_view value,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_set_pointer(
	hx_os_calldata *data, const char *name, void *value,
	hx_os_error *error);

/**
 * \brief Getters preserve outputs on failure; strings borrow this pack until
 * any mutation or destruction.
 *
 * \details Missing key and wrong type are distinct errors.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_get_bool(
	const hx_os_calldata *data, const char *name, int *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_get_i64(
	const hx_os_calldata *data, const char *name, int64_t *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_get_double(
	const hx_os_calldata *data, const char *name, double *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_get_string(
	const hx_os_calldata *data, const char *name, hx_string_view *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_calldata_get_pointer(
	const hx_os_calldata *data, const char *name, void **out,
	hx_os_error *error);

typedef struct hx_os_signal_handler hx_os_signal_handler;
/**
 * \brief Identifier belongs to one handler; never construct or reuse it with
 * another.
 *
 * \details Reused capacity gets a new generation, so old identifiers cannot
 * disconnect a new subscription. Identifiers remain usable for repeated
 * retirement waits.
 */
typedef uint64_t hx_os_signal_connection;

typedef hx_os_status(HX_OS_CALL *hx_os_signal_fn)(
	const hx_os_calldata *data, void *user, hx_os_error *error);

/**
 * \brief Read-only callback view.
 *
 * \details Its object, names and string/byte views are borrowed only until this
 * callback returns. Never retain/destroy/cast it to an owner. Named reads are
 * strict: missing keys are NOT_FOUND, wrong types are TYPE_MISMATCH; outputs
 * are preserved on failure. Enumeration order is not a persistent key order.
 * Reading needs no allocation. Fixed calldata reads work without PARAMETERS;
 * emit_params supplies the full growable value set.
 */
typedef struct hx_os_signal_arguments hx_os_signal_arguments;

HX_OS_API size_t HX_OS_CALL hx_os_signal_arguments_count(
	const hx_os_signal_arguments *arguments);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get(
	const hx_os_signal_arguments *arguments, const char *name,
	hx_os_param_value *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_at(
	const hx_os_signal_arguments *arguments, size_t index,
	hx_string_view *name, hx_os_param_value *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_bool(
	const hx_os_signal_arguments *arguments, const char *name, int *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_i64(
	const hx_os_signal_arguments *arguments, const char *name, int64_t *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_u64(
	const hx_os_signal_arguments *arguments, const char *name,
	uint64_t *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_bytes(
	const hx_os_signal_arguments *arguments, const char *name,
	hx_os_bytes_view *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_double(
	const hx_os_signal_arguments *arguments, const char *name, double *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_string(
	const hx_os_signal_arguments *arguments, const char *name,
	hx_string_view *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_get_pointer(
	const hx_os_signal_arguments *arguments, const char *name, void **out,
	hx_os_error *error);

/**
 * \brief Explicit owned copy for use after callback return.
 *
 * \details Requires PARAMETERS, uses the supplied context. Fixed calldata uses
 * default bounds; growable input keeps its limits. Strings/bytes are copied;
 * user pointers remain borrowed. Creation sets *out=NULL before work and
 * releases all partial state on failure. Input remains alive and unchanged
 * through copying, including allocator callbacks. Release with params_destroy.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_arguments_copy(
	const hx_os_signal_arguments *arguments, hx_os_context *context,
	hx_os_params **out, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_signal_all_fn)(const char *signal,
	const hx_os_signal_arguments *arguments, void *user,
	hx_os_error *error);

typedef void(HX_OS_CALL *hx_os_signal_cleanup_fn)(void *user);

typedef struct hx_os_signal_options {
	uint32_t size;
	uint32_t version;
	size_t max_connections;
	size_t max_parallel_emits;
	size_t max_depth;
} hx_os_signal_options;

#define HX_OS_SIGNAL_OPTIONS_INIT                                              \
	{                                                                      \
		sizeof(hx_os_signal_options), 1, 256, 64, 16                   \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_options_init(
	hx_os_signal_options *options, size_t capacity);

/**
 * \brief Explicit bounded synchronous dispatcher; no background thread.
 *
 * \details Options allocate fixed connection slots, plus at most
 * max_connections * 8 bytes per accepted emit. Limits: connections/emits
 * <=65535, depth <=64. NULL options/context select defaults. Ordinary create
 * needs no runtime init.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_create(
	hx_os_context *context, hx_os_signal_handler **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_create_with_options(
	hx_os_context *context, const hx_os_signal_options *options,
	hx_os_signal_handler **out, hx_os_error *error);

/**
 * \brief Copies a strict UTF-8 signal name (1..127 bytes).
 *
 * \details Thread-safe. Acceptance transfers optional cleanup (once); rejection
 * calls neither callback nor cleanup. Without cleanup, user is borrowed through
 * successful disconnect wait or handler close. out may be NULL for
 * handler-owned subscriptions. Callbacks/cleanup run outside locks and must
 * return normally, without throw/longjmp, thread exit or switching fibers
 * during the call.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect(
	hx_os_signal_handler *handler, const char *signal,
	hx_os_signal_fn function, void *user, hx_os_signal_cleanup_fn cleanup,
	hx_os_signal_connection *out, hx_os_error *error);

/**
 * \brief Observe every signal on this handler, including names first emitted
 * later.
 *
 * \details Uses one ordinary connection slot, with the same
 * cleanup/disconnect/wait contract. Named and all-signal callbacks share
 * registration order and the same emission snapshot/error rules. signal is
 * borrowed until callback return; arguments is a read-only view with optional
 * explicit owned copying. This is not a process-global subscription or a
 * wildcard-name convention.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect_all(
	hx_os_signal_handler *handler, hx_os_signal_all_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

/**
 * \brief Same read-only view as connect_all, filtered to one exact UTF-8 name.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect_observer(
	hx_os_signal_handler *handler, const char *signal,
	hx_os_signal_all_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_signal_mutable_fn)(const char *signal,
	hx_os_params *parameters, void *user, hx_os_error *error);

/**
 * \brief Growable, mutable arguments require SIGNALS and PARAMETERS.
 *
 * \details Connections share the existing bounded slots, registration order and
 * retirement rules. Only emit_params invokes mutable callbacks. Ordinary
 * read-only emit reports TYPE_MISMATCH for such subscriptions without invoking
 * them, and continues. The callback borrows the pack: use params
 * getters/setters, never retain or destroy it. Destruction while dispatch is
 * borrowing it returns BUSY.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect_mutable(
	hx_os_signal_handler *handler, const char *signal,
	hx_os_signal_mutable_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect_all_mutable(
	hx_os_signal_handler *handler, hx_os_signal_mutable_fn function,
	void *user, hx_os_signal_cleanup_fn cleanup,
	hx_os_signal_connection *out, hx_os_error *error);

/**
 * \brief Synchronous in-place dispatch.
 *
 * \details Each successful setter is visible to later callbacks and the caller,
 * including when a callback subsequently fails. Continue all eligible
 * subscriptions, return the first error; no rollback. Callbacks/read-only
 * observers share registration order. called counts user callbacks actually
 * started. Allocation failure before dispatch changes no values. Exclude
 * external access to this pack through return; nested calls on the same pack
 * are synchronous and must obey the same view lifetime.
 *
 * Existing fixed-calldata callbacks receive a temporary copied projection.
 * Unrepresentable types (bytes/u64) return UNREPRESENTABLE; fixed capacity
 * overflow returns LIMIT. That callback is skipped, the error is retained, and
 * other subscriptions continue. Use connect_observer for lossless reads.
 * Borrowed field views expire at the next mutation, possibly a nested emit.
 * parameters is required and stays owned by its caller.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_emit_params(
	hx_os_signal_handler *handler, const char *signal,
	hx_os_params *parameters, size_t *called, hx_os_error *error);

/**
 * \brief Synchronous callbacks execute on the emitter in registration order for
 * this emission's snapshot.
 *
 * \details Later connections wait for the next emit; a disconnected callback
 * not yet started is skipped. Concurrent emits may overlap callbacks, so
 * callback/user state must support that use. NULL data means an empty pack;
 * otherwise borrowed until emit returns. Invoke all eligible callbacks; return
 * the first non-OK callback status. Each callback receives a non-NULL error
 * initialized to OK, borrowed until return. Matching error.status preserves its
 * native details; otherwise the returned status becomes an error without a
 * native domain. Optional called receives how many started. No hard callback
 * deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_emit(
	hx_os_signal_handler *handler, const char *signal,
	const hx_os_calldata *data, size_t *called, hx_os_error *error);

/**
 * \brief Prevents new starts immediately.
 *
 * \details May synchronously run eligible cleanup; no waiting for active
 * callbacks. Repeated calls on a retired ID are OK.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_disconnect(
	hx_os_signal_handler *handler, hx_os_signal_connection connection,
	hx_os_error *error);

/**
 * \brief Disconnect then wait for callbacks AND cleanup.
 *
 * \details Monotonic NOW/INFINITE allowed; timeout preserves handler/ID/user
 * for retry. Waiting for one's own callback/cleanup reports DEADLOCK (including
 * nested callback ancestors).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_disconnect_until(
	hx_os_signal_handler *handler, hx_os_signal_connection connection,
	hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief Stop prevents connects/emits/new callback starts; does not run user
 * code.
 */
HX_OS_API void HX_OS_CALL hx_os_signal_handler_stop(
	hx_os_signal_handler *handler);

/**
 * \brief Stop, retire all subscriptions, wait for accepted emits/cleanup,
 * consume.
 *
 * \details Timeout/error retains *handler for retry; self-close reports
 * DEADLOCK. Owner must exclude new API entrants and parallel close before
 * closing; already accepted emits may finish concurrently. External lifetime
 * exclusion is required for every raw handle call. Cleanup runs synchronously
 * when possible, so a slow cleanup may exceed the waiting deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_close_until(
	hx_os_signal_handler **handler, hx_os_deadline deadline,
	hx_os_error *error);

/**
 * \brief Convenience close with NOW, preserving the handle on BUSY/TIMEOUT.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_destroy(
	hx_os_signal_handler **handler, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
