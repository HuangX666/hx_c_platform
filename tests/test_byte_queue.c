#include "utility_test.h"

static void test_ring(void)
{
	hx_os_byte_queue *queue = NULL;
	unsigned char input[64], output[64], model[64];
	size_t count = 0, i;
	uint32_t random = 123;

	for (i = 0; i < sizeof(input); ++i)
		input[i] = (unsigned char)i;

	CHECK(hx_os_byte_queue_create(NULL, sizeof(model), &queue, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_byte_queue_write(queue, NULL, 0, NULL) == HX_OS_OK);
	CHECK(hx_os_byte_queue_read(queue, NULL, 0, NULL) == HX_OS_OK);

	for (i = 0; i < 10000; ++i) {
		size_t bytes;
		hx_os_status status;

		random = random * UINT32_C(1664525) + UINT32_C(1013904223);
		bytes = (random >> 8) % 17;
		if (random & 1) {
			status = hx_os_byte_queue_write(
				queue, input, bytes, NULL);
			if (bytes > sizeof(model) - count)
				CHECK(status == HX_OS_LIMIT);
			else {
				CHECK(status == HX_OS_OK);
				memcpy(model + count, input, bytes);
				count += bytes;
			}
		}
		else {
			memset(output, 0xff, sizeof(output));
			status = hx_os_byte_queue_read(
				queue, output, bytes, NULL);
			if (bytes > count) {
				CHECK(status == HX_OS_NEED_INPUT);
				CHECK(output[0] == 0xff);
			}
			else {
				CHECK(status == HX_OS_OK);
				CHECK(!memcmp(output, model, bytes));
				count -= bytes;
				memmove(model, model + bytes, count);
			}
		}

		CHECK(hx_os_byte_queue_size(queue) == count);
		CHECK(hx_os_byte_queue_peek(queue, 0, output, count, NULL) ==
			HX_OS_OK);
		CHECK(!memcmp(output, model, count));
		if (count) {
			CHECK(hx_os_byte_queue_peek(queue, count - 1, output, 1,
				      NULL) == HX_OS_OK);
			CHECK(output[0] == model[count - 1]);
		}
	}

	CHECK(hx_os_byte_queue_read(queue, NULL, count, NULL) == HX_OS_OK);
	CHECK(hx_os_byte_queue_write(queue, input, 1, NULL) == HX_OS_OK);
	CHECK(hx_os_byte_queue_write(queue, hx_os_sequence_at(queue, 0), 1,
		      NULL) == HX_OS_INVALID);
	CHECK(hx_os_byte_queue_peek(queue, 2, output, 0, NULL) ==
		HX_OS_NEED_INPUT);
	CHECK(hx_os_byte_queue_destroy(&queue, NULL) == HX_OS_OK && !queue);
}

static void test_rollback(void)
{
	test_memory memory = {0, 0, 0};
	hx_os_context *context = test_context(&memory);
	hx_os_byte_queue *queue = NULL;

	CHECK(hx_os_byte_queue_create(context, 8, &queue, NULL) == HX_OS_OK);
	memory.fail = memory.calls + 1;
	CHECK(hx_os_byte_queue_write(queue, "abcd", 4, NULL) ==
		HX_OS_NO_MEMORY);
	CHECK(hx_os_byte_queue_size(queue) == 0);
	memory.fail = 0;
	CHECK(hx_os_byte_queue_write(queue, "abcd", 4, NULL) == HX_OS_OK);
	memory.fail = memory.calls + 1;
	CHECK(hx_os_byte_queue_write(queue, "efgh", 4, NULL) ==
		HX_OS_NO_MEMORY);
	CHECK(hx_os_byte_queue_size(queue) == 4);
	memory.fail = 0;
	CHECK(hx_os_byte_queue_destroy(&queue, NULL) == HX_OS_OK);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_CONTAINERS)) {
		hx_os_byte_queue *queue = NULL;

		CHECK(hx_os_byte_queue_create(NULL, 0, &queue, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}

	test_ring();
	test_rollback();
	puts("bulk byte queue contracts passed");

	return 0;
}
