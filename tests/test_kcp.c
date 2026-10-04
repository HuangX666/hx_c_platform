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

typedef struct packet {
	struct packet *next;
	size_t bytes;
	unsigned char data[1400];
} packet;

typedef struct link {
	packet *head;
	size_t serial;
	int losses;
} link;

typedef struct allocator {
	int allow;
	size_t live;
} allocator;

static void *allocate(size_t bytes, void *user)
{
	allocator *state = user;
	void *result;

	if (!state->allow)
		return NULL;

	result = malloc(bytes);
	if (result)
		++state->live;

	return result;
}

static void deallocate(void *memory, void *user)
{
	allocator *state = user;

	CHECK(state->live > 0);

	--state->live;

	free(memory);
}

static hx_os_status output(
	const void *data, size_t bytes, void *user, hx_os_error *error)
{
	link *path = user;
	int copies, index;

	(void)error;
	CHECK(bytes <= 1400);

	++path->serial;
	if (path->losses && path->serial % 7 == 0)
		return HX_OS_OK;

	copies = path->losses && path->serial % 13 == 0 ? 2 : 1;

	for (index = 0; index < copies; ++index) {
		packet *item = malloc(sizeof(*item));
		CHECK(item);

		item->bytes = bytes;
		memcpy(item->data, data, bytes);
		/* A stack deliberately reverses the delivery order. */
		item->next = path->head;
		path->head = item;
	}

	return HX_OS_OK;
}

static void deliver(link *path, hx_os_kcp *destination)
{
	while (path->head) {
		packet *item = path->head;

		path->head = item->next;
		CHECK(hx_os_kcp_input(destination, item->data, item->bytes,
			      NULL) == HX_OS_OK);

		free(item);
	}
}

static void discard(link *path)
{
	while (path->head) {
		packet *item = path->head;

		path->head = item->next;

		free(item);
	}
}

static void test_delivery(void)
{
	hx_os_context_options context_options;
	hx_os_context *context = NULL;
	hx_os_kcp *first = NULL, *second = NULL;
	hx_os_kcp_options options;
	allocator memory = {1, 0};
	link forward = {0}, backward = {0};
	uint64_t now;
	unsigned int index, got_first = 0, got_second = 0;
	unsigned char data[1024], received[1024];
	size_t bytes;

	hx_os_context_options_init(&context_options);
	context_options.allocator.allocate = allocate;
	context_options.allocator.deallocate = deallocate;
	context_options.allocator.user = &memory;
	CHECK(hx_os_context_create(&context_options, &context, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_kcp_options_init(&options, sizeof(options)) == HX_OS_OK);

	options.max_send_segments = 256;
	CHECK(hx_os_kcp_create(context, 42, &options, output, &forward, &first,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_kcp_create(context, 42, &options, output, &backward,
		      &second, NULL) == HX_OS_OK);
	CHECK(hx_os_kcp_reserved_bytes(first) < 2 * 1024 * 1024);

	/* No KCP operation may allocate from the context after creation. */
	memory.allow = 0;
	forward.losses = backward.losses = 1;

	for (index = 0; index < 100; ++index) {
		memset(data, (int)index, sizeof(data));
		CHECK(hx_os_kcp_send(first, data, sizeof(data), NULL) ==
			HX_OS_OK);
		CHECK(hx_os_kcp_send(second, data, sizeof(data), NULL) ==
			HX_OS_OK);
	}

	/* Start close to KCP's 32-bit clock wrap. */
	for (now = UINT64_C(0xfffffff0); now < UINT64_C(0x100007520);
		now += 10) {
		CHECK(hx_os_kcp_update(first, now, NULL) == HX_OS_OK);
		CHECK(hx_os_kcp_update(second, now, NULL) == HX_OS_OK);

		deliver(&forward, second);
		deliver(&backward, first);

		while (hx_os_kcp_receive(first, received, sizeof(received),
			       &bytes, NULL) == HX_OS_OK) {
			CHECK(bytes == sizeof(received) && got_first < 100);

			memset(data, (int)got_first++, sizeof(data));
			CHECK(!memcmp(data, received, sizeof(data)));
		}

		while (hx_os_kcp_receive(second, received, sizeof(received),
			       &bytes, NULL) == HX_OS_OK) {
			CHECK(bytes == sizeof(received) && got_second < 100);

			memset(data, (int)got_second++, sizeof(data));
			CHECK(!memcmp(data, received, sizeof(data)));
		}

		if (got_first == 100 && got_second == 100 &&
			!hx_os_kcp_pending_segments(first) &&
			!hx_os_kcp_pending_segments(second))
			break;
	}

	CHECK(got_first == 100 && got_second == 100);
	CHECK(!hx_os_kcp_pending_segments(first));
	CHECK(!hx_os_kcp_pending_segments(second));
	CHECK(hx_os_kcp_update(first, 0, NULL) == HX_OS_INVALID);

	discard(&forward);
	discard(&backward);

	hx_os_kcp_destroy(first);
	hx_os_kcp_destroy(second);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_kcp_create(context, 42, NULL, output, &forward, &first,
		      NULL) == HX_OS_NO_MEMORY &&
		!first);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(memory.live == 0);
}

static void test_limits(void)
{
	hx_os_kcp_options options;
	hx_os_kcp *sender = NULL, *receiver = NULL;
	link forward = {0}, backward = {0};
	unsigned char malformed[25] = {0};
	unsigned char data[65536], result[65536];
	size_t bytes = 0, count = 0;
	uint64_t now;

	CHECK(hx_os_kcp_options_init(&options, sizeof(options)) == HX_OS_OK);
	CHECK(hx_os_kcp_create(NULL, 42, &options, output, &forward, &sender,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_kcp_create(NULL, 42, &options, output, &backward, &receiver,
		      NULL) == HX_OS_OK);

	malformed[0] = 42;
	malformed[4] = 81;
	CHECK(hx_os_kcp_input(receiver, malformed, sizeof(malformed), NULL) ==
		HX_OS_SYNTAX);

	memset(data, 0x71, sizeof(data));
	CHECK(hx_os_kcp_send(sender, data, sizeof(data), NULL) == HX_OS_OK);

	for (now = 0; now < 5000; now += 10) {
		CHECK(hx_os_kcp_update(sender, now, NULL) == HX_OS_OK);
		CHECK(hx_os_kcp_update(receiver, now, NULL) == HX_OS_OK);

		deliver(&forward, receiver);
		deliver(&backward, sender);
		if (hx_os_kcp_receive(receiver, NULL, 0, &bytes, NULL) ==
			HX_OS_OUTPUT_FULL)
			break;
	}

	CHECK(bytes == sizeof(data));
	CHECK(hx_os_kcp_receive(receiver, result, sizeof(result), &bytes,
		      NULL) == HX_OS_OK);
	CHECK(bytes == sizeof(data) && !memcmp(data, result, bytes));

	while (hx_os_kcp_send(sender, data, sizeof(data), NULL) == HX_OS_OK)
		++count;

	CHECK(count < 3 && hx_os_kcp_pending_segments(sender) <= 128);

	discard(&forward);
	discard(&backward);

	hx_os_kcp_destroy(sender);
	hx_os_kcp_destroy(receiver);
}

int main(void)
{
	if (!(hx_os_capabilities() & HX_OS_CAP_KCP)) {
		hx_os_kcp *value = NULL;

		CHECK(hx_os_kcp_create(NULL, 1, NULL, output, NULL, &value,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!value);

		return 0;
	}

	test_delivery();
	test_limits();
	puts("KCP loss, reorder, duplicate, bounds and allocator checks "
	     "passed");

	return 0;
}
