#ifndef HX_C_PLATFORM_IPC_PRIVATE_H
#define HX_C_PLATFORM_IPC_PRIVATE_H

#include "hx_ipc.h"
#include "hx_os_kcp.h"
#include "hx_os_util.h"
#include "loop_private.h"
#include "hx_os_loop_fd.h"

enum { HX_IPC_I_HEADER = 64, HX_IPC_I_PACKET = 65536 };

enum { HX_IPC_I_TCP, HX_IPC_I_UDP, HX_IPC_I_KCP, HX_IPC_I_LOCAL };

enum {
	HX_IPC_I_HELLO = 1,
	HX_IPC_I_WELCOME,
	HX_IPC_I_DATA,
	HX_IPC_I_REQUEST,
	HX_IPC_I_REPLY,
	HX_IPC_I_ACK,
	HX_IPC_I_PING,
	HX_IPC_I_PONG,
	HX_IPC_I_CLOSE,
	HX_IPC_I_KCP_PACKET
};

typedef struct hx_ipc_frame hx_ipc_frame;
typedef struct hx_ipc_connection hx_ipc_connection;
typedef struct hx_ipc_waiter hx_ipc_waiter;
typedef struct hx_ipc_received hx_ipc_received;

struct hx_ipc_received {
	hx_ipc_message message;
	hx_ipc_received *next;
	unsigned char payload[];
};

typedef struct hx_ipc_header {
	uint64_t source;
	uint64_t destination;
	uint64_t id;
	uint64_t channel;
	uint64_t auxiliary;
	uint32_t bytes;
	hx_os_status status;
	int kind;
	int encoding;
} hx_ipc_header;

struct hx_ipc_frame {
	hx_ipc_frame *next;
	hx_os_deadline deadline;
	uint64_t peer;
	uint64_t id;
	size_t bytes;
	int control;
	int kind;
	unsigned char wire[];
};

struct hx_ipc_waiter {
	hx_ipc_waiter *next;
	hx_ipc_message *reply;
	hx_os_error error;
	uint64_t peer;
	uint64_t id;
	int kind;
	int done;
};

struct hx_ipc_connection {
	hx_ipc *endpoint;
	hx_os_socket *socket;
	hx_os_pipe *reader;
	hx_os_pipe *writer;
	hx_os_request *reading;
	hx_os_request *writing;
	hx_os_request *connecting;
	hx_ipc_frame *sending;
	hx_os_kcp *kcp;
	hx_ipc_received *incoming;
	hx_os_address address;
	hx_ipc_header header;
	hx_os_error error;
	uint64_t id;
	uint64_t remote;
	uint64_t received_at;
	uint64_t ping_at;
	uint64_t handshake_at;
	size_t max_message;
	size_t header_bytes;
	size_t body_bytes;
	size_t controls;
	size_t control_bytes;
	hx_os_status rejected;
	// 0 free, 1 connecting, 2 handshake, 3 ready, 4 retiring, 5 retired.
	// The I/O owner changes state/identity under the endpoint mutex.
	int state;
	int announced;
	unsigned char prefix[HX_IPC_I_HEADER];
	unsigned char input[4096];
};

struct hx_ipc {
	hx_os_atomic stop;

	hx_os_context *context;
	hx_os_network *network;
	hx_os_loop *loop;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_thread *thread;
	hx_os_socket *listener;
	hx_os_pipe *accept_pipe;
	hx_os_loop_watch *local_watch;
	hx_os_request *accepting;
	hx_os_request *datagram_read;

	hx_ipc_connection *peers;
	hx_ipc_waiter *waiters;
	hx_ipc_frame *outbox;
	hx_ipc_frame *outbox_last;
	hx_ipc_received *inbox;
	hx_ipc_received *inbox_last;
	hx_ipc_message *events;

	unsigned char *datagram;
	unsigned char *kcp_message;
	char *name;
	hx_string address_text;
	hx_os_address address;

	hx_ipc_options options;
	hx_ipc_statistics statistics;
	hx_os_error error;
	hx_os_backoff backoff;

	uint64_t serial;
	uint64_t retry_at;
	hx_os_deadline shutdown_deadline;

	size_t users;
	size_t wait_count;
	size_t inbox_count;
	size_t outbox_count;
	size_t control_bytes;
	size_t event_read;
	size_t event_count;
	size_t event_capacity;

	uint32_t attempts;
	int transport;
	int server;
	int closing;
	int finished;
	int close_running;
	int overflow;
	int use_backoff;
	int remote_kcp;
#if !defined(_WIN32)
	uint64_t path_device;
	uint64_t path_inode;
	int lock_descriptor;
#endif
};

hx_os_status hx_ipc_i_native_prepare(
	hx_ipc *endpoint, const char *address, hx_os_error *error);

hx_os_status hx_ipc_i_open_kcp(hx_os_context *context, const char *address,
	const hx_ipc_options *options, int server, hx_os_deadline deadline,
	hx_ipc **out, hx_os_error *error);

hx_os_status hx_ipc_i_native_connect(
	hx_ipc_connection *peer, hx_os_error *error);

hx_os_status hx_ipc_i_native_accept(
	hx_ipc *endpoint, int first, hx_os_error *error);

void hx_ipc_i_native_cleanup(hx_ipc *endpoint);

void hx_ipc_i_accepted(hx_ipc *endpoint, hx_os_socket *socket,
	hx_os_pipe *reader, hx_os_pipe *writer);

void HX_OS_CALL hx_ipc_i_pipe_accepted(
	hx_os_request *request, const hx_os_completion *completion, void *user);

hx_os_status hx_ipc_i_pipe_write_end(hx_ipc *endpoint, hx_os_pipe *reader,
	hx_os_pipe **writer, hx_os_error *error);

void hx_ipc_i_failed(hx_ipc *endpoint, const hx_os_error *error);

void hx_ipc_i_peer_failed(
	hx_ipc_connection *peer, hx_os_status status, const hx_os_error *error);

hx_ipc_connection *hx_ipc_i_find(hx_ipc *endpoint, uint64_t id);

hx_ipc_connection *hx_ipc_i_new_peer(
	hx_ipc *endpoint, const hx_os_address *address);

void hx_ipc_i_event(hx_ipc_connection *peer, int kind);

void hx_ipc_i_encode(unsigned char *wire, const hx_ipc_header *header);

int hx_ipc_i_decode(const unsigned char *wire, hx_ipc_header *header);

hx_os_status hx_ipc_i_queue(hx_ipc_connection *peer,
	const hx_ipc_header *header, const void *data, hx_os_deadline deadline,
	int control, hx_os_error *error);

void hx_ipc_i_control(hx_ipc_connection *peer, int kind, uint64_t id,
	uint64_t auxiliary, hx_os_status status);

void hx_ipc_i_frame_free(hx_ipc *endpoint, hx_ipc_frame *frame);
void hx_ipc_i_received_free(
	hx_ipc *endpoint, hx_ipc_received *message, int counted);

void hx_ipc_i_complete(hx_ipc *endpoint, uint64_t peer, uint64_t id,
	hx_os_status status, hx_ipc_message *reply);

void hx_ipc_i_stream_input(
	hx_ipc_connection *peer, const unsigned char *data, size_t bytes);

void hx_ipc_i_datagram(hx_ipc *endpoint, const hx_os_address *address,
	const unsigned char *data, size_t bytes);

hx_os_status hx_ipc_i_kcp_output(
	const void *packet, size_t bytes, void *user, hx_os_error *error);

void *HX_OS_CALL hx_ipc_i_worker(void *user);

void hx_ipc_i_dispose(hx_ipc *endpoint);

static inline void hx_ipc_i_lock(hx_ipc *endpoint)
{
	(void)hx_os_mutex_lock(endpoint->mutex, NULL);
}

static inline void hx_ipc_i_unlock(hx_ipc *endpoint)
{
	(void)hx_os_mutex_unlock(endpoint->mutex, NULL);
}

static inline int hx_ipc_i_stopping(hx_ipc *endpoint)
{
	return hx_os_atomic_load(&endpoint->stop, HX_OS_ACQUIRE) != 0;
}

static inline int hx_ipc_i_aborting(hx_ipc *endpoint)
{
	return hx_os_atomic_load(&endpoint->stop, HX_OS_ACQUIRE) >= 2;
}

#endif
