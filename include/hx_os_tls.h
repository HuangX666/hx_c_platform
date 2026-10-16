#ifndef HX_C_PLATFORM_TLS_H
#define HX_C_PLATFORM_TLS_H

#include "hx_os_net.h"
#include "hx_os_dial.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Transport Layer Security.
 *
 * \details Thread-local storage remains hx_os_tls_key.
 */
typedef struct hx_os_tls hx_os_tls;
typedef struct hx_os_tls_profile hx_os_tls_profile;

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

enum {
	HX_OS_TLS_CRL_NONE = 0,
	HX_OS_TLS_CRL_LEAF = 1,
	HX_OS_TLS_CRL_CHAIN = 2
};

typedef struct hx_os_tls_options_ex {
	uint32_t size, version;
	hx_os_tls_options base;
	hx_os_tcp_options socket;
	const char *const *alpn;
	size_t alpn_count;
	const hx_os_resolver_options *resolver;
	const unsigned char *certificate_sha256;
	const char *crl_file;
	uint32_t attempt_delay_ms;
	uint32_t crl_mode;
	int require_alpn;
	int enable_session_tickets;
} hx_os_tls_options_ex;

#define HX_OS_TLS_OPTIONS_EX_INIT                                              \
	{                                                                      \
		sizeof(hx_os_tls_options_ex), 1, HX_OS_TLS_OPTIONS_INIT,       \
			HX_OS_TCP_OPTIONS_INIT, NULL, 0, NULL, NULL, NULL,     \
			250, 0, 0, 0                                           \
	}

/**
 * \brief Extended options leave the original structure and ABI unchanged.
 *
 * \details All strings, protocol names and the optional 32-byte certificate
 * SHA-256 pin are copied before start returns. TCP settings override matching
 * base flags; listener dual-stack is configured before bind. Client connection
 * attempts alternate families with at most two pending sockets, using one
 * connection/handshake budget. resolver=NULL preserves system DNS; a supplied
 * configuration creates a private c-ares resolver on the client I/O worker,
 * sharing its deadline and close cancellation. Configuration/hosts file reads
 * are not hard real-time. Server resolver settings are invalid, not ignored.
 *
 * ALPN accepts up to 16 nonempty printable ASCII names, each at most 255 bytes.
 * Servers choose their first supported name; require_alpn rejects missing or
 * unmatched negotiation. Selected protocol is available through get_alpn.
 *
 * A pin is an ADDITIONAL leaf-certificate check, never a replacement for chain
 * or hostname validation. A server pin requires client-certificate validation.
 * crl_file is a stable UTF-8 path to a local PEM CRL bundle (at most 4 MiB).
 * LEAF checks the peer; CHAIN also checks its chain. Required missing, expired
 * or invalid CRLs fail verification. No network OCSP/AIA/CRL fetch occurs.
 * Server CRL checks likewise require client-certificate validation. NONE must
 * have no CRL file. Pin rotation and refreshing CRLs require a new endpoint.
 * enable_session_tickets is server-only, explicitly enabling stateless
 * tickets on that endpoint. No early data or internal session cache is enabled.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_options_ex_init(
	hx_os_tls_options_ex *options, size_t capacity);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_start_connect_ex(
	hx_os_context *context, const char *host, uint16_t port,
	const hx_os_tls_options_ex *options, hx_os_tls **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_start_listen_ex(
	hx_os_context *context, const char *numeric_host, uint16_t port,
	const hx_os_tls_options_ex *options, hx_os_tls **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_connect_ex_until(
	hx_os_context *context, const char *host, uint16_t port,
	const hx_os_tls_options_ex *options, hx_os_deadline deadline,
	hx_os_tls **out, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_listen_ex_until(
	hx_os_context *context, const char *numeric_host, uint16_t port,
	const hx_os_tls_options_ex *options, hx_os_deadline deadline,
	hx_os_tls **out, hx_os_error *error);

/**
 * \brief Immutable client trust/configuration snapshot for one host and port.
 * \details Explicit creation loads credentials; it starts no network thread.
 * Each profile caches at most one resumable session, consumed on reuse. Server
 * tickets may arrive only after application I/O; resumption is not guaranteed.
 * No early data is sent. Connections retain their profile. Changing trust,
 * pins, ALPN or CRLs requires a new profile; old files are not reread.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_profile_create(
	hx_os_context *context, const char *host, uint16_t port,
	const hx_os_tls_options_ex *options, hx_os_tls_profile **out,
	hx_os_error *error);
HX_OS_API void HX_OS_CALL hx_os_tls_profile_retain(hx_os_tls_profile *profile);
HX_OS_API void HX_OS_CALL hx_os_tls_profile_release(hx_os_tls_profile *profile);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_profile_connect_until(
	hx_os_tls_profile *profile, hx_os_deadline deadline, hx_os_tls **out,
	hx_os_error *error);

/**
 * \brief Copy the negotiated protocol; NOT_FOUND means none was negotiated.
 *
 * \details No borrowed OpenSSL data escapes. Output is unchanged on failure.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_get_alpn(
	hx_os_tls *tls, uint64_t peer, hx_string *out, hx_os_error *error);

HX_OS_API int HX_OS_CALL hx_os_tls_available(void);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_options_init(
	hx_os_tls_options *options, size_t capacity);

/**
 * \brief TLS 1.2/1.3 over TCP, certificate and hostname/IP verification on
 * clients.
 *
 * \details No insecure/anonymous fallback, DTLS, QUIC, renegotiation or early
 * data. A supplied CA PEM replaces system roots. Server certificate/key PEM
 * files are required; a client may provide both for mutual authentication.
 * Paths/password use UTF-8; host/server_name use ASCII/Punycode (no IDNA).
 * server_name overrides the verification/SNI name, not the connect address.
 * Options and strings are copied; files must remain stable through startup. The
 * output slot must not already own a live endpoint. Certificate failures retain
 * the native X.509 result in error.cause_code. A server accepts numeric
 * IPv4/IPv6; port=0 selects an ephemeral port. Reuse-address is enabled only by
 * the explicit flag.
 *
 * Client APIs share the bounded TCP dialer: alternate IPv4/IPv6, at most two
 * candidates, 250 ms between starts by default. Stalled candidates yield their
 * slots while untried addresses remain; see hx_os_dial.h. Original APIs keep
 * system DNS and start handshake_timeout_ms after resolution, shared by TCP
 * attempts and TLS handshake. Extended APIs include DNS in that budget.
 * A connect_until/wait_ready deadline limits that wait, not worker lifetime.
 *
 * Each explicitly created endpoint owns one I/O thread and a bounded loop. A
 * server shares that thread across max_peers connections; a client has one
 * peer. queue_capacity bounds each peer's plaintext receive/send queues.
 * Additional fixed record buffers and native OpenSSL allocations are extra. No
 * threads start merely because this module is enabled.
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

/**
 * \brief Five-second waiting shortcuts; the same retained-owner rule applies.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_connect(const char *host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_listen(const char *numeric_host,
	uint16_t port, const hx_os_tls_options *options, hx_os_tls **out,
	hx_os_error *error);

/**
 * \brief All deadlines use the library's monotonic domain.
 *
 * \details NOW polls; INFINITE is explicit. Native calls/scheduling may exceed
 * a deadline. Ready means a verified client connection or a prepared server
 * listener.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_wait_ready_until(
	hx_os_tls *tls, hx_os_deadline deadline, hx_os_error *error);
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_local_address(
	hx_os_tls *tls, hx_os_address *out, hx_os_error *error);

/**
 * \brief Servers accept only completed handshakes.
 *
 * \details Each accepted peer is announced once and remains addressable until
 * peer_close/abort or endpoint shutdown. IDs are local opaque identifiers,
 * never reused on one endpoint. They are not on-wire device/request IDs.
 * Clients use peer=0 for their sole peer.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_accept_until(hx_os_tls *tls,
	hx_os_deadline deadline, uint64_t *peer, hx_os_error *error);

/**
 * \brief One receiver and a simultaneous sender are supported per peer.
 *
 * \details A second receiver returns BUSY. Senders serialize whole calls to
 * avoid interleaving their byte spans; waiting for admission shares the
 * supplied deadline. Input/output are borrowed only during the call. Internal
 * queued data is owned, bounded and independent of caller buffers after return.
 *
 * send success includes handing encrypted output to the OS, not peer/app
 * acknowledgement. *accepted counts plaintext copied into the owned queue,
 * including on timeout/cancel/failure. Accepted bytes may still be sent after a
 * timeout; retry only the unaccepted suffix. flush waits for data accepted at
 * its entry; subsequent sends do not extend that snapshot.
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

/**
 * \brief Cancel currently waiting application calls; queued accepted data
 * remains owned.
 *
 * \details New calls can continue. This does not interrupt native crypto/DNS.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_cancel_waits(
	hx_os_tls *tls, hx_os_error *error);

/**
 * \brief Closing stops admission, drains accepted sends and exchanges
 * close_notify.
 *
 * \details Unread receive data is discarded by an explicit close. Timeout
 * preserves the object and progress for retry; it does not silently switch to
 * abort. abort explicitly abandons buffered data and cancels native requests,
 * but still waits for their retirement. No thread is killed or detached.
 *
 * Stop new application calls before endpoint destruction. In-flight calls wake
 * and retire; one close caller at a time. Native errors can be returned with
 * *tls=NULL when retirement has completed; a live owner means retry is
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

enum {
	HX_OS_TLS_PEER_CERTIFICATE = 1,
	HX_OS_TLS_PEER_VERIFIED = 2,
	HX_OS_TLS_PEER_RESUMED = 4
};

typedef struct hx_os_tls_peer_info {
	uint32_t size, version;
	uint64_t id;
	hx_os_address address;
	unsigned char certificate_sha256[32];
	char protocol[16];
	char cipher[64];
	uint32_t flags;
} hx_os_tls_peer_info;

/**
 * \brief Copies a snapshot into caller storage; small capacity returns
 * OUTPUT_FULL without changing output.
 *
 * \details No OpenSSL/native socket pointer is exposed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_os_tls_get_peer_info(hx_os_tls *tls,
	uint64_t peer, hx_os_tls_peer_info *out, size_t capacity,
	hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
