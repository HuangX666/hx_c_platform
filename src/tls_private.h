#ifndef HX_C_PLATFORM_TLS_PRIVATE_H
#define HX_C_PLATFORM_TLS_PRIVATE_H

#include "internal.h"
#include "hx_os_tls.h"

#if HX_OS_HAVE_TLS
#include "crypto_private.h"
#include "loop_private.h"
#include <openssl/ssl.h>

#define HX_TLS_RECORD 16384
#define HX_TLS_MARKERS 16

typedef struct hx_tls_ring {
	unsigned char *data;
	size_t head, size;
} hx_tls_ring;

typedef struct hx_tls_marker {
	uint64_t wire, plain;
} hx_tls_marker;

typedef struct hx_tls_peer {
	hx_os_tls *owner;
	hx_os_socket *socket;
	SSL *ssl;
	BIO *bio;
	hx_os_request *reading, *writing, *connecting;
	unsigned char *buffers;
	hx_tls_ring rx, tx;
	hx_tls_marker markers[HX_TLS_MARKERS];
	hx_os_tls_peer_info info;
	hx_os_error error;
	hx_os_deadline handshake;
	uint64_t accepted, encoded, flushed, produced, sent;
	size_t wire_bytes, wire_offset, tx_bytes;
	size_t marker_head, marker_count, users;
	int connected, retry_connect, ready, announced, retired, release,
		closing;
	int sender, receiver, terminal, tcp_eof;
} hx_tls_peer;

struct hx_os_tls {
	hx_os_context *context;
	hx_os_mutex *mutex;
	hx_os_condition *condition;
	hx_os_thread *worker;
	hx_os_network *network;
	hx_os_address_list *addresses;
	hx_os_loop *loop;
	hx_os_socket *listener;
	hx_os_request *accepting;
	hx_os_crypto *crypto;
	SSL_CTX *ssl_context;
	hx_tls_peer *peers;
	char *host;
	hx_os_tls_options options;
	hx_os_address local;
	hx_os_error error;
	uint64_t next_id, cancelled;
	size_t users, address_index;
	uint16_t port;
	int server, ready, closing, close_caller;
};

void hx_tls_i_lock(hx_os_tls *tls);
void hx_tls_i_unlock(hx_os_tls *tls);
void hx_tls_i_wake(hx_os_tls *tls);
int hx_tls_i_reentrant(hx_os_tls *tls);
void hx_tls_i_guard(hx_os_tls *tls);
void hx_tls_i_dispose(hx_os_tls *tls);
void hx_tls_i_fail(hx_os_tls *tls, const hx_os_error *error);
void hx_tls_i_peer_fail(hx_tls_peer *peer, const hx_os_error *error);
void *HX_OS_CALL hx_tls_i_worker(void *user);
hx_os_status hx_tls_i_credentials(hx_os_tls *tls, hx_os_error *error);
hx_os_status hx_tls_i_ssl(hx_tls_peer *peer, hx_os_error *error);
hx_os_status hx_tls_i_roots(hx_os_tls *tls, hx_os_error *error);
hx_os_status hx_tls_i_ca_file(
	hx_os_tls *tls, const char *path, hx_os_error *error);

size_t hx_tls_i_ring_write(
	hx_tls_ring *ring, size_t capacity, const void *data, size_t bytes);
size_t hx_tls_i_ring_read(
	hx_tls_ring *ring, size_t capacity, void *data, size_t bytes);
#endif
#endif
