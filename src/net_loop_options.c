#include "loop_private.h"
#include "hx_os_loop_options.h"

#if HX_OS_ENABLE_LOOP && HX_OS_ENABLE_NETWORK
static hx_os_status hx_os_loop_socket_check(
	hx_os_loop *loop, hx_os_socket *socket, hx_os_error *error)
{
	if (!loop || !socket || socket->loop != loop)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	if (loop->owner != hx_os_thread_id())
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	if (loop->closing || loop->closed)
		return hx_os_i_error(error, HX_OS_CLOSED, 0, 0);
	if (hx_os_atomic_load(&socket->pending, HX_OS_ACQUIRE))
		return hx_os_i_error(error, HX_OS_BUSY, 0, 0);
	return HX_OS_OK;
}
#endif

hx_os_status hx_os_loop_socket_configure_tcp(hx_os_loop *loop,
	hx_os_socket *socket, const hx_os_tcp_options *options,
	hx_os_error *error)
{
#if HX_OS_ENABLE_LOOP && HX_OS_ENABLE_NETWORK
	hx_os_status status = hx_os_loop_socket_check(loop, socket, error);

	if (status != HX_OS_OK)
		return status;
	// Owner-only, no I/O pending, no callbacks: preserve native
	// association.
	socket->loop = NULL;
	status = hx_os_socket_apply_tcp_options(socket, options, error);
	socket->loop = loop;
	return status;
#else
	(void)loop;
	(void)socket;
	(void)options;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}

hx_os_status hx_os_loop_socket_multicast(hx_os_loop *loop, hx_os_socket *socket,
	const hx_os_address *group, uint32_t interface_index, int join,
	hx_os_error *error)
{
#if HX_OS_ENABLE_LOOP && HX_OS_ENABLE_NETWORK
	hx_os_status status = hx_os_loop_socket_check(loop, socket, error);

	if (status != HX_OS_OK)
		return status;
	if (join != 0 && join != 1)
		return hx_os_i_error(error, HX_OS_INVALID, 0, 0);
	socket->loop = NULL;
	status = join ? hx_os_socket_join_multicast(
				socket, group, interface_index, error)
		      : hx_os_socket_leave_multicast(
				socket, group, interface_index, error);
	socket->loop = loop;
	return status;
#else
	(void)loop;
	(void)socket;
	(void)group;
	(void)interface_index;
	(void)join;
	return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);
#endif
}
