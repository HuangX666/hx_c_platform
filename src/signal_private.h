#ifndef HX_C_PLATFORM_SIGNAL_PRIVATE_H
#define HX_C_PLATFORM_SIGNAL_PRIVATE_H

#include "internal.h"
#include "hx_os_signal.h"

void *hx_os_i_signal_exchange_execution(void *value);

enum { HX_OS_SIGNAL_FIXED, HX_OS_SIGNAL_VIEW, HX_OS_SIGNAL_MUTABLE };

typedef union hx_os_signal_callback {
	hx_os_signal_fn named;
	hx_os_signal_all_fn all;
	hx_os_signal_mutable_fn modify;
} hx_os_signal_callback;

struct hx_os_signal_arguments {
	const hx_os_calldata *data;
	const hx_os_params *params;
};

typedef struct hx_os_signal_slot {
	hx_os_signal_callback function;
	hx_os_signal_cleanup_fn cleanup;
	void *user;
	uint64_t identifier;
	uint32_t running;
	uint16_t state, kind;
	char name[128];
} hx_os_signal_slot;

struct hx_os_signal_handler {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_signal_slot *slots;
	size_t capacity;
	size_t max_parallel_emits;
	size_t max_depth;
	size_t live_slots;
	size_t emits;
	uint64_t generation;
	int closing;
	int unique_names;
};

hx_os_status hx_os_i_dispatch_options_init(
	hx_os_signal_options *options, size_t capacity);

hx_os_status hx_os_i_dispatch_handler_create(
	hx_os_context *context, hx_os_signal_handler **out, hx_os_error *error);

hx_os_status hx_os_i_dispatch_handler_create_with_options(
	hx_os_context *context, const hx_os_signal_options *options,
	hx_os_signal_handler **out, hx_os_error *error);

hx_os_status hx_os_i_dispatch_connect(hx_os_signal_handler *handler,
	const char *signal, hx_os_signal_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_connect_all(hx_os_signal_handler *handler,
	hx_os_signal_all_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_connect_view(hx_os_signal_handler *handler,
	const char *signal, hx_os_signal_all_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_connect_mutable(hx_os_signal_handler *handler,
	const char *signal, hx_os_signal_mutable_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_emit_params(hx_os_signal_handler *handler,
	const char *signal, hx_os_params *params, size_t *called,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_emit(hx_os_signal_handler *handler,
	const char *signal, const hx_os_calldata *data, size_t *called,
	hx_os_error *error);

hx_os_status hx_os_i_dispatch_disconnect(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_error *error);

hx_os_status hx_os_i_dispatch_disconnect_until(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_deadline deadline,
	hx_os_error *error);

void hx_os_i_dispatch_handler_stop(hx_os_signal_handler *handler);

hx_os_status hx_os_i_dispatch_handler_close_until(hx_os_signal_handler **owner,
	hx_os_deadline deadline, hx_os_error *error);

hx_os_status hx_os_i_dispatch_handler_destroy(
	hx_os_signal_handler **handler, hx_os_error *error);

#endif
