#ifndef HX_C_PLATFORM_DATAGRAM_H
#define HX_C_PLATFORM_DATAGRAM_H

#include "hx_os_net.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
	HX_OS_PACKET_LOCAL_ADDRESS = 1,
	HX_OS_PACKET_INTERFACE = 2,
	HX_OS_PACKET_TRUNCATED = 4,
	HX_OS_PACKET_CONTROL_TRUNCATED = 8
};

/**
 * \brief Received destination or requested outgoing source IP/interface.
 *
 * \details Zero initialization requests the native route on send. Flags
 * LOCAL_ADDRESS and INTERFACE select fields; receive sets only available
 * fields. local.port is zero on receive and must be zero on send: source
 * ports come from bind. Received IPv4 information may accompany an IPv4-
 * mapped IPv6 peer. A multicast/broadcast destination is not a valid local
 * source for replies; clear LOCAL_ADDRESS and retain INTERFACE in that case.
 * Sending ignores the two truncation flags. No socket-wide route is changed.
 */
typedef struct hx_os_packet_info {
	hx_os_address local;
	uint32_t interface_index;
	uint32_t flags;
} hx_os_packet_info;

/**
 * \brief Enable native per-packet destination/interface information.
 *
 * \details Call before receiving, excluding I/O and option changes. IPv6
 * sockets also enable IPv4 information for dual-stack reception. Updates
 * are sequential; failure can leave the earlier option changed. Missing
 * native support returns NOT_SUPPORTED with the native error when supplied.
 * This does not start threads or bind a socket. Requires NETWORK only.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_packet_info(
	hx_os_socket *socket, int enabled, hx_os_error *error);

/**
 * \brief Send one whole UDP datagram, optionally choosing its source route.
 *
 * \details peer is required; route may be NULL. These direct operations
 * require an unattached UDP socket; one sender and one receiver can run
 * concurrently, with each direction serialized. Inputs are borrowed for
 * the call. completed is required and is reset to zero before validation.
 * Zero bytes sends an empty datagram. At most INT_MAX bytes are accepted.
 * NOW tries once, then returns TIMEOUT if progress would block. _until uses
 * one absolute monotonic deadline, retries EINTR, and never splits a packet.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_packet(hx_os_socket *socket,
	const hx_os_address *peer, const hx_os_packet_info *route,
	const void *data, size_t bytes, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_packet_until(
	hx_os_socket *socket, const hx_os_address *peer,
	const hx_os_packet_info *route, const void *data, size_t bytes,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

/**
 * \brief Receive one UDP datagram and optional destination/interface data.
 *
 * \details peer/info are optional and reset to zero on entry. completed is
 * required and reset to zero. OK includes an empty datagram. LIMIT consumes
 * a truncated datagram: completed counts copied bytes and info identifies
 * payload/control truncation; the discarded remainder cannot be retried.
 * Other failures leave outputs empty; no data arrives after return. Enable
 * packet information first when needed; absent metadata is not fabricated.
 * local.port remains zero, avoiding a per-packet getsockname call. A zero-
 * capacity receive still consumes one packet and reports LIMIT if nonempty.
 * The same ownership and deadline rules as send_packet apply.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_packet(
	hx_os_socket *socket, hx_os_address *peer, hx_os_packet_info *info,
	void *data, size_t capacity, size_t *completed, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_packet_until(
	hx_os_socket *socket, hx_os_address *peer, hx_os_packet_info *info,
	void *data, size_t capacity, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_to_until(
	hx_os_socket *socket, const hx_os_address *peer, const void *data,
	size_t bytes, hx_os_deadline deadline, size_t *completed,
	hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_from_until(
	hx_os_socket *socket, hx_os_address *peer, void *data, size_t capacity,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

typedef struct hx_os_datagram_send {
	const void *data;
	size_t bytes;
	hx_os_address peer;
	hx_os_packet_info route;
} hx_os_datagram_send;

typedef struct hx_os_datagram_receive {
	void *data;
	size_t capacity;
	size_t bytes;
	hx_os_address peer;
	hx_os_packet_info info;
	hx_os_status status;
} hx_os_datagram_receive;

/**
 * \brief Send at most 64 complete datagrams using one waiting deadline.
 *
 * \details completed is required, reset to zero, and counts accepted packets.
 * Stop at the first error; retry only the suffix. Zero count is a no-op.
 * No atomic-batch guarantee, worker, allocation, or implicit retransmission.
 * Entries are validated when reached; earlier packets may already be sent.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_send_batch_until(
	hx_os_socket *socket, const hx_os_datagram_send *items, size_t count,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

/**
 * \brief Wait for one datagram, then drain currently available packets.
 *
 * \details At most 64 entries; only data/capacity are inputs. Entry outputs
 * start empty with status=WOULD_BLOCK. completed counts consumed datagrams,
 * including a final truncated packet. OK can return fewer than count.
 * Truncation stops the batch with LIMIT; that entry contains copied bytes
 * and flags. Other errors stop and retain earlier results. Do not retry a
 * consumed/truncated packet. Zero count is a no-op. Buffers are borrowed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_receive_batch_until(
	hx_os_socket *socket, hx_os_datagram_receive *items, size_t count,
	hx_os_deadline deadline, size_t *completed, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
