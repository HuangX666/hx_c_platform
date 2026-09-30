#if !defined(_WIN32)
#define _GNU_SOURCE
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct fd_test {
	hx_os_loop_watch *watch;
	int calls;
	int cleanups;
} fd_test;

static void ready(hx_os_loop_watch *watch, int events, void *user)
{
	fd_test *state = user;
#if !defined(_WIN32)
	char data[8];
	CHECK(events & HX_OS_WAIT_READ);
	CHECK(read(hx_os_loop_watch_descriptor(watch), data, sizeof(data)) ==
		4);
	CHECK(!memcmp(data, "ping", 4));
	CHECK(hx_os_loop_watch_wait_until(watch, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_DEADLOCK);
	CHECK(hx_os_loop_detach_fd(&state->watch, NULL) == HX_OS_DEADLOCK);
#else
	(void)watch;
	(void)events;
#endif
	++state->calls;
}
static void cleanup(void *user)
{
	fd_test *state = user;
	++state->cleanups;
	CHECK(hx_os_loop_detach_fd(&state->watch, NULL) == HX_OS_DEADLOCK);
}
static void posted(
	hx_os_request *request, const hx_os_completion *result, void *user)
{
	(void)request;
	(void)result;
	(void)user;
	CHECK(0);
}

int main(void)
{
	fd_test state = {NULL, 0, 0};
	if (!(hx_os_capabilities() & HX_OS_CAP_LOOP_FD)) {
		CHECK(hx_os_loop_attach_fd(NULL, 0, HX_OS_WAIT_READ, ready,
			      &state, &state.watch,
			      NULL) == HX_OS_NOT_SUPPORTED);
		return 0;
	}
#if !defined(_WIN32)
	{
		int descriptors[2];
		hx_os_loop *loop = NULL;
		hx_os_context *context = NULL;
		hx_os_request *request = NULL;
		hx_os_loop_options options;
		hx_os_operation operation;
		size_t baseline;
		CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
		hx_os_loop_options_init(&options);
		options.max_requests = 1;
		CHECK(hx_os_loop_create_local(context, &options, &loop, NULL) ==
			HX_OS_OK);
		baseline = hx_os_context_allocations(context);
		CHECK(pipe(descriptors) == 0);
		CHECK(fcntl(descriptors[0], F_SETFL, O_NONBLOCK) == 0);
		CHECK(hx_os_loop_attach_fd_with_cleanup(loop, descriptors[0],
			      HX_OS_WAIT_READ, ready, cleanup, &state,
			      &state.watch, NULL) == HX_OS_OK);
		CHECK(!state.calls && hx_os_loop_pending(loop) == 1);
		CHECK(hx_os_loop_watch_descriptor(state.watch) !=
			descriptors[0]);
		CHECK(close(descriptors[0]) == 0);
		hx_os_operation_init(&operation, HX_OS_OP_POST);
		CHECK(hx_os_loop_submit(loop, &operation, posted, NULL,
			      &request, NULL) == HX_OS_LIMIT &&
			!request);
		CHECK(write(descriptors[1], "ping", 4) == 4);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);
		CHECK(state.calls == 1 && state.cleanups == 1);
		CHECK(hx_os_loop_watch_descriptor(state.watch) == -1);
		CHECK(hx_os_loop_watch_wait_until(state.watch,
			      HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_detach_fd(&state.watch, NULL) == HX_OS_OK);
		CHECK(!state.watch &&
			hx_os_context_allocations(context) == baseline);
		CHECK(close(descriptors[1]) == 0);
#if defined(__linux__)
		{
			int regular = open("/dev/null", O_RDONLY);
			hx_os_error error;
			CHECK(regular >= 0);
			CHECK(hx_os_loop_attach_fd(loop, regular,
				      HX_OS_WAIT_READ, ready, &state,
				      &state.watch,
				      &error) == HX_OS_NOT_SUPPORTED);
			CHECK(error.domain == HX_OS_ERROR_ERRNO &&
				!state.watch);
			CHECK(hx_os_loop_pending(loop) == 0 &&
				hx_os_context_allocations(context) == baseline);
			CHECK(close(regular) == 0);
		}
#endif
		state.calls = state.cleanups = 0;
		CHECK(pipe(descriptors) == 0);
		CHECK(hx_os_loop_attach_fd_with_cleanup(loop, descriptors[0],
			      HX_OS_WAIT_READ, ready, cleanup, &state,
			      &state.watch, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK);
		CHECK(state.calls == 0 && state.cleanups == 1);
		CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_BUSY);
		CHECK(hx_os_loop_detach_fd(&state.watch, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
		CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	}
#endif
	puts("external descriptor lifecycle contracts passed");
	return 0;
}
