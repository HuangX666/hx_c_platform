#ifndef HX_C_PLATFORM_PROCEDURE_H
#define HX_C_PLATFORM_PROCEDURE_H

#include "hx_os_signal.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct hx_os_proc_handler hx_os_proc_handler;
typedef uint64_t hx_os_proc_registration;

typedef hx_os_status(HX_OS_CALL *hx_os_proc_fn)(
	hx_os_calldata *parameters, void *user, hx_os_error *error);

typedef hx_os_status(HX_OS_CALL *hx_os_proc_params_fn)(
	hx_os_params *parameters, void *user, hx_os_error *error);

typedef void(HX_OS_CALL *hx_os_proc_cleanup_fn)(void *user);

typedef struct hx_os_proc_options {
	uint32_t size;
	uint32_t version;
	size_t max_procedures;
	size_t max_parallel_calls;
	size_t max_depth;
} hx_os_proc_options;

#define HX_OS_PROC_OPTIONS_INIT                                                \
	{                                                                      \
		sizeof(hx_os_proc_options), 1, 256, 64, 16                     \
	}
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_options_init(
	hx_os_proc_options *options, size_t capacity);

HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_create(
	hx_os_context *context, hx_os_proc_handler **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_create_with_options(
	hx_os_context *context, const hx_os_proc_options *options,
	hx_os_proc_handler **out, hx_os_error *error);

/**
 * \brief Copies a strict UTF-8 name, 1..127 bytes.
 *
 * \details Duplicate active names are EXISTS. register leaves lifetime with the
 * handler. add optionally returns an ID for unregister. Successful add adopts
 * cleanup, called once outside locks after in-flight calls finish; rejected add
 * does not adopt user/cleanup.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_register(
	hx_os_proc_handler *handler, const char *name, hx_os_proc_fn function,
	void *user, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_add(hx_os_proc_handler *handler,
	const char *name, hx_os_proc_fn function, void *user,
	hx_os_proc_cleanup_fn cleanup, hx_os_proc_registration *out,
	hx_os_error *error);

/**
 * \brief Synchronous exactly-one lookup.
 *
 * \details Missing/disconnected-before-start is NOT_FOUND. Concurrent calls can
 * overlap; callbacks run outside locks. params is copied for invocation and
 * committed only on OK. Borrowed pointer pointee effects cannot be rolled back.
 * NULL params is an empty local pack. No signature DSL or type coercion: use
 * typed calldata getters/setters. User code must return normally (no
 * throw/longjmp/thread exit/fiber switch).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_call(hx_os_proc_handler *handler,
	const char *name, hx_os_calldata *parameters, hx_os_error *error);

/**
 * \brief Growable counterpart, requires PROCEDURES and PARAMETERS.
 *
 * \details Names are unique across both callback types. Cross-type calls return
 * TYPE_MISMATCH, with no user callback. Lifetime/retirement matches add.
 * Callback borrows its mutable private pack; destruction during the invocation
 * returns BUSY.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_add_params(
	hx_os_proc_handler *handler, const char *name,
	hx_os_proc_params_fn function, void *user,
	hx_os_proc_cleanup_fn cleanup, hx_os_proc_registration *out,
	hx_os_error *error);

/**
 * \brief Copies input and its limits using the handler context; NULL is empty
 * with default limits.
 *
 * \details Publishes a new owned result only on success (*out=NULL on failure).
 * Input stays alive and unchanged through the call. Own result with
 * params_destroy. Callback side effects through borrowed pointers/OS objects
 * cannot be rolled back. No implicit conversion to/from fixed calldata.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_call_params(
	hx_os_proc_handler *handler, const char *name,
	const hx_os_params *input, hx_os_params **out, hx_os_error *error);

/**
 * \brief Immediately prevent later starts, then wait for active calls and
 * cleanup.
 *
 * \details Timeout retains the ID/handler; retry with the SAME ID.
 * Self/ancestor wait is DEADLOCK. A new registration gets a new ID even for the
 * same name.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_unregister_until(
	hx_os_proc_handler *handler, hx_os_proc_registration registration,
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_os_proc_stop(hx_os_proc_handler *handler);

/**
 * \brief Stop/retire/release.
 *
 * \details Timeout or self-close retains *handler. Slow synchronous user
 * callbacks/cleanup can exceed a deadline; no hard cancellation. Finish
 * registration/unregistration/control calls before final close; accepted
 * callbacks may finish concurrently. Exclude new API entrants and parallel
 * final closes. Same-handler allocator reentry during add/close returns
 * DEADLOCK; stop is ignored during that reentry. Finish host calls and retire
 * every callback/user pointer before unloading its module.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_close_until(
	hx_os_proc_handler **handler, hx_os_deadline deadline,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_proc_destroy(
	hx_os_proc_handler **handler, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
