#ifndef HX_C_PLATFORM_NET_H
#define HX_C_PLATFORM_NET_H

#include "hx_os_base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_network hx_os_network;
typedef struct hx_os_socket hx_os_socket;
typedef struct hx_os_address_list hx_os_address_list;

enum { HX_OS_LOCAL = 1, HX_OS_IPV4 = 4, HX_OS_IPV6 = 6 };

enum { HX_OS_TCP = 1, HX_OS_UDP = 2, HX_OS_LOCAL_STREAM = 3 };

enum { HX_OS_WAIT_READ = 1, HX_OS_WAIT_WRITE = 2 };

enum { HX_OS_SHUT_READ = 1, HX_OS_SHUT_WRITE = 2, HX_OS_SHUT_BOTH = 3 };

typedef struct hx_os_address {
	uint8_t bytes[16];
	uint16_t port;
	uint16_t family;
	uint32_t scope_id;
} hx_os_address;

/**
 * \brief Network instances balance native initialization and own socket leases.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_network_create(
	hx_os_context *context, hx_os_network **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_network_destroy(
	hx_os_network **network, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_address_parse(const char *numeric_host,
	uint16_t port, hx_os_address *out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_address_format(
	const hx_os_address *address, char *buffer, size_t capacity,
	hx_os_error *error);

/**
 * \brief Blocking native DNS.
 *
 * \details Host is an ASCII/Punycode name; no implicit IDNA. Use loop_resolve
 * for deadline-aware asynchronous waiting. Results are capped at 256 distinct
 * addresses; exceeding the cap reports LIMIT.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_resolve(hx_os_network *network,
	const char *host, uint16_t port, int protocol, hx_os_address_list **out,
	hx_os_error *error);

HX_OS_API size_t HX_OS_CALL hx_os_address_list_count(
	const hx_os_address_list *list);

HX_OS_API const hx_os_address *HX_OS_CALL hx_os_address_list_get(
	const hx_os_address_list *list, size_t index);

HX_OS_API void HX_OS_CALL hx_os_address_list_destroy(hx_os_address_list *list);

/**
 * \brief Sockets are nonblocking and noninheritable.
 *
 * \details After connecting, one sending thread and one receiving thread may
 * operate concurrently; serialize each direction. Exclude
 * bind/connect/options/attachment/shutdown/close from I/O. After attachment,
 * the loop owns I/O scheduling and socket close. Do not mix direct I/O with
 * pending loop operations. Timeout on a connect wait leaves connection
 * establishment in progress; close to abandon it. After a failed or cancelled
 * asynchronous CONNECT, close the socket.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_create(hx_os_network *network,
	int family, int protocol, hx_os_socket **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_bind(
	hx_os_socket *socket, const hx_os_address *address, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_listen(
	hx_os_socket *socket, int backlog, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_local_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error);

/**
 * \brief Native escape hatch: level/name and value layout come from this
 * target's socket headers, not portable numeric constants.
 *
 * \details Get takes capacity in *bytes and returns native length only on
 * success; it is not a size query. Buffer contents on native failure are
 * unspecified. Set after loop attachment returns BUSY even when idle. Getters
 * require owner exclusion. Inputs are borrowed during the call. Native errors
 * are preserved.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_option_set(hx_os_socket *socket,
	int level, int name, const void *value, size_t bytes,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_option_get(hx_os_socket *socket,
	int level, int name, void *value, size_t *bytes, hx_os_error *error);

/**
 * \brief Portable common options; nonzero enables.
 *
 * \details Reuse is never implicit on bind.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_nodelay(
	hx_os_socket *socket, int enabled, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_keepalive(
	hx_os_socket *socket, int enabled, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_reuseaddr(
	hx_os_socket *socket, int enabled, hx_os_error *error);

/**
 * \brief Unconnected sockets report their native getpeername error; output is
 * kept.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_peer_address(
	hx_os_socket *socket, hx_os_address *address, hx_os_error *error);

/**
 * \brief Accept one connection without waiting.
 *
 * \returns WOULD_BLOCK for an empty backlog or a recoverable interrupted/
 * aborted connection; native details distinguish these cases. Retry after
 * readiness, subject to the caller's deadline. Listener/resource failures
 * remain errors. The loop's ACCEPT request retries recoverable failures
 * internally until it succeeds, is cancelled, or reaches its deadline.
 */
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

/**
 * \brief Stream read + readiness wait; returns some bytes, EOF, error or
 * timeout.
 *
 * \details Timeout keeps the socket usable and completed=0. No hidden buffer.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_until(
	hx_os_socket *socket, void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_until(hx_os_socket *socket,
	const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_to(hx_os_socket *socket,
	const hx_os_address *peer, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error);

/**
 * \brief LIMIT means a truncated datagram; its remainder is discarded, not
 * retried.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_from(
	hx_os_socket *socket, hx_os_address *peer, void *data, size_t bytes,
	size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_shutdown(
	hx_os_socket *socket, int direction, hx_os_error *error);

/**
 * \brief Close the socket and clear its owner pointer when consumed.
 *
 * \details Pending I/O returns BUSY. A Windows native close failure, including
 * WOULD_BLOCK with nonzero linger, preserves the pointer, network reference
 * and loop ownership for retry. POSIX close is attempted once and consumes
 * the descriptor even on error; do not retry a saved native descriptor.
 * A non-NULL owner still needs closing; a NULL owner has been consumed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_close(
	hx_os_socket **socket, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
