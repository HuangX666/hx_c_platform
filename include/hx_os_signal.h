#ifndef HX_C_PLATFORM_SIGNAL_H
#define HX_C_PLATFORM_SIGNAL_H
#include "hx_string.h"
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
/* Fixed-layout stack parameter pack: 16 entries, 512 combined name/string
 * bytes including terminators. Use only init/set/get; fields are storage.
 * Names are copied strict UTF-8, 1..127 bytes. Strings copy UTF-8 bytes and
 * allow embedded NUL. Pointers are borrowed, never freed. Assignment copies
 * the pack safely because string locations are offsets, not self-pointers.
 * Set is transactional. No allocation. Exclude mutation while borrowed views
 * or concurrent reads are in use. Future extensions require new types.
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
/* Immediate, allocation-free edit cursor over an initialized pack. First
 * failure stops later setters; earlier successful edits remain. The pack
 * outlives the setters. Finish copies the first error without further work.
 * Storage layout is fixed; do not modify these members directly.
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
/* Getters preserve outputs on failure; strings borrow this pack until any
 * mutation or destruction. Missing key and wrong type are distinct errors.
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
/* Identifier belongs to one handler; never construct or reuse it with another.
 * Reused capacity gets a new generation, so old identifiers cannot disconnect
 * a new subscription. Identifiers remain usable for repeated retirement waits.
 */
typedef uint64_t hx_os_signal_connection;
typedef hx_os_status(HX_OS_CALL *hx_os_signal_fn)(
	const hx_os_calldata *data, void *user, hx_os_error *error);
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
/* Explicit bounded synchronous dispatcher; no background thread. Options
 * allocate fixed connection slots, plus at most max_connections * 8 bytes
 * per accepted emit. Limits: connections/emits <=65535, depth <=64.
 * NULL options/context select defaults. Ordinary create needs no runtime init.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_create(
	hx_os_context *context, hx_os_signal_handler **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_create_with_options(
	hx_os_context *context, const hx_os_signal_options *options,
	hx_os_signal_handler **out, hx_os_error *error);
/* Copies a strict UTF-8 signal name (1..127 bytes). Thread-safe. Acceptance
 * transfers optional cleanup (once); rejection calls neither callback nor
 * cleanup. Without cleanup, user is borrowed through successful disconnect
 * wait or handler close. out may be NULL for handler-owned subscriptions.
 * Callbacks/cleanup run outside locks and must return normally, without
 * throw/longjmp, thread exit or switching fibers during the call.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_connect(
	hx_os_signal_handler *handler, const char *signal,
	hx_os_signal_fn function, void *user, hx_os_signal_cleanup_fn cleanup,
	hx_os_signal_connection *out, hx_os_error *error);
/* Synchronous callbacks execute on the emitter in registration order for
 * this emission's snapshot. Later connections wait for the next emit; a
 * disconnected callback not yet started is skipped. Concurrent emits may
 * overlap callbacks, so callback/user state must support that use.
 * NULL data means an empty pack; otherwise borrowed until emit returns.
 * Invoke all eligible callbacks; return the first non-OK callback status.
 * Each callback receives a non-NULL error initialized to OK, borrowed until
 * return. Matching error.status preserves its native details; otherwise the
 * returned status becomes an error without a native domain.
 * Optional called receives how many started. No hard callback deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_emit(
	hx_os_signal_handler *handler, const char *signal,
	const hx_os_calldata *data, size_t *called, hx_os_error *error);
/* Prevents new starts immediately. May synchronously run eligible cleanup;
 * no waiting for active callbacks. Repeated calls on a retired ID are OK.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_disconnect(
	hx_os_signal_handler *handler, hx_os_signal_connection connection,
	hx_os_error *error);
/* Disconnect then wait for callbacks AND cleanup. Monotonic NOW/INFINITE
 * allowed; timeout preserves handler/ID/user for retry. Waiting for one's own
 * callback/cleanup reports DEADLOCK (including nested callback ancestors).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_disconnect_until(
	hx_os_signal_handler *handler, hx_os_signal_connection connection,
	hx_os_deadline deadline, hx_os_error *error);
/* Stop prevents connects/emits/new callback starts; does not run user code. */
HX_OS_API void HX_OS_CALL hx_os_signal_handler_stop(
	hx_os_signal_handler *handler);
/* Stop, retire all subscriptions, wait for accepted emits/cleanup, consume.
 * Timeout/error retains *handler for retry; self-close reports DEADLOCK.
 * Owner must exclude new API entrants and parallel close before closing;
 * already accepted emits may finish concurrently. External lifetime exclusion
 * is required for every raw handle call. Cleanup runs synchronously when
 * possible, so a slow cleanup may exceed the waiting deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_close_until(
	hx_os_signal_handler **handler, hx_os_deadline deadline,
	hx_os_error *error);
/* Convenience close with NOW, preserving the handle on BUSY/TIMEOUT. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_signal_handler_destroy(
	hx_os_signal_handler **handler, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
