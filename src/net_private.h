#ifndef HX_C_PLATFORM_NET_PRIVATE_H
#define HX_C_PLATFORM_NET_PRIVATE_H

#include "internal.h"
#include "hx_os_net.h"
#include "poll_private.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET hx_os_i_socket;
typedef int hx_os_i_socklen;
#define HX_OS_I_BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int hx_os_i_socket;
typedef socklen_t hx_os_i_socklen;
#define HX_OS_I_BAD_SOCKET (-1)
#endif

struct hx_os_network {
	hx_os_context *context;
	hx_os_atomic objects;
};
struct hx_os_socket {
	hx_os_network *network;
	hx_os_i_socket native;
	int family;
	int protocol;
	hx_os_atomic pending;
	void *loop;
	struct hx_os_socket *loop_next;
	hx_os_i_poll_source source;
};

hx_os_status hx_os_i_socket_error(hx_os_error *error, int code);
int hx_os_i_socket_last_error(void);
void hx_os_i_socket_close(hx_os_i_socket socket);
hx_os_status hx_os_i_socket_wrap(hx_os_network *network, hx_os_i_socket native,
	int family, int protocol, hx_os_socket **out, hx_os_error *error);
hx_os_status hx_os_i_socket_connected(hx_os_socket *socket, hx_os_error *error);
int hx_os_i_address_native(const hx_os_address *address,
	struct sockaddr_storage *native, hx_os_i_socklen *length);
int hx_os_i_address_public(
	const struct sockaddr *native, hx_os_address *address);
hx_os_status hx_os_i_loop_detach(hx_os_socket *socket, hx_os_error *error);

#endif
