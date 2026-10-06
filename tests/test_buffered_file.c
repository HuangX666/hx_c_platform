#include "utility_test.h"

typedef struct sink_state {
	hx_os_file_writer *owner;
	hx_os_event *entered, *release;
	hx_os_atomic hold, fail_write, fail_flush;
	size_t length;
	unsigned char output[50000];
	unsigned closes, flushes;
} sink_state;

static void await(hx_os_event *event)
{
	CHECK(hx_os_event_wait_until(
		      event, hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
}

static hx_os_status sink_write(void *user, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	sink_state *sink = user;
	size_t count = bytes > 3 ? 3 : bytes;
	int fail;

	CHECK(hx_os_file_writer_close_until(&sink->owner, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_DEADLOCK);
	if (hx_os_atomic_load(&sink->hold, HX_OS_ACQUIRE)) {
		hx_os_event_set(sink->entered);
		await(sink->release);
	}
	fail = (int)hx_os_atomic_load(&sink->fail_write, HX_OS_ACQUIRE);
	if (fail) {
		hx_os_atomic_store(&sink->fail_write, 0, HX_OS_RELEASE);
		if (count > 2)
			count = 2;
	}
	CHECK(sink->length + count <= sizeof(sink->output));
	memcpy(sink->output + sink->length, data, count);
	sink->length += count;
	*completed = count;
	if (fail) {
		if (error) {
			error->status = HX_OS_IO_ERROR;
			error->domain = HX_OS_ERROR_ERRNO;
			error->native_code = 123;
			error->cause_code = 456;
		}
		return HX_OS_IO_ERROR;
	}

	return HX_OS_OK;
}

static hx_os_status sink_flush(void *user, int persistent, hx_os_error *error)
{
	sink_state *sink = user;

	(void)error;
	++sink->flushes;
	if (persistent && hx_os_atomic_load(&sink->fail_flush, HX_OS_ACQUIRE)) {
		hx_os_atomic_store(&sink->fail_flush, 0, HX_OS_RELEASE);
		return HX_OS_IO_ERROR;
	}

	return HX_OS_OK;
}

static hx_os_status sink_close(void *user, hx_os_error *error)
{
	sink_state *sink = user;

	(void)error;
	++sink->closes;
	return HX_OS_OK;
}

static void make_sink(sink_state *sink, size_t capacity)
{
	hx_os_stream_callbacks callbacks = {
		NULL, sink_write, NULL, sink_flush, sink_close};
	hx_os_file_writer_options options = HX_OS_FILE_WRITER_OPTIONS_INIT;
	hx_os_stream *stream = NULL;

	memset(sink, 0, sizeof(*sink));
	options.queue_bytes = capacity;
	CHECK(hx_os_event_create(NULL, 1, 0, &sink->entered, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(NULL, 1, 0, &sink->release, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_create(NULL, &callbacks, sink, &stream, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_file_writer_from_stream(
		      NULL, &stream, &options, &sink->owner, NULL) == HX_OS_OK);
	CHECK(!stream);
}

static void destroy_sink(sink_state *sink)
{
	CHECK(!sink->owner && sink->closes == 1);
	hx_os_event_destroy(sink->entered);
	hx_os_event_destroy(sink->release);
}

static void test_full_and_close(void)
{
	sink_state sink;
	size_t accepted, pending;

	make_sink(&sink, 8);
	hx_os_atomic_store(&sink.hold, 1, HX_OS_RELEASE);
	CHECK(hx_os_file_writer_write_until(sink.owner, "abcdefgh", 8,
		      &accepted, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(accepted == 8);
	await(sink.entered);
	CHECK(hx_os_file_writer_pending(sink.owner, &pending, NULL) ==
		HX_OS_OK);
	CHECK(pending == 8);
	CHECK(hx_os_file_writer_write_until(sink.owner, "more", 4, &accepted,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		accepted == 0);
	CHECK(hx_os_file_writer_flush_until(sink.owner, 0, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_file_writer_close_until(
		      &sink.owner, HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		sink.owner);
	CHECK(hx_os_file_writer_write_until(sink.owner, "x", 1, &accepted,
		      HX_OS_DEADLINE_NOW, NULL) == HX_OS_CLOSED &&
		accepted == 0);

	hx_os_event_set(sink.release);
	CHECK(hx_os_file_writer_close_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.length == 8 && !memcmp(sink.output, "abcdefgh", 8));
	destroy_sink(&sink);
}

static void test_partial_and_retry(void)
{
	sink_state sink;
	size_t accepted, pending;
	hx_os_error error;

	make_sink(&sink, 8);
	hx_os_atomic_store(&sink.fail_write, 1, HX_OS_RELEASE);
	CHECK(hx_os_file_writer_write_until(sink.owner, "abcdefgh", 8,
		      &accepted, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_flush_until(sink.owner, 0,
		      hx_os_deadline_after_ms(5000), &error) == HX_OS_IO_ERROR);
	CHECK(error.native_code == 123 && error.cause_code == 456);
	CHECK(hx_os_file_writer_pending(sink.owner, &pending, NULL) ==
			HX_OS_IO_ERROR &&
		pending == 6);
	CHECK(hx_os_file_writer_close_until(&sink.owner, HX_OS_DEADLINE_NOW,
		      NULL) == HX_OS_IO_ERROR &&
		sink.owner);
	CHECK(hx_os_file_writer_retry(sink.owner, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_close_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.length == 8 && !memcmp(sink.output, "abcdefgh", 8));
	destroy_sink(&sink);

	make_sink(&sink, 8);
	hx_os_atomic_store(&sink.fail_flush, 1, HX_OS_RELEASE);
	CHECK(hx_os_file_writer_flush_until(sink.owner, 1,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_IO_ERROR);
	CHECK(hx_os_file_writer_retry(sink.owner, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_flush_until(sink.owner, 1,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_close_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.flushes == 3);
	destroy_sink(&sink);
}

static void test_abort(void)
{
	sink_state sink;
	size_t accepted;

	make_sink(&sink, 8);
	hx_os_atomic_store(&sink.hold, 1, HX_OS_RELEASE);
	CHECK(hx_os_file_writer_write_until(sink.owner, "abcdefghij", 10,
		      &accepted, HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	CHECK(accepted == 8);
	await(sink.entered);
	CHECK(hx_os_file_writer_abort_until(
		      &sink.owner, HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT &&
		sink.owner);
	hx_os_event_set(sink.release);
	CHECK(hx_os_file_writer_abort_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.length == 3 && !memcmp(sink.output, "abc", 3));
	destroy_sink(&sink);
}

static void *HX_OS_CALL stopped_producer(void *user)
{
	sink_state *sink = user;
	size_t accepted;

	CHECK(hx_os_file_writer_write_until(sink->owner, "more", 4, &accepted,
		      HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_CLOSED);
	CHECK(accepted == 0);

	return NULL;
}

static void test_stop(void)
{
	sink_state sink;
	hx_os_thread *producer_thread = NULL;
	size_t accepted;

	make_sink(&sink, 8);
	hx_os_atomic_store(&sink.hold, 1, HX_OS_RELEASE);
	CHECK(hx_os_file_writer_write_until(sink.owner, "abcdefgh", 8,
		      &accepted, HX_OS_DEADLINE_NOW, NULL) == HX_OS_OK);
	await(sink.entered);
	CHECK(hx_os_thread_create(NULL, stopped_producer, &sink,
		      &producer_thread, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_stop(sink.owner, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_stop(sink.owner, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(producer_thread,
		      hx_os_deadline_after_ms(5000), NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&producer_thread, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_close_until(
		      &sink.owner, HX_OS_DEADLINE_NOW, NULL) == HX_OS_TIMEOUT);
	hx_os_event_set(sink.release);
	CHECK(hx_os_file_writer_close_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.length == 8 && !memcmp(sink.output, "abcdefgh", 8));
	destroy_sink(&sink);
}

typedef struct producer {
	hx_os_file_writer *writer;
	hx_os_event *start;
	unsigned char value;
} producer;

static void *HX_OS_CALL produce(void *user)
{
	producer *state = user;
	unsigned char bytes[20001];
	size_t accepted;

	memset(bytes, state->value, sizeof(bytes));
	await(state->start);
	CHECK(hx_os_file_writer_write_until(state->writer, bytes, sizeof(bytes),
		      &accepted, hx_os_deadline_after_ms(5000),
		      NULL) == HX_OS_OK);
	CHECK(accepted == sizeof(bytes));

	return NULL;
}

static void test_producers(void)
{
	sink_state sink;
	producer first, second;
	hx_os_thread *a = NULL, *b = NULL;
	size_t i;

	make_sink(&sink, 17);
	first.writer = second.writer = sink.owner;
	first.start = second.start = sink.release;
	first.value = 'A';
	second.value = 'B';
	CHECK(hx_os_thread_create(NULL, produce, &first, &a, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(NULL, produce, &second, &b, NULL) ==
		HX_OS_OK);
	hx_os_event_set(sink.release);
	CHECK(hx_os_thread_join_until(a, hx_os_deadline_after_ms(5000), NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(b, hx_os_deadline_after_ms(5000), NULL,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&a, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&b, NULL) == HX_OS_OK);
	CHECK(hx_os_file_writer_flush_until(sink.owner, 0,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(sink.length == 40002 && sink.output[0] != sink.output[20001]);
	for (i = 0; i < 20001; ++i) {
		CHECK(sink.output[i] == sink.output[0]);
		CHECK(sink.output[i + 20001] == sink.output[20001]);
	}
	CHECK(hx_os_file_writer_close_until(&sink.owner,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	destroy_sink(&sink);
}

static void test_allocations_and_file(void)
{
	test_memory memory = {0, 0, 0};
	hx_os_context *context = test_context(&memory);
	hx_os_file_writer_options options = HX_OS_FILE_WRITER_OPTIONS_INIT;
	hx_os_file_writer *writer = NULL;
	hx_os_stream *stream = NULL;
	hx_os_status status;
	size_t i, accepted, before;
	hx_string text = HX_STRING_INIT;

	options.queue_bytes = 7;
	for (i = 1; i <= 8; ++i) {
		CHECK(hx_os_stream_memory(context, 64, &stream, NULL) ==
			HX_OS_OK);
		memory.fail = memory.calls + i;
		status = hx_os_file_writer_from_stream(
			context, &stream, &options, &writer, NULL);
		memory.fail = 0;
		if (status == HX_OS_OK) {
			CHECK(!stream);
			CHECK(hx_os_file_writer_close_until(&writer,
				      hx_os_deadline_after_ms(5000),
				      NULL) == HX_OS_OK);
		}
		else {
			CHECK(status == HX_OS_NO_MEMORY && !writer && stream);
			CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);
		}
		CHECK(hx_os_context_allocations(context) == 0);
	}
	CHECK(hx_os_file_writer_open_ex(context, "hx-buffered.txt",
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_TRUNCATE,
		      &options, &writer, NULL) == HX_OS_OK);
	before = memory.calls;
	CHECK(hx_os_file_writer_write_until(writer, "hello buffered file", 19,
		      &accepted, hx_os_deadline_after_ms(5000),
		      NULL) == HX_OS_OK);
	CHECK(accepted == 19);
	CHECK(hx_os_file_writer_flush_until(writer, 1,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(memory.calls == before);
	CHECK(hx_os_file_writer_close_until(&writer,
		      hx_os_deadline_after_ms(5000), NULL) == HX_OS_OK);
	CHECK(hx_os_file_read_text("hx-buffered.txt", &text, NULL) == HX_OS_OK);
	CHECK(!strcmp(hx_string_c_str(&text), "hello buffered file"));
	hx_string_destroy(&text);
	CHECK(hx_os_file_remove("hx-buffered.txt", NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
}

int main(void)
{
	if (!hx_os_file_writer_available()) {
		hx_os_file_writer *writer = NULL;

		CHECK(hx_os_file_writer_open("unused", &writer, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			!writer);
		return 0;
	}

	test_full_and_close();
	test_partial_and_retry();
	test_abort();
	test_stop();
	test_producers();
	test_allocations_and_file();
	puts("bounded background file writer contracts passed");

	return 0;
}
