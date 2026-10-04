#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct positioned_work {
	hx_os_file *file;
	hx_os_event *start;
	uint64_t offset;
} positioned_work;

static void *positioned_worker(void *user)
{
	positioned_work *work = user;
	uint32_t i;
	size_t count;

	CHECK(hx_os_event_wait_until(
		      work->start, HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);

	for (i = 1; i <= 100; ++i) {
		uint32_t read_back = 0;

		CHECK(hx_os_file_write_at(work->file, work->offset, &i,
			      sizeof(i), &count, NULL) == HX_OS_OK &&
			count == sizeof(i));
		CHECK(hx_os_file_read_at(work->file, work->offset, &read_back,
			      sizeof(read_back), &count, NULL) == HX_OS_OK);
		CHECK(count == sizeof(read_back) && read_back == i);
	}

	return NULL;
}

static void test_positioned(void)
{
	hx_os_file *file = NULL, *other = NULL;
	hx_os_event *start = NULL;
	hx_os_thread *threads[4];
	positioned_work work[4];
	char path[128], buffer[32];
	uint64_t position, size;
	size_t count;
	int i;

	if (!(hx_os_capabilities() & HX_OS_CAP_POSITIONED_IO)) {
		CHECK(hx_os_file_read_at(NULL, 0, buffer, sizeof(buffer),
			      &count, NULL) == HX_OS_NOT_SUPPORTED &&
			!count);
		CHECK(hx_os_file_write_at(NULL, 0, "x", 1, &count, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!count);

		return;
	}

	(void)snprintf(path, sizeof(path), "hx-positioned-%llx-中文.bin",
		(unsigned long long)hx_os_monotonic_ns());
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_all(
		      file, "0123456789abcdef", 16, &count, NULL) == HX_OS_OK &&
		count == 16);
	CHECK(hx_os_file_seek(file, 7, HX_OS_SEEK_BEGIN, &position, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &start, NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		work[i].file = file;
		work[i].start = start;
		work[i].offset = 64 + (uint64_t)i * 16;
		CHECK(hx_os_thread_create(NULL, positioned_worker, &work[i],
			      &threads[i], NULL) == HX_OS_OK);
	}

	hx_os_event_set(start);
	CHECK(hx_os_file_read_some(file, buffer, 1, &count, NULL) == HX_OS_OK);
	CHECK(count == 1 && buffer[0] == '7');

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(3000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	hx_os_event_destroy(start);
	CHECK(hx_os_file_read_at(file, 3, buffer, 3, &count, NULL) == HX_OS_OK);
	CHECK(count == 3 && !memcmp(buffer, "345", 3));
	CHECK(hx_os_file_seek(file, 0, HX_OS_SEEK_CURRENT, &position, NULL) ==
			HX_OS_OK &&
		position == 8);
	CHECK(hx_os_file_read_at(file, 0, NULL, 0, &count, NULL) == HX_OS_OK &&
		!count);
	CHECK(hx_os_file_read_at(file, UINT64_MAX, buffer, 1, &count, NULL) ==
			HX_OS_RANGE &&
		!count);
	CHECK(hx_os_file_size(file, &size, NULL) == HX_OS_OK);
	CHECK(hx_os_file_read_at(file, size - 1, buffer, sizeof(buffer), &count,
		      NULL) == HX_OS_OK &&
		count == 1);
	CHECK(hx_os_file_read_at(file, size, buffer, sizeof(buffer), &count,
		      NULL) == HX_OS_EOF &&
		!count);
	CHECK(hx_os_file_open(NULL, path,
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_APPEND,
		      &other, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_at(other, 0, "x", 1, &count, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_close(&other, NULL) == HX_OS_OK);
	CHECK(hx_os_file_open(NULL, path, HX_OS_FILE_READ, &other, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_write_at(other, 0, "x", 1, &count, NULL) ==
		HX_OS_ACCESS_DENIED);
	CHECK(hx_os_file_close(&other, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	if (hx_os_capabilities() & HX_OS_CAP_PIPE) {
		CHECK(hx_os_pipe_pair_anonymous(NULL, &file, &other, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_file_read_at(file, 0, buffer, 1, &count, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
		CHECK(hx_os_file_close(&other, NULL) == HX_OS_OK);
	}
}

static void socket_completed(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	int *calls = user;

	(void)request;
	CHECK(completion->error.status == HX_OS_OK);

	++*calls;
}

static int socket_option(hx_os_socket *socket, int level, int name)
{
	int value = 0;
	size_t size = sizeof(value);
	hx_os_error error;

	hx_os_status status = hx_os_socket_option_get(
		socket, level, name, &value, &size, &error);
	if (status != HX_OS_OK || !size || size > sizeof(value))
		fprintf(stderr,
			"option level=%d name=%d status=%d native=%lld "
			"size=%zu value=%d\n",
			level, name, (int)status, (long long)error.native_code,
			size, value);

	CHECK(status == HX_OS_OK && size > 0 && size <= sizeof(value));

	return value;
}

static void test_socket_options(void)
{
	hx_os_network *network = NULL;
	hx_os_socket *listener = NULL, *client = NULL, *server = NULL;
	hx_os_address address, peer, previous;
	hx_os_error error;
	int value = 1;
	size_t size = sizeof(value);

	if (!(hx_os_capabilities() & HX_OS_CAP_NET_OPTIONS)) {
		CHECK(hx_os_socket_set_reuseaddr(NULL, 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_socket_option_get(NULL, SOL_SOCKET, SO_REUSEADDR,
			      &value, &size, NULL) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_socket_peer_address(NULL, &peer, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return;
	}

	CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP, &client,
		      NULL) == HX_OS_OK);
	CHECK(socket_option(client, SOL_SOCKET, SO_REUSEADDR) == 0);
	CHECK(hx_os_socket_set_reuseaddr(client, 1, NULL) == HX_OS_OK);
	CHECK(socket_option(client, SOL_SOCKET, SO_REUSEADDR) != 0);
	CHECK(hx_os_socket_set_reuseaddr(client, 0, NULL) == HX_OS_OK);
	CHECK(socket_option(client, SOL_SOCKET, SO_REUSEADDR) == 0);
	CHECK(hx_os_socket_set_nodelay(client, 1, NULL) == HX_OS_OK);
	CHECK(socket_option(client, IPPROTO_TCP, TCP_NODELAY) != 0);
	CHECK(hx_os_socket_set_keepalive(client, 1, NULL) == HX_OS_OK);
	CHECK(socket_option(client, SOL_SOCKET, SO_KEEPALIVE) != 0);
	CHECK(hx_os_socket_option_set(client, SOL_SOCKET, SO_REUSEADDR, &value,
		      SIZE_MAX, NULL) == HX_OS_RANGE);

	memset(&peer, 0xa5, sizeof(peer));
	previous = peer;
	CHECK(hx_os_socket_peer_address(client, &peer, &error) != HX_OS_OK);
	CHECK(error.native_code && !memcmp(&peer, &previous, sizeof(peer)));

	size = sizeof(value);
	CHECK(hx_os_socket_option_get(client, SOL_SOCKET, 0x7fffffff, &value,
		      &size, &error) != HX_OS_OK &&
		error.native_code);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP, &listener,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_address_parse("127.0.0.1", 0, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_bind(listener, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_listen(listener, 4, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_local_address(listener, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_connect_until(client, &address,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_socket_peer_address(client, &peer, NULL) == HX_OS_OK);
	CHECK(!memcmp(&address, &peer, sizeof(peer)));
	CHECK(hx_os_socket_wait_until(listener, HX_OS_WAIT_READ,
		      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	CHECK(hx_os_socket_accept(listener, &server, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_local_address(client, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_peer_address(server, &peer, NULL) == HX_OS_OK);
	CHECK(!memcmp(&address, &peer, sizeof(peer)));
	if (hx_os_capabilities() & HX_OS_CAP_LOOP) {
		hx_os_loop *loop = NULL;
		hx_os_operation op;
		hx_os_request *request = NULL;
		int calls = 0;

		CHECK(hx_os_loop_create(network, NULL, &loop, NULL) ==
			HX_OS_OK);

		hx_os_operation_init(&op, HX_OS_OP_READ);
		op.socket = client;
		CHECK(hx_os_loop_submit(loop, &op, socket_completed, &calls,
			      &request, NULL) == HX_OS_OK);
		CHECK(hx_os_socket_set_reuseaddr(client, 1, NULL) ==
			HX_OS_BUSY);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK &&
			calls == 1);
		CHECK(hx_os_socket_set_nodelay(client, 0, NULL) == HX_OS_BUSY);

		hx_os_request_release(request);
		CHECK(hx_os_socket_close(&client, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
	}

	CHECK(hx_os_socket_close(&client, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&server, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_close(&listener, NULL) == HX_OS_OK);
	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
}

int main(void)
{
	test_positioned();
	test_socket_options();
	puts("positioned I/O and socket option contracts passed");

	return 0;
}
