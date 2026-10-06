#include "signal_private.h"
#include "hx_os_procedure.h"

struct hx_os_proc_handler {
	hx_os_context *context;
	hx_os_signal_handler *dispatch;
};

typedef union hx_os_proc_callback {
	hx_os_proc_fn fixed;
	hx_os_proc_params_fn growable;
} hx_os_proc_callback;

#if HX_OS_ENABLE_PROCEDURES
typedef struct hx_os_proc_guard {
	struct hx_os_proc_guard *previous;
	hx_os_proc_handler *handler;
} hx_os_proc_guard;
#if defined(_MSC_VER)
static __declspec(thread) hx_os_proc_guard *hx_os_proc_current;
#else
static __thread hx_os_proc_guard *hx_os_proc_current;
#endif
static int hx_os_proc_guarded(hx_os_proc_handler *handler)
{
	hx_os_proc_guard *guard;

	for (guard = hx_os_proc_current; guard; guard = guard->previous)
		if (guard->handler == handler)
			return 1;

	return 0;
}

typedef struct hx_os_proc_binding {
	hx_os_proc_callback function;
	hx_os_proc_cleanup_fn cleanup;
	void *user;
} hx_os_proc_binding;

typedef struct hx_os_proc_invocation {
	void *parameters;
	int growable;
} hx_os_proc_invocation;

static hx_os_status hx_os_proc_invoke(const hx_os_calldata *data, void *user,
	int growable, hx_os_error *error)
{
	hx_os_proc_binding *binding = user;
	void *parameters = NULL;
	hx_os_proc_invocation *call;

	hx_os_status status = hx_os_calldata_get_pointer(
		data, "invocation", &parameters, error);
	if (status != HX_OS_OK)
		return status;

	call = parameters;
	if (call->growable != growable)
		return hx_os_i_error(error, HX_OS_TYPE_MISMATCH, 0, 0);

	return call->growable ? binding->function.growable(
					call->parameters, binding->user, error)
			      : binding->function.fixed(
					call->parameters, binding->user, error);
}

static hx_os_status HX_OS_CALL hx_os_proc_invoke_fixed(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	return hx_os_proc_invoke(data, user, 0, error);
}

static hx_os_status HX_OS_CALL hx_os_proc_invoke_params(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	return hx_os_proc_invoke(data, user, 1, error);
}

static void HX_OS_CALL hx_os_proc_retire(void *user)
{
	hx_os_proc_binding *binding = user;

	if (binding->cleanup)
		binding->cleanup(binding->user);

	hx_os_memory_free(binding);
}

static hx_os_status hx_os_proc_dispatch(hx_os_proc_handler *handler,
	const char *name, void *parameters, int growable, hx_os_error *error)
{
	hx_os_proc_invocation call = {parameters, growable};
	hx_os_calldata invocation = HX_OS_CALLDATA_INIT;
	size_t called = 0;
	hx_os_status status = hx_os_calldata_set_pointer(
		&invocation, "invocation", &call, error);

	if (status == HX_OS_OK)
		status = hx_os_i_dispatch_emit(
			handler->dispatch, name, &invocation, &called, error);
	if (status == HX_OS_OK && !called)
		status = hx_os_i_error(error, HX_OS_NOT_FOUND, 0, 0);

	return status;
}
#endif
hx_os_status hx_os_proc_options_init(
	hx_os_proc_options *options, size_t capacity)
{
	const hx_os_proc_options defaults = HX_OS_PROC_OPTIONS_INIT;

	if (!options)
		return HX_OS_INVALID;

	if (capacity < sizeof(*options))
		return HX_OS_OUTPUT_FULL;

	*options = defaults;

	return HX_OS_OK;
}

hx_os_status hx_os_proc_create(
	hx_os_context *context, hx_os_proc_handler **out, hx_os_error *error)
{
	return hx_os_proc_create_with_options(context, NULL, out, error);
}

hx_os_status hx_os_proc_create_with_options(hx_os_context *context,
	const hx_os_proc_options *options, hx_os_proc_handler **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES
	const hx_os_proc_options defaults = HX_OS_PROC_OPTIONS_INIT;
	hx_os_signal_options limits = HX_OS_SIGNAL_OPTIONS_INIT;
	hx_os_proc_handler *handler;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	*out = NULL;
	if (!options)
		options = &defaults;

	if (options->size < sizeof(*options) || options->version != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	limits.max_connections = options->max_procedures;
	limits.max_parallel_emits = options->max_parallel_calls;
	limits.max_depth = options->max_depth;
	handler = hx_os_i_zero(context, sizeof(*handler));
	if (!handler)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	handler->context = context;

	status = hx_os_i_dispatch_handler_create_with_options(
		context, &limits, &handler->dispatch, error);
	if (status != HX_OS_OK) {
		hx_os_memory_free(handler);

		return status;
	}

	handler->dispatch->unique_names = 1;
	*out = handler;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
#else
	(void)context;
	(void)options;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_proc_register(hx_os_proc_handler *handler, const char *name,
	hx_os_proc_fn function, void *user, hx_os_error *error)
{
	return hx_os_proc_add(handler, name, function, user, NULL, NULL, error);
}

static hx_os_status hx_os_proc_add_impl(hx_os_proc_handler *handler,
	const char *name, hx_os_proc_callback function, int growable,
	void *user, hx_os_proc_cleanup_fn cleanup, hx_os_proc_registration *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES
	hx_os_proc_binding *binding;
	hx_os_proc_guard guard;
	hx_os_status status;

	if (out)
		*out = 0;

	if (!handler || (growable ? !function.growable : !function.fixed))
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_proc_guarded(handler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	guard.handler = handler;
	guard.previous = hx_os_proc_current;
	hx_os_proc_current = &guard;
	binding = hx_os_i_alloc(handler->context, sizeof(*binding), 0);
	if (!binding) {
		hx_os_proc_current = guard.previous;

		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
	}

	binding->function = function;
	binding->cleanup = cleanup;
	binding->user = user;

	status = hx_os_i_dispatch_connect(handler->dispatch, name,
		growable ? hx_os_proc_invoke_params : hx_os_proc_invoke_fixed,
		binding, hx_os_proc_retire, out, error);
	if (status != HX_OS_OK)
		hx_os_memory_free(binding);

	hx_os_proc_current = guard.previous;

	return status;
#else
	(void)handler;
	(void)name;
	(void)function;
	(void)growable;
	(void)user;
	(void)cleanup;
	if (out)
		*out = 0;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_proc_add(hx_os_proc_handler *handler, const char *name,
	hx_os_proc_fn function, void *user, hx_os_proc_cleanup_fn cleanup,
	hx_os_proc_registration *out, hx_os_error *error)
{
	hx_os_proc_callback callback;

	callback.fixed = function;

	return hx_os_proc_add_impl(
		handler, name, callback, 0, user, cleanup, out, error);
}

hx_os_status hx_os_proc_add_params(hx_os_proc_handler *handler,
	const char *name, hx_os_proc_params_fn function, void *user,
	hx_os_proc_cleanup_fn cleanup, hx_os_proc_registration *out,
	hx_os_error *error)
{
	hx_os_proc_callback callback;

	if (out)
		*out = 0;
	if (!HX_OS_ENABLE_PROCEDURES || !HX_OS_ENABLE_PARAMETERS)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
	callback.growable = function;

	return hx_os_proc_add_impl(
		handler, name, callback, 1, user, cleanup, out, error);
}

hx_os_status hx_os_proc_call(hx_os_proc_handler *handler, const char *name,
	hx_os_calldata *parameters, hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES
	hx_os_calldata copy = HX_OS_CALLDATA_INIT;
	hx_os_status status;

	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_proc_guarded(handler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	if (parameters)
		copy = *parameters;

	status = hx_os_proc_dispatch(handler, name, &copy, 0, error);

	if (status == HX_OS_OK && parameters)
		*parameters = copy;

	return status;
#else
	(void)handler;
	(void)name;
	(void)parameters;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_proc_call_params(hx_os_proc_handler *handler,
	const char *name, const hx_os_params *input, hx_os_params **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES && HX_OS_ENABLE_PARAMETERS
	hx_os_params *copy = NULL;
	hx_os_proc_guard guard;
	hx_os_status status;

	if (!out)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	*out = NULL;
	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (hx_os_proc_guarded(handler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	guard.handler = handler;
	guard.previous = hx_os_proc_current;
	hx_os_proc_current = &guard;
	status = input
		? hx_os_params_clone(input, handler->context, &copy, error)
		: hx_os_params_create(handler->context, &copy, error);
	hx_os_proc_current = guard.previous;
	if (status != HX_OS_OK)
		return status;

	status = hx_os_i_params_hold(copy, error);
	if (status == HX_OS_OK) {
		status = hx_os_proc_dispatch(handler, name, copy, 1, error);
		hx_os_i_params_release(copy);
	}
	if (status == HX_OS_OK)
		*out = copy;
	else {
		hx_os_proc_current = &guard;
		(void)hx_os_params_destroy(&copy, NULL);
		hx_os_proc_current = guard.previous;
	}

	return status;
#else
	(void)handler;
	(void)name;
	(void)input;
	if (out)
		*out = NULL;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_proc_unregister_until(hx_os_proc_handler *handler,
	hx_os_proc_registration registration, hx_os_deadline deadline,
	hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES
	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (hx_os_proc_guarded(handler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	return hx_os_i_dispatch_disconnect_until(
		handler->dispatch, registration, deadline, error);
#else
	(void)handler;
	(void)registration;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

void hx_os_proc_stop(hx_os_proc_handler *handler)
{
#if HX_OS_ENABLE_PROCEDURES
	if (handler && !hx_os_proc_guarded(handler))
		hx_os_i_dispatch_handler_stop(handler->dispatch);
#else
	(void)handler;
#endif
}

hx_os_status hx_os_proc_close_until(hx_os_proc_handler **handler,
	hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_PROCEDURES
	hx_os_status status;
	hx_os_proc_guard guard;

	if (!handler)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);

	if (!*handler)
		return hx_os_i_error(error, HX_OS_OK, 0, 0);

	if (hx_os_proc_guarded(*handler))
		return hx_os_i_error(error, HX_OS_DEADLOCK, 0, 0);

	guard.handler = *handler;
	guard.previous = hx_os_proc_current;
	hx_os_proc_current = &guard;

	status = hx_os_i_dispatch_handler_close_until(
		&(*handler)->dispatch, deadline, error);
	if (status == HX_OS_OK) {
		*handler = NULL;

		hx_os_memory_free(guard.handler);
	}

	hx_os_proc_current = guard.previous;

	return status;
#else
	(void)handler;
	(void)deadline;

	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_proc_destroy(
	hx_os_proc_handler **handler, hx_os_error *error)
{
	return hx_os_proc_close_until(handler, HX_OS_DEADLINE_NOW, error);
}
