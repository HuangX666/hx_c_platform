#ifndef HX_C_PLATFORM_IPC_H
#define HX_C_PLATFORM_IPC_H

#include "hx_os_base.h"
#include "hx_os_control.h"
#include "hx_string.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hx_ipc hx_ipc;
typedef uint64_t hx_ipc_peer;

enum {
	HX_IPC_DATA = 1,
	HX_IPC_REQUEST,
	HX_IPC_CONNECTED,
	HX_IPC_DISCONNECTED,
	HX_IPC_OVERFLOW
};

enum { HX_IPC_BINARY = 0, HX_IPC_UTF8 = 1 };

/**
 * \brief Library-owned immutable message.
 *
 * \details Release once; do not copy ownership. data is borrowed from this
 * object and valid until release. Messages may outlive the endpoint and may be
 * released on another thread.
 */
typedef struct hx_ipc_message {
	const void *data;
	size_t bytes;
	hx_ipc_peer peer;
	uint64_t channel;
	uint64_t request_id;
	hx_os_error error;
	int kind;
	int encoding;
} hx_ipc_message;

typedef struct hx_ipc_options {
	uint32_t size;
	uint32_t version;
	size_t max_peers;
	size_t max_messages;
	size_t max_pending_requests;
	size_t max_message_bytes;
	size_t max_pending_bytes;
	size_t max_kcp_bytes;
	uint32_t handshake_timeout_ms;
	uint32_t idle_timeout_ms;
	uint32_t reconnect_attempts;
	uint32_t reconnect_delay_ms;
} hx_ipc_options;

typedef struct hx_ipc_statistics {
	uint64_t received_messages;
	uint64_t rejected_messages;
	uint64_t discarded_datagrams;
	uint64_t lost_events;
	size_t pending_bytes;
	size_t kcp_bytes;
	size_t peers;
} hx_ipc_statistics;

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_options_init(
	hx_ipc_options *options, size_t capacity);

/**
 * \brief Common defaults: default context/options, five-second operation wait.
 *
 * \details Close always takes an explicit deadline. The _until and
 * _with_options variants expose timing, allocation and capacity control.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_listen(
	const char *address, hx_ipc **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_connect(
	const char *address, hx_ipc **out, hx_os_error *error);

/**
 * \brief Connect with bounded exponential reconnect delay and jitter.
 * \details NULL backoff uses 100 ms to 30 s, eight retries, 20% jitter, and
 * a per-endpoint random seed. Explicit seed=0 also selects that seed. The
 * retry budget resets after a successful handshake. Requests are never
 * replayed automatically. Other creation/timeout ownership rules match
 * connect_until. Existing connect functions retain their fixed-delay policy.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_connect_backoff_until(
	hx_os_context *context, const char *address,
	const hx_ipc_options *options, const hx_os_backoff_options *backoff,
	hx_os_deadline deadline, hx_ipc **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_send_text(
	hx_ipc *endpoint, const char *text, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_request_text(hx_ipc *endpoint,
	const char *text, hx_ipc_message **reply, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_reply_text(hx_ipc *endpoint,
	const hx_ipc_message *request, const char *text, hx_os_error *error);

/**
 * \brief Each endpoint owns one I/O thread, shared by its bounded peer set.
 *
 * \details Linking the library starts nothing. Business callbacks do not run on
 * that thread. Custom allocator callbacks can run there or on an application
 * caller. Supported addresses: tcp://127.0.0.1:port, udp://127.0.0.1:port,
 * kcp://127.0.0.1:port (also [::1]), local://simple-name, and POSIX-only
 * unix:///absolute/path. Port 0 is allowed for listeners; address() reports the
 * actual endpoint. IP endpoints reject non-loopback addresses. local:// uses a
 * Windows named pipe or a per-user Unix socket directory. Raw UDP remains
 * available separately through hx_os_socket_*.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_listen_with_options(
	hx_os_context *context, const char *address,
	const hx_ipc_options *options, hx_ipc **out, hx_os_error *error);

/**
 * \brief Once the endpoint is created, *out remains owned even on a handshake
 * error or TIMEOUT: wait_connected_until() or close it.
 *
 * \details Earlier creation failures leave *out NULL. Each reconnect
 * establishes a fresh session; old pending calls fail and are never
 * automatically replayed.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_connect_until(hx_os_context *context,
	const char *address, const hx_ipc_options *options,
	hx_os_deadline deadline, hx_ipc **out, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_wait_connected_until(hx_ipc *endpoint,
	hx_os_deadline deadline, hx_ipc_peer *peer, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_address(
	hx_ipc *endpoint, hx_string *out, hx_os_error *error);

/**
 * \brief Thread-safe, copying sends.
 *
 * \details Reliable transports wait for acknowledgement that the complete
 * message entered the peer's bounded inbox, not that its business action
 * executed. Plain UDP completes after the local datagram send and has no
 * delivery/ordering/deduplication guarantee. A timeout can have an unknown
 * remote outcome; it never invalidates caller storage. peer=0 chooses the
 * connected peer on a client endpoint only.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_send_until(hx_ipc *endpoint,
	hx_ipc_peer peer, uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_send_text_until(hx_ipc *endpoint,
	hx_ipc_peer peer, uint64_t channel, const char *text,
	hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief A request waits for its matching reply while both directions keep
 * moving.
 *
 * \details Concurrent requests may complete in any order. Replies bypass the
 * ordinary inbox but still obey the byte budget. Late replies are discarded
 * after a request deadline. Use finite deadlines for independently implemented
 * peers.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_request_until(hx_ipc *endpoint,
	hx_ipc_peer peer, uint64_t channel, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_ipc_message **reply, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_request_text_until(hx_ipc *endpoint,
	hx_ipc_peer peer, uint64_t channel, const char *text,
	hx_os_deadline deadline, hx_ipc_message **reply, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_reply_text_until(hx_ipc *endpoint,
	const hx_ipc_message *request, const char *text,
	hx_os_deadline deadline, hx_os_error *error);

HX_OS_API hx_os_status HX_OS_CALL hx_ipc_reply_until(hx_ipc *endpoint,
	const hx_ipc_message *request, const void *data, size_t bytes,
	hx_os_deadline deadline, hx_os_error *error);

/**
 * \brief One complete message or event per call.
 *
 * \details NOW is a nonblocking poll. Multiple receivers distribute messages,
 * they do not broadcast copies. Full reliable inboxes explicitly reject
 * admission; UDP may drop. A bounded event ring reports overflow instead of
 * silently promising all lifecycle events. Empty messages are valid.
 * Destruction wakes all waiting callers. Event allocation failure retains the
 * event for retry. Allocator callbacks run outside the endpoint mutex;
 * queues/closure are rechecked afterward.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_receive_until(hx_ipc *endpoint,
	hx_os_deadline deadline, hx_ipc_message **message, hx_os_error *error);

HX_OS_API void HX_OS_CALL hx_ipc_message_release(hx_ipc_message *message);

/**
 * \brief Allocators may query statistics of a published, live endpoint.
 *
 * \details They must not close it or make blocking IPC calls: they can run
 * inside an active call or on its I/O worker. Creation/disposal callbacks
 * cannot use the endpoint.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_statistics_read(hx_ipc *endpoint,
	hx_ipc_statistics *out, size_t capacity, hx_os_error *error);

/**
 * \brief Serial owner close; prevent new calls from starting once close begins.
 *
 * \details Already-running calls are cancelled and may finish concurrently.
 * Timeout retains *endpoint for retry, including native I/O buffers. No thread
 * is killed or detached. Close never waits for the peer's business code.
 */
HX_OS_API hx_os_status HX_OS_CALL hx_ipc_close_until(
	hx_ipc **endpoint, hx_os_deadline deadline, hx_os_error *error);

#ifdef __cplusplus
}
#endif
#endif
