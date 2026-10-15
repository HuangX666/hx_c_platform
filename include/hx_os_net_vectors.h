#ifndef HX_C_PLATFORM_NET_VECTORS_H
#define HX_C_PLATFORM_NET_VECTORS_H

#include "hx_os_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_send_buffer {
	const void *data;
	size_t bytes;
} hx_os_send_buffer;

typedef struct hx_os_receive_buffer {
	void *data;
	size_t bytes;
} hx_os_receive_buffer;

/**
 * \brief Read/write up to 64 borrowed buffers as one stream operation.
 *
 * \details TCP and local stream sockets only, with no loop attachment.
 * One sender and one receiver can coexist; serialize each direction and
 * exclude close/options/shutdown. Buffers are visited in order without a
 * merged allocation. The total must fit size_t; a native call transfers at
 * most INT_MAX bytes. completed is required, reset to zero, and counts the
 * prefix across all buffers, not complete buffers. A zero total succeeds
 * without detecting EOF; a positive receive returning zero reports EOF.
 * Empty buffers are allowed. A NULL array requires count=0.
 * These calls do not create TCP message boundaries or bypass native caching.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_sendv_some(hx_os_socket *socket,
	const hx_os_send_buffer *buffers, size_t count, size_t *completed,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receivev_some(
	hx_os_socket *socket, const hx_os_receive_buffer *buffers, size_t count,
	size_t *completed, hx_os_error *error);

/**
 * \brief Wait using one absolute deadline: send all or receive some bytes.
 *
 * \details NOW makes one attempt. Send retains cumulative progress on later
 * timeout/error; resume only the remaining suffix. Receive succeeds as soon
 * as some bytes arrive, without filling every buffer. Waiting does not free
 * or close the socket, and no I/O outlives return. EINTR retries stay within
 * the same deadline. These synchronous waits occupy the calling thread.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_sendv_until(hx_os_socket *socket,
	const hx_os_send_buffer *buffers, size_t count, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receivev_until(
	hx_os_socket *socket, const hx_os_receive_buffer *buffers, size_t count,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
