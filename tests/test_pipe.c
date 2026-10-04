#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pthread.h>
#include <signal.h>
#endif

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct pipe_result {
	hx_os_completion completion;
	hx_os_pipe **closing;
	int calls;
} pipe_result;

static void pipe_completed(
	hx_os_request *request, const hx_os_completion *completion, void *user)
{
	pipe_result *result = user;

	++result->calls;
	CHECK(result->calls == 1);

	result->completion = *completion;
	CHECK(hx_os_request_wait_until(request, HX_OS_DEADLINE_NOW, NULL,
		      NULL) == HX_OS_DEADLOCK);
	if (result->closing)
		CHECK(hx_os_pipe_close_until(result->closing,
			      HX_OS_DEADLINE_NOW, NULL) == HX_OS_DEADLOCK);
}

static void pipe_retired(
	hx_os_request *request, pipe_result *result, hx_os_status status)
{
	hx_os_completion completion;

	CHECK(result->calls == 1 && result->completion.error.status == status);
	CHECK(hx_os_request_wait_until(request, HX_OS_DEADLINE_NOW, &completion,
		      NULL) == HX_OS_OK);
	CHECK(completion.error.status == status);

	hx_os_request_release(request);
}

static void test_sync(void)
{
	hx_os_file *reader = NULL, *writer = NULL;
	char bytes[32];
	size_t count;
	uint64_t size = 7;

	CHECK(hx_os_pipe_pair_anonymous(NULL, &reader, &writer, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_size(reader, &size, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(size == 7);
	CHECK(hx_os_file_sync(writer, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_file_write_all(writer, "中文", 6, &count, NULL) ==
		HX_OS_OK);
	CHECK(count == 6);
	CHECK(hx_os_file_close(&writer, NULL) == HX_OS_OK);
	CHECK(hx_os_file_read_some(
		      reader, bytes, sizeof(bytes), &count, NULL) == HX_OS_OK &&
		count == 6);
	CHECK(!memcmp(bytes, "中文", 6));
	CHECK(hx_os_file_read_some(reader, bytes, sizeof(bytes), &count,
		      NULL) == HX_OS_EOF &&
		!count);
	CHECK(hx_os_file_close(&reader, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_pair_anonymous(NULL, &reader, &writer, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_close(&reader, NULL) == HX_OS_OK);
	CHECK(hx_os_file_write_some(writer, "x", 1, &count, NULL) ==
			HX_OS_CLOSED &&
		!count);
#if !defined(_WIN32)
	{
		sigset_t mask, saved, pending;
		int value;

		sigemptyset(&mask);
		sigaddset(&mask, SIGPIPE);
		CHECK(pthread_sigmask(SIG_BLOCK, &mask, &saved) == 0);
		CHECK(raise(SIGPIPE) == 0);
		CHECK(hx_os_file_write_some(writer, "x", 1, &count, NULL) ==
			HX_OS_CLOSED);
		CHECK(sigpending(&pending) == 0 &&
			sigismember(&pending, SIGPIPE));
		CHECK(sigwait(&mask, &value) == 0 && value == SIGPIPE);
		CHECK(pthread_sigmask(SIG_SETMASK, &saved, NULL) == 0);
	}
#endif
	CHECK(hx_os_file_close(&writer, NULL) == HX_OS_OK);
}

static void *pipe_test_allocate(size_t bytes, void *user)
{
	int *budget = user;

	if (!*budget)
		return NULL;

	--*budget;

	return malloc(bytes);
}

static void pipe_test_free(void *memory, void *user)
{
	(void)user;

	free(memory);
}

static void test_creation_failure(void)
{
	hx_os_context_options options;
	hx_os_context *context = NULL;
	int budget = 32, i;

	hx_os_context_options_init(&options);
	options.allocator.allocate = pipe_test_allocate;
	options.allocator.deallocate = pipe_test_free;
	options.allocator.user = &budget;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	for (i = 0; i < 3; ++i) {
		hx_os_file *reader = NULL, *writer = NULL;
		hx_os_status status;

		budget = i;

		status = hx_os_pipe_pair_anonymous(
			context, &reader, &writer, NULL);
		if (status == HX_OS_OK) {
			CHECK(hx_os_file_close(&reader, NULL) == HX_OS_OK);
			CHECK(hx_os_file_close(&writer, NULL) == HX_OS_OK);
		}
		else
			CHECK(status == HX_OS_NO_MEMORY && !reader && !writer);

		CHECK(!hx_os_context_allocations(context));
	}

	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
}

static void *pipe_cancel_worker(void *user)
{
	hx_os_request_cancel(user);

	return NULL;
}

static void test_cancel_race(hx_os_loop *loop)
{
	int i;

	for (i = 0; i < 16; ++i) {
		hx_os_pipe *reader = NULL, *writer = NULL;
		hx_os_request *read_request, *write_request;
		hx_os_thread *thread = NULL;
		pipe_result read_result, write_result;
		char data;

		memset(&read_result, 0, sizeof(read_result));
		memset(&write_result, 0, sizeof(write_result));
		CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_pipe_read_async(loop, reader, &data, 1,
			      HX_OS_DEADLINE_INFINITE, pipe_completed,
			      &read_result, &read_request, NULL) == HX_OS_OK);
		CHECK(hx_os_pipe_write_async(loop, writer, "x", 1,
			      HX_OS_DEADLINE_INFINITE, pipe_completed,
			      &write_result, &write_request, NULL) == HX_OS_OK);
		CHECK(hx_os_thread_create(NULL, pipe_cancel_worker,
			      read_request, &thread, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_join_until(thread,
			      hx_os_deadline_after_ms(2000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
		CHECK(read_result.completion.error.status == HX_OS_OK ||
			read_result.completion.error.status == HX_OS_CANCELLED);

		pipe_retired(read_request, &read_result,
			read_result.completion.error.status);
		pipe_retired(write_request, &write_result, HX_OS_OK);
		CHECK(hx_os_pipe_close_until(
			      &reader, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
		CHECK(hx_os_pipe_close_until(
			      &writer, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	}
}

static void test_async(hx_os_loop *loop)
{
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_request *read_request, *write_request, *rejected = NULL;
	pipe_result read_result, write_result;
	char input[] = "hello", output[32] = {0};

	memset(&read_result, 0, sizeof(read_result));
	memset(&write_result, 0, sizeof(write_result));
	CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_read_async(loop, reader, output, sizeof(output),
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &read_result,
		      &read_request, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_read_async(loop, reader, output, sizeof(output),
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &read_result,
		      &rejected, NULL) == HX_OS_BUSY &&
		!rejected);
	CHECK(hx_os_pipe_write_async(loop, writer, input, 5,
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &write_result,
		      &write_request, NULL) == HX_OS_OK);

	memset(input, 'x', 5);
	CHECK(!read_result.calls && !write_result.calls);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(read_result.completion.bytes == 5 && !memcmp(output, "hello", 5));
	CHECK(write_result.completion.bytes == 5);

	pipe_retired(read_request, &read_result, HX_OS_OK);
	pipe_retired(write_request, &write_result, HX_OS_OK);
	CHECK(hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);

	memset(&read_result, 0, sizeof(read_result));
	CHECK(hx_os_pipe_read_async(loop, reader, output, sizeof(output),
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &read_result,
		      &read_request, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);

	pipe_retired(read_request, &read_result, HX_OS_EOF);
	CHECK(hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
}

static void test_cancel(hx_os_loop *loop)
{
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_request *request;
	pipe_result result;
	hx_os_status status;
	char output[4] = {1, 2, 3, 4};
	int step;

	CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) == HX_OS_OK);

	for (step = 0; step < 3; ++step) {
		memset(&result, 0, sizeof(result));
		CHECK(hx_os_pipe_read_async(loop, reader, output,
			      sizeof(output),
			      step == 2 ? HX_OS_DEADLINE_NOW
					: HX_OS_DEADLINE_INFINITE,
			      pipe_completed, &result, &request,
			      NULL) == HX_OS_OK);
		if (step == 1) {
			status = hx_os_loop_poll_until(
				loop, HX_OS_DEADLINE_NOW, NULL);
			CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);
			CHECK(!result.calls);
		}

		if (step != 2)
			hx_os_request_cancel(request);

		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);

		pipe_retired(request, &result,
			step == 2 ? HX_OS_TIMEOUT : HX_OS_CANCELLED);
		CHECK(output[0] == 1 && output[3] == 4);
	}

	memset(&result, 0, sizeof(result));
	result.closing = &reader;
	CHECK(hx_os_pipe_read_async(loop, reader, output, sizeof(output),
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &result,
		      &request, NULL) == HX_OS_OK);

	status = hx_os_loop_poll_until(loop, HX_OS_DEADLINE_NOW, NULL);
	CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);

	status = hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL);
	CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);
	if (status == HX_OS_TIMEOUT) {
		hx_os_request *rejected = NULL;

		CHECK(reader);
		CHECK(hx_os_pipe_read_async(loop, reader, output,
			      sizeof(output), HX_OS_DEADLINE_INFINITE,
			      pipe_completed, &result, &rejected,
			      NULL) == HX_OS_CLOSED);
		CHECK(hx_os_pipe_close_until(&reader,
			      hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);
	}

	CHECK(!reader);

	pipe_retired(request, &result, HX_OS_CANCELLED);
	CHECK(hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
}

static void test_child(hx_os_loop *loop)
{
	int parent_reads;

	for (parent_reads = 0; parent_reads <= 1; ++parent_reads) {
		hx_os_pipe *parent = NULL;
		hx_os_file *child = NULL;
		hx_os_request *request;
		pipe_result result;
		char data[8] = {0};
		size_t count;

		memset(&result, 0, sizeof(result));
		CHECK(hx_os_pipe_pair_child(NULL, parent_reads, &parent, &child,
			      NULL) == HX_OS_OK);
		if (parent_reads) {
			CHECK(hx_os_file_write_all(child, "child", 5, &count,
				      NULL) == HX_OS_OK);
			CHECK(hx_os_pipe_read_async(loop, parent, data,
				      sizeof(data), HX_OS_DEADLINE_INFINITE,
				      pipe_completed, &result, &request,
				      NULL) == HX_OS_OK);
		}
		else {
			CHECK(hx_os_pipe_write_async(loop, parent, "child", 5,
				      HX_OS_DEADLINE_INFINITE, pipe_completed,
				      &result, &request, NULL) == HX_OS_OK);
		}

		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);
		if (!parent_reads)
			CHECK(hx_os_file_read_some(child, data, sizeof(data),
				      &count, NULL) == HX_OS_OK &&
				count == 5);

		CHECK(!memcmp(data, "child", 5));

		pipe_retired(request, &result, HX_OS_OK);
		CHECK(hx_os_file_close(&child, NULL) == HX_OS_OK);
		CHECK(hx_os_pipe_close_until(
			      &parent, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	}
}

static void test_large_write(hx_os_loop *loop)
{
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_request *request;
	hx_os_pipe_operation op;
	pipe_result result;
	hx_os_status status;
	size_t bytes = 1024 * 1024;

	char *data = malloc(bytes);
	CHECK(data);

	memset(data, 0x35, bytes);
	memset(&result, 0, sizeof(result));
	CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_operation_init(&op, sizeof(op), HX_OS_PIPE_WRITE) ==
		HX_OS_OK);

	op.pipe = writer;
	op.data = data;
	op.bytes = bytes;
	op.send_mode = HX_OS_SEND_BORROW;
	CHECK(hx_os_loop_submit_pipe(loop, &op, pipe_completed, &result,
		      &request, NULL) == HX_OS_OK);

	status = hx_os_loop_poll_until(loop, HX_OS_DEADLINE_NOW, NULL);
	CHECK(status == HX_OS_OK || status == HX_OS_TIMEOUT);
	CHECK(!result.calls);

	hx_os_request_cancel(request);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);
	CHECK(result.completion.bytes < bytes);

	pipe_retired(request, &result, HX_OS_CANCELLED);

	free(data);
	CHECK(hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);

	memset(&result, 0, sizeof(result));
	CHECK(hx_os_pipe_write_async(loop, writer, "x", 1,
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &result,
		      &request, NULL) == HX_OS_OK);
	CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000), NULL) ==
		HX_OS_OK);

	pipe_retired(request, &result, HX_OS_CLOSED);
	CHECK(hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
}

static void test_budget(void)
{
	hx_os_loop_options options;
	hx_os_loop *loop = NULL;
	hx_os_pipe *reader = NULL, *writer = NULL;
	hx_os_request *request, *rejected;
	hx_os_operation post;
	pipe_result result;
	char data[9];

	memset(&result, 0, sizeof(result));
	hx_os_loop_options_init(&options);
	options.max_requests = 1;
	options.max_pending_bytes = 8;
	CHECK(hx_os_loop_create_local(NULL, &options, &loop, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) == HX_OS_OK);
	CHECK(hx_os_pipe_read_async(loop, reader, data, 9,
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &result,
		      &rejected, NULL) == HX_OS_LIMIT);
	CHECK(!hx_os_loop_pending(loop) && !rejected);
	CHECK(hx_os_pipe_read_async(loop, reader, data, 8,
		      HX_OS_DEADLINE_INFINITE, pipe_completed, &result,
		      &request, NULL) == HX_OS_OK);

	hx_os_operation_init(&post, HX_OS_OP_POST);
	CHECK(hx_os_loop_submit(loop, &post, pipe_completed, &result, &rejected,
		      NULL) == HX_OS_LIMIT &&
		!rejected);
	CHECK(hx_os_loop_close_until(
		      loop, hx_os_deadline_after_ms(2000), NULL) == HX_OS_OK);

	pipe_retired(request, &result, HX_OS_CANCELLED);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_BUSY);
	CHECK(hx_os_pipe_close_until(&reader, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_pipe_close_until(&writer, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
}

int main(void)
{
	hx_os_loop *loop = NULL;

	uint64_t capabilities = hx_os_capabilities();
	if (capabilities & HX_OS_CAP_LOOP) {
		hx_os_operation op;
		hx_os_request *request = NULL;
		pipe_result result;

		memset(&result, 0, sizeof(result));
		CHECK(hx_os_loop_create_local(NULL, NULL, &loop, NULL) ==
			HX_OS_OK);

		hx_os_operation_init(&op, HX_OS_OP_RESOLVE);
		op.host = "localhost";
		CHECK(hx_os_loop_submit(loop, &op, pipe_completed, &result,
			      &request, NULL) == HX_OS_NOT_SUPPORTED);

		hx_os_operation_init(&op, HX_OS_OP_TIMER);
		op.deadline = hx_os_deadline_after_ms(1);
		CHECK(hx_os_loop_submit(loop, &op, pipe_completed, &result,
			      &request, NULL) == HX_OS_OK);
		CHECK(hx_os_loop_run_until(loop, hx_os_deadline_after_ms(2000),
			      NULL) == HX_OS_OK);

		pipe_retired(request, &result, HX_OS_OK);
		CHECK(hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);
	}

	if (!(capabilities & HX_OS_CAP_PIPE)) {
		hx_os_file *reader = NULL, *writer = NULL;

		CHECK(hx_os_pipe_pair_anonymous(NULL, &reader, &writer, NULL) ==
			HX_OS_NOT_SUPPORTED);
	}
	else {
		test_sync();
		test_creation_failure();
	}

	if (!(capabilities & HX_OS_CAP_ASYNC_PIPE)) {
		hx_os_pipe *reader = NULL, *writer = NULL;

		CHECK(hx_os_pipe_pair_async(NULL, &reader, &writer, NULL) ==
			HX_OS_NOT_SUPPORTED);

		return 0;
	}

	CHECK(hx_os_loop_create_local(NULL, NULL, &loop, NULL) == HX_OS_OK);

	test_async(loop);
	test_cancel(loop);
	test_cancel_race(loop);
	test_child(loop);
	test_large_write(loop);
	CHECK(hx_os_loop_close_until(loop, HX_OS_DEADLINE_NOW, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_loop_destroy(&loop, NULL) == HX_OS_OK);

	test_budget();
	puts("pipe contracts passed");

	return 0;
}
