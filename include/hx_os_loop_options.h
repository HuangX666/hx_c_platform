#ifndef HX_C_PLATFORM_LOOP_OPTIONS_H
#define HX_C_PLATFORM_LOOP_OPTIONS_H

#include "hx_os_loop.h"
#include "hx_os_dial.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Configure an attached socket from its loop's owner thread.
 * \details Finish or cancel pending I/O and consume its completion first;
 * pending operations return BUSY. Other threads may submit an ordinary POST
 * request whose callback invokes these functions. No extra queue is created.
 * Loop/socket must stay alive; normal close/ownership exclusion still applies.
 * TCP updates can partially succeed. Dual-stack remains a pre-bind option.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_socket_configure_tcp(
	hx_os_loop *loop, hx_os_socket *socket,
	const hx_os_tcp_options *options, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_loop_socket_multicast(hx_os_loop *loop,
	hx_os_socket *socket, const hx_os_address *group,
	uint32_t interface_index, int join, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
