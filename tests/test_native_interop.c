#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#endif
#include "utility_test.h"

static void test_file(void)
{
	test_memory memory = {0};
	hx_os_context *context = test_context(&memory);
	hx_os_file *file = NULL;
	uintptr_t native = 42;
	char buffer[4];
	size_t count, calls, i;
	uint64_t offset;

	CHECK(hx_os_file_native_borrow(NULL, &native, NULL) == HX_OS_INVALID);
	CHECK(native == 42);
	CHECK(hx_os_file_open(context, "native-interop-test.tmp",
		      HX_OS_FILE_READ | HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_EXCLUSIVE,
		      &file, NULL) == HX_OS_OK);
	calls = memory.calls;
	for (i = 0; i < 1000; ++i)
		CHECK(hx_os_file_native_borrow(file, &native, NULL) ==
			HX_OS_OK);
	CHECK(memory.calls == calls);
#if defined(_WIN32)
	{
		DWORD written = 0;

		CHECK(GetFileType((HANDLE)native) == FILE_TYPE_DISK);
		CHECK(WriteFile((HANDLE)native, "data", 4, &written, NULL));
		CHECK(written == 4);
	}
#else
	{
		struct stat info;

		CHECK(fstat((int)native, &info) == 0 && S_ISREG(info.st_mode));
		CHECK(write((int)native, "data", 4) == 4);
	}
#endif
	CHECK(hx_os_file_seek(file, 0, HX_OS_SEEK_CURRENT, &offset, NULL) ==
			HX_OS_OK &&
		offset == 4);
	CHECK(hx_os_file_seek(file, 0, HX_OS_SEEK_BEGIN, &offset, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_read_some(file, buffer, 4, &count, NULL) == HX_OS_OK);
	CHECK(count == 4 && !memcmp(buffer, "data", 4));
	CHECK(hx_os_file_close(&file, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove("native-interop-test.tmp", NULL) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(!memory.live);
}

typedef struct thread_state {
	hx_os_event *ready, *release;
#if defined(_WIN32)
	DWORD identifier;
#else
	pthread_t identifier;
#endif
} thread_state;

static void *HX_OS_CALL thread_worker(void *user)
{
	thread_state *state = user;

#if defined(_WIN32)
	state->identifier = GetCurrentThreadId();
#else
	state->identifier = pthread_self();
#endif
	hx_os_event_set(state->ready);
	CHECK(hx_os_event_wait_until(state->release,
		      hx_os_deadline_after_ms(10000), NULL) == HX_OS_OK);

	return NULL;
}

static void test_thread(void)
{
	thread_state state;
	hx_os_thread *thread = NULL;

	struct native_probe {
#if defined(_WIN32)
		HANDLE value;
#else
		pthread_t value;
#endif
		unsigned char tail[16];
	} probe, previous;

	CHECK(hx_os_event_create(NULL, 1, 0, &state.ready, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &state.release, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, thread_worker, &state, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(state.ready, hx_os_deadline_after_ms(3000),
		      NULL) == HX_OS_OK);
	memset(&probe, 0x5a, sizeof(probe));
	previous = probe;
	CHECK(hx_os_thread_native_size() == sizeof(probe.value));
	CHECK(hx_os_thread_native_borrow(thread, &probe.value,
		      sizeof(probe.value) - 1, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(!memcmp(&probe, &previous, sizeof(probe)));
	CHECK(hx_os_thread_native_borrow(
		      thread, &probe.value, sizeof(probe), NULL) == HX_OS_OK);
	CHECK(!memcmp(probe.tail, previous.tail, sizeof(probe.tail)));
#if defined(_WIN32)
	CHECK(GetThreadId(probe.value) == state.identifier);
#else
	CHECK(pthread_equal(probe.value, state.identifier));
#endif
	hx_os_event_set(state.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(3000),
		      NULL, NULL) == HX_OS_OK);
	previous = probe;
	CHECK(hx_os_thread_native_borrow(thread, &probe.value, sizeof(probe),
		      NULL) == HX_OS_CLOSED);
	CHECK(!memcmp(&probe, &previous, sizeof(probe)));
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	hx_os_event_destroy(state.ready);
	hx_os_event_destroy(state.release);
}

static void HX_OS_CALL accept_cancelled(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	(void)request;
	CHECK(completion->error.status == HX_OS_CANCELLED);
	++*(int *)user;
}

static void test_socket(void)
{
	hx_os_network *network = NULL;
	hx_os_socket *socket = NULL;
	hx_os_address address;
	uintptr_t native = 42;
	int type = 0;
#if defined(_WIN32)
	int length = sizeof(type);
#else
	socklen_t length = sizeof(type);
#endif

	CHECK(hx_os_network_create(NULL, &network, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_create(network, HX_OS_IPV4, HX_OS_TCP, &socket,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_socket_native_borrow(socket, &native, NULL) == HX_OS_OK);
#if defined(_WIN32)
	CHECK(getsockopt((SOCKET)native, SOL_SOCKET, SO_TYPE, (char *)&type,
		      &length) == 0);
#else
	CHECK(getsockopt((int)native, SOL_SOCKET, SO_TYPE, &type, &length) ==
		0);
#endif
	CHECK(type == SOCK_STREAM);
	CHECK(hx_os_address_parse("127.0.0.1", 0, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_bind(socket, &address, NULL) == HX_OS_OK);
	CHECK(hx_os_socket_listen(socket, 1, NULL) == HX_OS_OK);

	if (hx_os_capabilities() & HX_OS_CAP_LOOP) {
		hx_os_loop *loop = NULL;
		hx_os_request *request = NULL;
		hx_os_operation operation;
		int calls = 0;

		CHECK(hx_os_loop_create(network, NULL, &loop, NULL) ==
			HX_OS_OK);
		hx_os_operation_init(&operation, HX_OS_OP_ACCEPT);
		operation.socket = socket;
		operation.deadline = hx_os_deadline_after_ms(3000);
		CHECK(hx_os_loop_submit(loop, &operation, accept_cancelled,
			      &calls, &request, NULL) == HX_OS_OK);
		native = 42;
		CHECK(hx_os_socket_native_borrow(socket, &native, NULL) ==
				HX_OS_BUSY &&
			native == 42);
		hx_os_request_cancel(request);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(3000),
			      NULL) == HX_OS_OK);
		CHECK(calls == 1);
		hx_os_request_release(request);
		CHECK(hx_os_socket_native_borrow(socket, &native, NULL) ==
			HX_OS_BUSY);
		CHECK(hx_os_socket_close(&socket, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_close_until(loop,
			      hx_os_deadline_after_ms(3000), NULL) == HX_OS_OK);
		CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
	}
	else
		CHECK(hx_os_socket_close(&socket, NULL) == HX_OS_OK);

	CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
}

static void test_process(const char *self)
{
	hx_string path = HX_STRING_INIT;
	hx_os_process *process = NULL;
	const char *arguments[] = {"--native-child", NULL};
	uintptr_t value = 42;
#if defined(_WIN32)
	wchar_t executable[32768];
	DWORD units = GetModuleFileNameW(NULL, executable, 32768);

	(void)self;
	CHECK(units > 0 && units < 32768);
	CHECK(hx_string_assign_wide(&path, executable, units, NULL) ==
		HX_OS_OK);
#else
	CHECK(hx_string_assign_cstr(&path, self, NULL) == HX_OS_OK);
#endif
	CHECK(hx_os_process_spawn(NULL, hx_string_c_str(&path), arguments, NULL,
		      NULL, &process, NULL) == HX_OS_OK);
	CHECK(hx_os_process_native_borrow(process, &value, NULL) == HX_OS_OK);
#if defined(_WIN32)
	CHECK(GetProcessId((HANDLE)value) == hx_os_process_identifier(process));
#else
	CHECK(value == hx_os_process_identifier(process));
#endif
	CHECK(hx_os_process_wait_until(process, hx_os_deadline_after_ms(5000),
		      NULL, NULL) == HX_OS_OK);
	value = 42;
	CHECK(hx_os_process_native_borrow(process, &value, NULL) ==
			HX_OS_CLOSED &&
		value == 42);
	CHECK(hx_os_process_destroy(&process, NULL) == HX_OS_OK);
	hx_string_destroy(&path);
}

int main(int argc, char **argv)
{
	uintptr_t value = 42;
	uint64_t capabilities = hx_os_capabilities();

	if (argc == 2 && !strcmp(argv[1], "--native-child"))
		return 0;
	if (!hx_os_native_interop_available()) {
		CHECK(!hx_os_thread_native_size());
		CHECK(hx_os_file_native_borrow(NULL, &value, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			value == 42);
		CHECK(hx_os_socket_native_borrow(NULL, &value, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			value == 42);
		CHECK(hx_os_process_native_borrow(NULL, &value, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			value == 42);
		CHECK(hx_os_thread_native_borrow(NULL, &value, sizeof(value),
			      NULL) == HX_OS_NOT_SUPPORTED &&
			value == 42);

		return 0;
	}

	test_file();
	test_thread();
	if (capabilities & HX_OS_CAP_NETWORK)
		test_socket();
	else
		CHECK(hx_os_socket_native_borrow(NULL, &value, NULL) ==
			HX_OS_NOT_SUPPORTED);
	if (capabilities & HX_OS_CAP_PROCESS)
		test_process(argv[0]);
	else
		CHECK(hx_os_process_native_borrow(NULL, &value, NULL) ==
			HX_OS_NOT_SUPPORTED);

	return 0;
}
