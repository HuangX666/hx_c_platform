#ifndef HX_C_PLATFORM_NET_EXTRA_H
#define HX_C_PLATFORM_NET_EXTRA_H

#include "hx_os_net_interfaces.h"

#ifdef __cplusplus
extern "C" {
#endif

HX_OS_API int HX_OS_CALL hx_os_address_is_ipv4_mapped(
	const hx_os_address *address);

/**
 * \brief Map IPv4 to IPv6, or restore a mapped IPv6 address to IPv4.
 *
 * \details Port is preserved; output is unchanged on failure. In-place use is
 * allowed. Map accepts IPv4 or an already mapped IPv6 address; unmap accepts
 * IPv4 or mapped IPv6 with scope zero. Other families are invalid.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_address_map_ipv4(
	const hx_os_address *address, hx_os_address *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_address_unmap_ipv4(
	const hx_os_address *address, hx_os_address *out, hx_os_error *error);

/**
 * \brief Compare family, address, port and IPv6 scope; invalid inputs differ.
 * \details normalize_mapped treats unscoped mapped IPv6 as equivalent IPv4.
 */
HX_OS_API int HX_OS_CALL hx_os_address_equal(const hx_os_address *left,
	const hx_os_address *right, int normalize_mapped);

/**
 * \brief Parse numeric IPv4:port or [IPv6%zone]:port, without DNS.
 *
 * \details Decimal port 0..65535 is required. Zone is an interface index or
 * native interface name, only on IPv6; URI percent escaping is not accepted.
 * Input is limited to 255 bytes. Formatting uses a numeric zone, includes
 * port, and preserves output on failure. No network object is required.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_endpoint_parse(
	const char *text, hx_os_address *out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_endpoint_format(
	const hx_os_address *address, char *buffer, size_t capacity,
	hx_os_error *error);

/**
 * \brief Set socket buffer requests; zero preserves that direction.
 * \details Send is updated before receive; failure may leave earlier changes.
 * Native limits apply. Get returns native actual capacity, which may be
 * rounded, doubled or clamped by the OS, not necessarily the requested value.
 * Both getters' output pointers are required and unchanged on failure.
 * Setters require exclusive access and fail BUSY after loop attachment.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_buffers(hx_os_socket *socket,
	uint32_t send_bytes, uint32_t receive_bytes, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_get_buffers(hx_os_socket *socket,
	uint32_t *send_bytes, uint32_t *receive_bytes, hx_os_error *error);

/**
 * \brief Bound unacknowledged/unsent TCP data in milliseconds where supported.
 * \details Linux/Android TCP_USER_TIMEOUT; zero restores the native default.
 * Other platforms return NOT_SUPPORTED. This does not time DNS or connect.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_tcp_user_timeout(
	hx_os_socket *socket, uint32_t milliseconds, hx_os_error *error);

/**
 * \brief Windows UDP: report/reset peer-unreachable ICMP errors, or ignore.
 * \details enabled=1 selects Winsock's reset reporting, zero disables it.
 * Other systems return NOT_SUPPORTED; their error policy is not fabricated.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_set_udp_icmp_errors(
	hx_os_socket *socket, int enabled, hx_os_error *error);

/**
 * \brief Join/leave a source-specific UDP multicast group.
 * \details group and unicast source must match the socket family. Ports are
 * ignored. Interface/scope, ownership and duplicate semantics match the
 * any-source membership API. No fallback to an unrestricted group is made.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_join_multicast_source(
	hx_os_socket *socket, const hx_os_address *group,
	const hx_os_address *source, uint32_t interface_index,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_socket_leave_multicast_source(
	hx_os_socket *socket, const hx_os_address *group,
	const hx_os_address *source, uint32_t interface_index,
	hx_os_error *error);

typedef struct hx_os_route_info {
	hx_os_address source;
	uint32_t interface_index;
} hx_os_route_info;

/**
 * \brief Query the selected local address and interface for a destination.
 *
 * \details No application datagram is sent. This is a point-in-time routing
 * decision, not proof of peer reachability. Unscoped mapped IPv6 destinations
 * use an IPv4 lookup. The source port is zero. Network
 * must remain alive during the call. Failure preserves output. POSIX uses a
 * temporary UDP connection and IP snapshot; if the selected address maps to
 * multiple interfaces it returns NOT_SUPPORTED rather than guessing. Query
 * again after network changes. Enumeration permissions and limits still apply.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_route_query(hx_os_network *network,
	const hx_os_address *destination, hx_os_route_info *out,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
