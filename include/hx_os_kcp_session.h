#ifndef HX_C_PLATFORM_KCP_SESSION_H
#define HX_C_PLATFORM_KCP_SESSION_H

#include "hx_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_os_kcp_session hx_os_kcp_session;
typedef hx_ipc_options hx_os_kcp_session_options;
typedef hx_ipc_message hx_os_kcp_message;

enum {
	HX_OS_KCP_DATA = HX_IPC_DATA,
	HX_OS_KCP_REQUEST = HX_IPC_REQUEST,
	HX_OS_KCP_CONNECTED = HX_IPC_CONNECTED,
	HX_OS_KCP_DISCONNECTED = HX_IPC_DISCONNECTED,
	HX_OS_KCP_OVERFLOW = HX_IPC_OVERFLOW
};

HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_options_init(
	hx_os_kcp_session_options *options, size_t capacity);

/**
 * \brief Bounded reliable KCP messages for trusted networks; no encryption.
 * \details Endpoint is numeric IP:port or [IPv6%scope]:port, with an optional
 * kcp:// scheme. Listen permits port zero. NULL uses bounded defaults;
 * use kcp_session_options_init to customize. One created I/O thread per
 * endpoint handles all peers, protocol timers and complete message delivery.
 * The HXIP session envelope and KCP message mode match docs/IPC.md section 8;
 * another language must implement both, not just feed raw KCP packets.
 * No authentication or spoofing protection: use only a trusted network.
 * Existing IPC factories remain loopback-only. Close takes a caller deadline.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_listen(const char *endpoint,
	const hx_os_kcp_session_options *options, hx_os_kcp_session **out,
	hx_os_error *error);

/**
 * \brief Connection timeout retains a non-NULL session for wait/close retry.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_connect_until(
	const char *endpoint, const hx_os_kcp_session_options *options,
	hx_os_deadline deadline, hx_os_kcp_session **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_wait_until(
	hx_os_kcp_session *session, hx_os_deadline deadline, uint64_t *peer,
	hx_os_error *error);
/**
 * \brief Copy the listener's bound URI or the client's configured peer URI.
 * \details The kcp:// URI can be passed back to connect_until. Replace a
 * wildcard listener address with a reachable interface address first.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_address(
	hx_os_kcp_session *session, hx_string *out, hx_os_error *error);

/**
 * \brief Send a copied message; peer=0 selects the client's connected peer.
 * \details Success means peer inbox admission, not business completion.
 * Same-session sends are ordered; concurrent callers' ordering is unspecified.
 * Reconnect creates a fresh session; unfinished operations are never replayed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_send_until(
	hx_os_kcp_session *session, uint64_t peer, uint64_t channel,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_receive_until(
	hx_os_kcp_session *session, hx_os_deadline deadline,
	hx_os_kcp_message **message, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_request_until(
	hx_os_kcp_session *session, uint64_t peer, uint64_t channel,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_kcp_message **reply, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_reply_until(
	hx_os_kcp_session *session, const hx_os_kcp_message *request,
	const void *data, size_t bytes, hx_os_deadline deadline,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_kcp_message_release(hx_os_kcp_message *message);
HX_OS_API hx_os_status HX_OS_CALL hx_os_kcp_session_close_until(
	hx_os_kcp_session **session, hx_os_deadline deadline,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
