#ifndef HX_C_PLATFORM_NET_H
#define HX_C_PLATFORM_NET_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_network hx_os_network;
typedef struct hx_os_socket hx_os_socket;
typedef struct hx_os_address_list hx_os_address_list;

enum { HX_OS_IPV4 = 4, HX_OS_IPV6 = 6 };
enum { HX_OS_TCP = 1, HX_OS_UDP = 2 };
enum { HX_OS_WAIT_READ = 1, HX_OS_WAIT_WRITE = 2 };
enum { HX_OS_SHUT_READ = 1, HX_OS_SHUT_WRITE = 2, HX_OS_SHUT_BOTH = 3 };

typedef struct hx_os_address {
	uint8_t bytes[16];
	uint16_t port;
	uint16_t family;
	uint32_t scope_id;
} hx_os_address;

/* Network instances balance native initialization and own socket leases. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_network_create(
	hx_os_context *context, hx_os_network **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_network_destroy(
	hx_os_network **network, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_address_parse(const char *numeric_host,
	uint16_t port, hx_os_address *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_address_format(
	const hx_os_address *address, char *buffer, size_t capacity,
	hx_os_error *error);
/*
 * Blocking native DNS. Host is an ASCII/Punycode name; no implicit IDNA.
 * Use loop_resolve for deadline-aware asynchronous waiting. Results are
 * capped at 256 distinct addresses; exceeding the cap reports LIMIT.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolve(hx_os_network *network,
	const char *host, uint16_t port, int protocol, hx_os_address_list **out,
	hx_os_error *error);
HX_OS_API size_t HX_OS_CALL hx_os_address_list_count(
	const hx_os_address_list *list);
HX_OS_API const hx_os_address *HX_OS_CALL hx_os_address_list_get(
	const hx_os_address_list *list, size_t index);
HX_OS_API void HX_OS_CALL hx_os_address_list_destroy(hx_os_address_list *list);

/*
 * Sockets are nonblocking and noninheritable. Serialize operations per socket.
 * After attachment, the loop owns I/O scheduling and socket close. Do not
 * mix direct I/O with pending loop operations. Timeout on a connect wait
 * leaves connection establishment in progress; close to abandon it.
 * After a failed or cancelled asynchronous CONNECT, close the socket.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_create(hx_os_network *network,
	int family, int protocol, hx_os_socket **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_bind(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_listen(
	hx_os_socket *socket, int backlog, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_local_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_accept(hx_os_socket *listener,
	hx_os_socket **out, hx_os_address *peer, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_connect(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_connect_until(
	hx_os_socket *socket, const hx_os_address *address,
	hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_wait_until(hx_os_socket *socket,
	int events, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_some(hx_os_socket *socket,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_some(
	hx_os_socket *socket, void *data, size_t bytes, size_t *completed,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_until(hx_os_socket *socket,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_to(hx_os_socket *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error);
/* LIMIT means a truncated datagram; its remainder is discarded, not retried. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_from(
	hx_os_socket *socket, hx_os_address *peer, void *data, size_t bytes,
	size_t *completed, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_shutdown(
	hx_os_socket *socket, int direction, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_close(
	hx_os_socket **socket, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
