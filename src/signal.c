#include "signal_private.h"
hx_os_status hx_os_signal_options_init(
	hx_os_signal_options *options, size_t capacity)
{
	return hx_os_i_dispatch_options_init(options, capacity);
}
hx_os_status hx_os_signal_handler_create(
	hx_os_context *context, hx_os_signal_handler **out, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_handler_create(context, out, error);
#else
	(void)context;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_handler_create_with_options(hx_os_context *context,
	const hx_os_signal_options *options, hx_os_signal_handler **out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_handler_create_with_options(
		context, options, out, error);
#else
	(void)context;
	(void)options;
	if (out)
		*out = NULL;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_connect(hx_os_signal_handler *handler,
	const char *signal, hx_os_signal_fn function, void *user,
	hx_os_signal_cleanup_fn cleanup, hx_os_signal_connection *out,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_connect(
		handler, signal, function, user, cleanup, out, error);
#else
	(void)handler;
	(void)signal;
	(void)function;
	(void)user;
	(void)cleanup;
	if (out)
		*out = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_emit(hx_os_signal_handler *handler,
	const char *signal, const hx_os_calldata *data, size_t *called,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_emit(handler, signal, data, called, error);
#else
	(void)handler;
	(void)signal;
	(void)data;
	if (called)
		*called = 0;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_disconnect(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_disconnect(handler, connection, error);
#else
	(void)handler;
	(void)connection;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_disconnect_until(hx_os_signal_handler *handler,
	hx_os_signal_connection connection, hx_os_deadline deadline,
	hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_disconnect_until(
		handler, connection, deadline, error);
#else
	(void)handler;
	(void)connection;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
void hx_os_signal_handler_stop(hx_os_signal_handler *handler)
{
#if HX_OS_ENABLE_SIGNALS
	hx_os_i_dispatch_handler_stop(handler);
#else
	(void)handler;
#endif
}
hx_os_status hx_os_signal_handler_close_until(hx_os_signal_handler **owner,
	hx_os_deadline deadline, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_handler_close_until(owner, deadline, error);
#else
	(void)owner;
	(void)deadline;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
hx_os_status hx_os_signal_handler_destroy(
	hx_os_signal_handler **handler, hx_os_error *error)
{
#if HX_OS_ENABLE_SIGNALS
	return hx_os_i_dispatch_handler_destroy(handler, error);
#else
	(void)handler;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
