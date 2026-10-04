#ifndef HX_C_PLATFORM_LOCAL_H
#define HX_C_PLATFORM_LOCAL_H

#include "hx_os_net.h"
#include "hx_os_file.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Pathname stream sockets only; no abstract names, credentials or FD passing.
 */
typedef struct hx_os_local_address {
	char path[108];
} hx_os_local_address;

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_address_init(
	hx_os_local_address *out, const char *path, hx_os_error *error);

/* LOCAL_SOCKETS + NETWORK. Windows requires a native AF_UNIX provider
 * (Windows 10 1803+, not Windows 7). macOS has a shorter native path limit.
 * UTF-8 paths include a NUL within 108 bytes; never silently truncate.
 * Serialize operations and exclude concurrent close. Bind borrows its
 * address during the call; connect copies the path for finish_connect.
 * A retry must use the same path. Fatal connection failure requires close.
 * The capability bit describes the compiled adapter; native creation may
 * still return NOT_SUPPORTED when the OS/provider is unavailable.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_create(
	hx_os_network *network, hx_os_socket **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_bind(hx_os_socket *socket,
	const hx_os_local_address *address, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_listen(
	hx_os_socket *socket, int backlog, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_connect(
	hx_os_socket *socket, const hx_os_local_address *address,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_connect_until(
	hx_os_socket *socket, const hx_os_local_address *address,
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_finish_connect(
	hx_os_socket *socket, hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_accept_until(
	hx_os_socket *listener, hx_os_deadline deadline, hx_os_socket **out,
	hx_os_error *error);

/* Empty result is an unnamed peer. Output preserved on failure. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_socket_address(
	hx_os_socket *socket, int peer, hx_os_local_address *out,
	hx_os_error *error);

/*
 * Common actions combine create + bind/listen or create + connect.
 * bind never removes an existing path. Close never removes the pathname:
 * the caller controls its directory and removes its own name after use.
 * A failed listen after successful bind can leave the new pathname.
 * connect TIMEOUT retains *out; retry finish_connect or close it. Other
 * connect errors close the new socket and leave *out=NULL.
 * Send/receive/wait/shutdown/close reuse the ordinary socket API.
 * Existing loop READ/WRITE accept connected local sockets; loop CONNECT/
 * ACCEPT are NOT_SUPPORTED (the old IP completion/address has no pathname).
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_local_listen(hx_os_network *network,
	const char *path, int backlog, hx_os_socket **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_local_connect(hx_os_network *network,
	const char *path, hx_os_deadline deadline, hx_os_socket **out,
	hx_os_error *error);

/* Blocking full-duplex byte streams, owned file endpoints. Independent of
 * NETWORK and LOOP. POSIX uses socketpair; Windows uses a private TCP
 * loopback connection and balances its own Winsock references.
 * Default creation deadline is 5 seconds; _until exposes that policy.
 * Output endpoints are NULL on any creation failure, including timeout.
 * File read/write/close contracts apply; seeking/sync/mapping are unsupported.
 * There is no buffering/durability guarantee. Use shutdown to send EOF
 * without losing the opposite direction. Exclude close and same-direction
 * simultaneous I/O; one reader and one writer may run concurrently.
 * Windows socket-backed endpoints cannot be child standard handles.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_pair_socket(hx_os_context *context,
	hx_os_file **first, hx_os_file **second, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_pair_socket_until(
	hx_os_context *context, hx_os_deadline deadline, hx_os_file **first,
	hx_os_file **second, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_pipe_socket_shutdown(
	hx_os_file *file, int direction, hx_os_error *error);
#ifdef __cplusplus
}
#endif
#endif
