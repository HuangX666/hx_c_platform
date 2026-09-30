#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
static void *echo_peer(void *user)
{
	hx_os_file *file = user;
	char buffer[32];
	size_t total = 0, got, sent;
	hx_os_status status;
	do {
		status = hx_os_file_read_some(file, buffer + total,
			sizeof(buffer) - total, &got, NULL);
		total += got;
	} while (status == HX_OS_OK);
	CHECK(status == HX_OS_EOF && total == 10);
	CHECK(!memcmp(buffer, "中文😀", 10));
	CHECK(hx_os_file_write_all(file, buffer, total, &sent, NULL) ==
			HX_OS_OK &&
		sent == total);
	CHECK(hx_os_pipe_socket_shutdown(file, HX_OS_SHUT_WRITE, NULL) ==
		HX_OS_OK);
	return NULL;
}
typedef struct allocation_state {
	int calls, fail, live;
} allocation_state;
static void *HX_OS_CALL allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	void *memory;
	if (++state->calls == state->fail)
		return NULL;
	memory = malloc(bytes);
	if (memory)
		++state->live;
	return memory;
}
static void HX_OS_CALL deallocate(void *memory, void *user)
{
	allocation_state *state = user;
	if (memory) {
		--state->live;
		free(memory);
	}
}
int main(void)
{
	hx_os_file *first = NULL, *second = NULL;
	hx_os_thread *thread = NULL;
	hx_os_context *context = NULL;
	hx_os_context_options options;
	allocation_state allocation = {0, 0, 0};
	char buffer[32];
	size_t sent, got, total = 0;
	uint64_t size = 7;
	hx_os_status status;
	int i;
	if (!(hx_os_capabilities() & HX_OS_CAP_DUPLEX_PIPE)) {
		CHECK(hx_os_pipe_pair_socket(NULL, &first, &second, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!first && !second);
		return 0;
	}
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &allocation;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);
	for (i = 1; i <= 3; ++i) {
		allocation.calls = 0;
		allocation.fail = i;
		status = hx_os_pipe_pair_socket(context, &first, &second, NULL);
		allocation.fail = 0;
		CHECK(status == HX_OS_OK || status == HX_OS_NO_MEMORY);
		if (status != HX_OS_OK)
			CHECK(!first && !second);
		CHECK(hx_os_file_close(&first, NULL) == HX_OS_OK);
		CHECK(hx_os_file_close(&second, NULL) == HX_OS_OK);
		CHECK(hx_os_context_allocations(context) == 0);
	}
	CHECK(hx_os_pipe_pair_socket(context, &first, &second, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_file_size(first, &size, NULL) == HX_OS_NOT_SUPPORTED &&
		size == 7);
	CHECK(hx_os_file_sync(first, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_seek(first, 0, 0, &size, NULL) == HX_OS_NOT_SUPPORTED);
#if defined(_WIN32)
	if (hx_os_capabilities() & HX_OS_CAP_PROCESS) {
		hx_os_process_options spawn = HX_OS_PROCESS_OPTIONS_INIT;
		hx_os_process *process = NULL;
		spawn.standard_output = first;
		CHECK(hx_os_process_spawn(NULL, "unused-duplex-child", NULL,
			      NULL, &spawn, &process,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!process);
	}
#endif
	CHECK(hx_os_thread_create(NULL, echo_peer, second, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_write_all(first, "中文😀", 10, &sent, NULL) ==
			HX_OS_OK &&
		sent == 10);
	CHECK(hx_os_pipe_socket_shutdown(first, HX_OS_SHUT_WRITE, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_write_some(first, "x", 1, &sent, NULL) ==
			HX_OS_CLOSED &&
		!sent);
	do {
		status = hx_os_file_read_some(first, buffer + total,
			sizeof(buffer) - total, &got, NULL);
		total += got;
	} while (status == HX_OS_OK);
	CHECK(status == HX_OS_EOF && total == 10 &&
		!memcmp(buffer, "中文😀", 10));
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(2000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&first, NULL) == HX_OS_OK);
	CHECK(hx_os_file_close(&second, NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK &&
		!allocation.live);
	return 0;
}
