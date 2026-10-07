#ifndef HX_C_PLATFORM_TLS_H
#define HX_C_PLATFORM_TLS_H

#include "hx_os_net.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Transport Layer Security. Thread-local storage remains hx_os_tls_key. */
typedef struct hx_os_tls hx_os_tls;

enum {
	HX_OS_TLS_REUSEADDR = 1,
	HX_OS_TLS_NODELAY = 2,
	HX_OS_TLS_REQUIRE_CLIENT_CERTIFICATE = 4
};

typedef struct hx_os_tls_options {
	uint32_t size, version;
	const char *ca_file;
	const char *certificate_file;
	const char *private_key_file;
	const char *key_password;
	const char *server_name;
	size_t queue_capacity;
	uint32_t max_peers;
	uint32_t handshake_timeout_ms;
	uint32_t flags;
} hx_os_tls_options;

#define HX_OS_TLS_OPTIONS_INIT                                                 \
	{                                                                      \
		sizeof(hx_os_tls_options), 1, NULL, NULL, NULL, NULL, NULL,    \
			65536, 16, 5000, 0                                     \
	}

HX_OS_API int HX_OS_CALL hx_os_tls_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_options_init(
	hx_os_tls_options *options, size_t capacity);

/* TLS 1.2/1.3 over TCP, certificate and hostname/IP verification on clients.
 * No insecure/anonymous fallback, DTLS, QUIC, renegotiation or early data.
 * A supplied CA PEM replaces system roots. Server certificate/key PEM files
 * are required; a client may provide both for mutual authentication.
 * Paths/password use UTF-8; host/server_name use ASCII/Punycode (no IDNA).
 * server_name overrides the verification/SNI name, not the connect address.
 * Options and strings are copied; files must remain stable through startup.
 * The output slot must not already own a live endpoint.
 * Certificate failures retain the native X.509 result in error.cause_code.
 * A server accepts numeric IPv4/IPv6; port=0 selects an ephemeral port.
 * Reuse-address is enabled only by the explicit flag.
 *
 * Each explicitly created endpoint owns one I/O thread and a bounded loop.
 * A server shares that thread across max_peers connections; a client has one
 * peer. queue_capacity bounds each peer's plaintext receive/send queues.
 * Additional fixed record buffers and native OpenSSL allocations are extra.
 * No threads start merely because this module is enabled.
 *
 * start_* returns once the owned worker is created. Startup/file/DNS/crypto
 * errors arrive through wait_ready/accept/I/O. connect/listen_until also wait.
 * Once startup has been accepted, *out remains owned even on wait failure or
 * timeout; close/abort it explicitly. Timeout never frees live worker state.
 * Native DNS and certificate work are not forcibly interruptible.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_start_connect(
	hx_os_context *context, const char *host, uint16_t port,
	const hx_os_tls_options *options, hx_os_tls **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_start_listen(hx_os_context *context,
	const char *numeric_host, uint16_t port,
	const hx_os_tls_options *options, hx_os_tls **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_connect_until(
	hx_os_context *context, const char *host, uint16_t port,
	const hx_os_tls_options *options, hx_os_deadline deadline,
	hx_os_tls **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_listen_until(hx_os_context *context,
	const char *numeric_host, uint16_t port,
	const hx_os_tls_options *options, hx_os_deadline deadline,
	hx_os_tls **out, hx_os_error *error);

/* Five-second waiting shortcuts; the same retained-owner rule applies. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_connect(const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_listen(const char *numeric_host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error);

/* All deadlines use the library's monotonic domain. NOW polls; INFINITE is
 * explicit. Native calls/scheduling may exceed a deadline. Ready means a
 * verified client connection or a prepared server listener. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_wait_ready_until(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_local_address(
	hx_os_tls *tls, hx_os_address *out, hx_os_error *error);

/* Servers accept only completed handshakes. Each accepted peer is announced
 * once and remains addressable until peer_close/abort or endpoint shutdown.
 * IDs are local opaque identifiers, never reused on one endpoint. They are
 * not on-wire device/request IDs. Clients use peer=0 for their sole peer.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_accept_until(hx_os_tls *tls,
	hx_os_deadline deadline, uint64_t *peer, hx_os_error *error);

/* One receiver and a simultaneous sender are supported per peer. A second
 * receiver returns BUSY. Senders serialize whole calls to avoid interleaving
 * their byte spans; waiting for admission shares the supplied deadline.
 * Input/output are borrowed only during the call. Internal queued data is
 * owned, bounded and independent of caller buffers after return.
 *
 * send success includes handing encrypted output to the OS, not peer/app
 * acknowledgement. *accepted counts plaintext copied into the owned queue,
 * including on timeout/cancel/failure. Accepted bytes may still be sent after
 * a timeout; retry only the unaccepted suffix. flush waits for data accepted
 * at its entry; subsequent sends do not extend that snapshot.
 *
 * receive returns some verified plaintext, EOF only for close_notify, or an
 * error. Abrupt TCP EOF is TRUNCATED_INPUT. Buffered valid bytes precede the
 * terminal result. TLS remains a byte stream: use application framing for
 * JSON/messages. receive_exact preserves partial bytes/count on failure;
 * premature orderly EOF becomes TRUNCATED_INPUT. No implicit text conversion.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_send_until(hx_os_tls *tls,
	uint64_t peer, const void *data, size_t bytes, hx_os_deadline deadline,
	size_t *accepted, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_flush_until(hx_os_tls *tls,
	uint64_t peer, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_receive_until(hx_os_tls *tls,
	uint64_t peer, void *data, size_t capacity, hx_os_deadline deadline,
	size_t *received, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_receive_exact_until(hx_os_tls *tls,
	uint64_t peer, void *data, size_t bytes, hx_os_deadline deadline,
	size_t *received, hx_os_error *error);

/* Cancel currently waiting application calls; queued accepted data remains
 * owned. New calls can continue. This does not interrupt native crypto/DNS. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_cancel_waits(
	hx_os_tls *tls, hx_os_error *error);

/* Closing stops admission, drains accepted sends and exchanges close_notify.
 * Unread receive data is discarded by an explicit close. Timeout preserves
 * the object and progress for retry; it does not silently switch to abort.
 * abort explicitly abandons buffered data and cancels native requests, but
 * still waits for their retirement. No thread is killed or detached.
 *
 * Stop new application calls before endpoint destruction. In-flight calls
 * wake and retire; one close caller at a time. Native errors can be returned
 * with *tls=NULL when retirement has completed; a live owner means retry is
 * required. Allocator user state/runtime code must survive that owner.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_peer_close_until(hx_os_tls *tls,
	uint64_t peer, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_peer_abort_until(hx_os_tls *tls,
	uint64_t peer, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_close_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_abort_until(
	hx_os_tls **tls, hx_os_deadline deadline, hx_os_error *error);

enum { HX_OS_TLS_PEER_CERTIFICATE = 1, HX_OS_TLS_PEER_VERIFIED = 2 };

typedef struct hx_os_tls_peer_info {
	uint32_t size, version;
	uint64_t id;
	hx_os_address address;
	unsigned char certificate_sha256[32];
	char protocol[16];
	char cipher[64];
	uint32_t flags;
} hx_os_tls_peer_info;

/* Copies a snapshot into caller storage; small capacity returns OUTPUT_FULL
 * without changing output. No OpenSSL/native socket pointer is exposed. */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_get_peer_info(hx_os_tls *tls,
	uint64_t peer, hx_os_tls_peer_info *out, size_t capacity,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
