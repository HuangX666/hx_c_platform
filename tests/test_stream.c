#include "utility_test.h"
#include <math.h>

static void test_numbers(void)
{
	hx_os_stream *stream = NULL;
	const void *data;
	const unsigned char expected[] = {0x12, 0x34, 0x56, 0xfe, 0xff, 0xff,
		0xff, 0x3f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00};
	size_t bytes;
	uint64_t position;
	uint32_t integer;
	int32_t negative;
	int64_t minimum;
	float single;
	double real;

	CHECK(hx_os_stream_memory(NULL, 64, &stream, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_write_u24(
		      stream, HX_OS_BIG_ENDIAN, 0x123456, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_write_i32(stream, HX_OS_LITTLE_ENDIAN, -2, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_stream_write_f32(stream, HX_OS_BIG_ENDIAN, 1.0f, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_stream_write_f64(stream, HX_OS_BIG_ENDIAN, -0.0, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_stream_data(stream, &data, &bytes, NULL) == HX_OS_OK);
	CHECK(bytes == sizeof(expected) && !memcmp(data, expected, bytes));
	CHECK(hx_os_stream_write_u24(stream, HX_OS_BIG_ENDIAN, 0x1000000,
		      NULL) == HX_OS_RANGE);
	CHECK(hx_os_stream_seek(stream, 0, HX_OS_SEEK_BEGIN, &position, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_stream_read_u24(stream, HX_OS_BIG_ENDIAN, &integer, NULL) ==
			HX_OS_OK &&
		integer == 0x123456);
	CHECK(hx_os_stream_read_i32(stream, HX_OS_LITTLE_ENDIAN, &negative,
		      NULL) == HX_OS_OK &&
		negative == -2);
	CHECK(hx_os_stream_read_f32(stream, HX_OS_BIG_ENDIAN, &single, NULL) ==
			HX_OS_OK &&
		single == 1.0f);
	CHECK(hx_os_stream_read_f64(stream, HX_OS_BIG_ENDIAN, &real, NULL) ==
			HX_OS_OK &&
		real == 0.0 && signbit(real));
	CHECK(hx_os_stream_write_i64(
		      stream, HX_OS_BIG_ENDIAN, INT64_MIN, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_seek(stream, -8, HX_OS_SEEK_CURRENT, &position,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_stream_read_i64(stream, HX_OS_BIG_ENDIAN, &minimum, NULL) ==
			HX_OS_OK &&
		minimum == INT64_MIN);
	CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);
}

static void test_memory_stream(void)
{
	test_memory memory = {0, 0, 0};
	hx_os_context *context = test_context(&memory);
	hx_os_stream *stream = NULL;
	const void *data;
	size_t bytes, completed;
	uint64_t position;
	uint32_t value = 123;

	CHECK(hx_os_stream_memory(context, 8, &stream, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_seek(stream, 5, HX_OS_SEEK_BEGIN, &position, NULL) ==
		HX_OS_OK);
	memory.fail = memory.calls + 1;
	CHECK(hx_os_stream_write_all(stream, "xy", 2, &completed, NULL) ==
			HX_OS_NO_MEMORY &&
		completed == 0);
	memory.fail = 0;
	CHECK(hx_os_stream_write_all(stream, "xy", 2, &completed, NULL) ==
			HX_OS_OK &&
		completed == 2);
	CHECK(hx_os_stream_data(stream, &data, &bytes, NULL) == HX_OS_OK);
	CHECK(bytes == 7 && !memcmp(data, "\0\0\0\0\0xy", 7));
	CHECK(hx_os_stream_write_all(stream, "ab", 2, &completed, NULL) ==
			HX_OS_LIMIT &&
		completed == 0);
	CHECK(hx_os_stream_seek(stream, INT64_MIN, HX_OS_SEEK_CURRENT,
		      &position, NULL) == HX_OS_RANGE);
	CHECK(hx_os_stream_write_all(stream, data, 1, &completed, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_stream_flush(stream, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_flush(stream, 1, NULL) == HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);

	CHECK(hx_os_stream_from_memory(context, "xy", 2, &stream, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_stream_read_u32(stream, HX_OS_BIG_ENDIAN, &value, NULL) ==
			HX_OS_TRUNCATED_INPUT &&
		value == 123);
	CHECK(hx_os_stream_tell(stream, &position, NULL) == HX_OS_OK &&
		position == 2);
	CHECK(hx_os_stream_read_u32(stream, HX_OS_BIG_ENDIAN, &value, NULL) ==
			HX_OS_EOF &&
		value == 123);
	CHECK(hx_os_stream_write_u8(stream, HX_OS_BIG_ENDIAN, 1, NULL) ==
		HX_OS_NOT_SUPPORTED);
	CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
}

typedef struct short_backend {
	hx_os_stream *owner;
	size_t position;
	unsigned char written[8];
	int stalled;
	int closed;
} short_backend;

static hx_os_status short_read(void *user, void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	short_backend *backend = user;
	size_t count = bytes > 2 ? 2 : bytes;

	CHECK(hx_os_stream_close(&backend->owner, NULL) == HX_OS_BUSY);
	*completed = 0;
	if (backend->stalled)
		return HX_OS_OK;
	if (backend->position >= 4) {
		if (error) {
			error->status = HX_OS_IO_ERROR;
			error->domain = HX_OS_ERROR_ERRNO;
			error->native_code = 123;
			error->cause_code = 456;
		}
		return HX_OS_IO_ERROR;
	}

	memset(data, 'a', count);
	backend->position += count;
	*completed = count;

	return HX_OS_OK;
}

static hx_os_status short_write(void *user, const void *data, size_t bytes,
	size_t *completed, hx_os_error *error)
{
	short_backend *backend = user;
	size_t count = bytes > 2 ? 2 : bytes;

	*completed = 0;
	if (backend->stalled)
		return HX_OS_OK;
	CHECK(backend->position + count <= sizeof(backend->written));
	memcpy(backend->written + backend->position, data, count);
	backend->position += count;
	*completed = count;
	if (backend->position == 4) {
		if (error) {
			error->status = HX_OS_IO_ERROR;
			error->domain = HX_OS_ERROR_ERRNO;
			error->native_code = 789;
			error->cause_code = 0;
		}
		return HX_OS_IO_ERROR;
	}

	return HX_OS_OK;
}

static hx_os_status short_close(void *user, hx_os_error *error)
{
	short_backend *backend = user;

	(void)error;
	++backend->closed;
	return HX_OS_OK;
}

static void test_callbacks(void)
{
	short_backend backend = {NULL, 0, {0}, 0, 0};
	hx_os_stream_callbacks callbacks = {
		short_read, short_write, NULL, NULL, short_close};
	char output[8];
	size_t completed;
	hx_os_error error;

	CHECK(hx_os_stream_create(NULL, &callbacks, &backend, &backend.owner,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_stream_read_exact(backend.owner, output, 8, &completed,
		      &error) == HX_OS_IO_ERROR);
	CHECK(completed == 4 && !memcmp(output, "aaaa", 4));
	CHECK(error.native_code == 123 && error.cause_code == 456);
	backend.stalled = 1;
	CHECK(hx_os_stream_read_exact(backend.owner, output, 1, &completed,
		      NULL) == HX_OS_IO_ERROR &&
		completed == 0);
	CHECK(hx_os_stream_write_all(backend.owner, "x", 1, &completed, NULL) ==
			HX_OS_IO_ERROR &&
		completed == 0);
	backend.position = 0;
	backend.stalled = 0;
	CHECK(hx_os_stream_write_all(backend.owner, "abcdefgh", 8, &completed,
		      &error) == HX_OS_IO_ERROR &&
		completed == 4);
	CHECK(error.native_code == 789 && !memcmp(backend.written, "abcd", 4));
	CHECK(hx_os_stream_write_all(
		      backend.owner, "efgh", 4, &completed, NULL) == HX_OS_OK &&
		completed == 4);
	CHECK(!memcmp(backend.written, "abcdefgh", 8));
	CHECK(hx_os_stream_close(&backend.owner, NULL) == HX_OS_OK);
	CHECK(backend.closed == 1 && !backend.owner);
}

static void test_file(void)
{
	hx_os_file *file = NULL;
	hx_os_stream *stream = NULL;
	uint32_t value;

	CHECK(hx_os_file_open(NULL, "hx-stream.bin",
		      HX_OS_FILE_WRITE | HX_OS_FILE_CREATE |
			      HX_OS_FILE_TRUNCATE,
		      &file, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_from_file(NULL, &file, &stream, NULL) == HX_OS_OK);
	CHECK(!file);
	CHECK(hx_os_stream_write_u32(
		      stream, HX_OS_BIG_ENDIAN, 0x12345678, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_flush(stream, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);
	CHECK(hx_os_stream_open(NULL, "hx-stream.bin", HX_OS_FILE_READ, &stream,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_stream_read_u32(stream, HX_OS_BIG_ENDIAN, &value, NULL) ==
			HX_OS_OK &&
		value == 0x12345678);
	CHECK(hx_os_stream_close(&stream, NULL) == HX_OS_OK);
	CHECK(hx_os_file_remove("hx-stream.bin", NULL) == HX_OS_OK);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_STREAM)) {
		hx_os_stream *stream = NULL;

		CHECK(hx_os_stream_memory(NULL, 0, &stream, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}

	test_numbers();
	test_memory_stream();
	test_callbacks();
	test_file();
	puts("binary stream and endian contracts passed");

	return 0;
}
