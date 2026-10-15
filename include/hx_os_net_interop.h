#ifndef HX_C_PLATFORM_NET_INTEROP_H
#define HX_C_PLATFORM_NET_INTEROP_H

#include "hx_os_net.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { HX_OS_SOCKET_TAKE = 1, HX_OS_SOCKET_DUP = 2 };

/**
 * \brief Import an existing nonblocking IP TCP/UDP socket.
 * \details TAKE consumes *native_owner only on success, setting UINTPTR_MAX.
 * DUP preserves it; both handles share endpoint/options and POSIX status
 * flags. The caller must own/exclude all I/O, pending requests, registrations
 * and close. Windows additionally requires an OVERLAPPED-capable socket and
 * caller-guaranteed nonblocking mode; Winsock has no read-only mode query.
 * Existing IOCP associations cannot be transferred. The imported handle is
 * required to have SO_NOSIGPIPE set on targets lacking MSG_NOSIGNAL.
 * The resulting handle is noninheritable. No blocking mode, linger or
 * connection state is changed.
 * The network object must outlive the returned owner. Failure leaves out NULL.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_import(hx_os_network *network,
	uintptr_t *native_owner, int mode, hx_os_socket **out,
	hx_os_error *error);

/**
 * \brief Send FIN, then explicitly discard incoming bytes until peer EOF.
 * \details Does not close the socket or prove application acknowledgement.
 * discarded counts bytes consumed by this call. TIMEOUT retains the socket
 * in half-closed state; retry with the same owner or close it. Do not send
 * afterwards. Exclude other I/O, options and close; attached sockets are BUSY.
 * A peer that keeps sending can exhaust the deadline. No unbounded buffering.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_shutdown_drain_until(
	hx_os_socket *socket, hx_os_deadline deadline, uint64_t *discarded,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
