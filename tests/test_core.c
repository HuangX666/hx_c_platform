#include "hx_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at line %d: %s\n",       \
				__LINE__, #value);                             \
			exit(1);                                               \
		}                                                              \
	} while (0)

typedef struct worker_data {
	hx_os_event *ready;
	hx_os_event *stop;
	hx_os_tls_key *key;
	hx_os_atomic cleaned;
} worker_data;

static void cleanup(void *value, void *user)
{
	worker_data *data = user;

	CHECK(value == data);

	(void)hx_os_atomic_fetch_add(&data->cleaned, 1, HX_OS_SEQ_CST);
}

static void *worker(void *user)
{
	worker_data *data = user;

	CHECK(hx_os_tls_set(data->key, data, NULL) == HX_OS_OK);

	hx_os_event_set(data->ready);
	CHECK(hx_os_event_wait_until(
		      data->stop, HX_OS_DEADLINE_INFINITE, NULL) == HX_OS_OK);

	return data;
}

int main(void)
{
	hx_os_context *context = NULL;
	hx_os_thread *thread = NULL;
	hx_os_error error;
	worker_data data;
	void *memory = NULL;
	void *result = NULL;
	size_t offset;
	uint64_t before;
	int64_t wall;

	memset(&data, 0, sizeof(data));
	CHECK(hx_os_context_create(NULL, &context, &error) == HX_OS_OK);
	CHECK(hx_os_monotonic_read(&before, &error) == HX_OS_OK);
	CHECK(hx_os_wall_time_read(&wall, &error) == HX_OS_OK);
	CHECK(wall > 0);
	CHECK(hx_os_memory_alloc(context, 71, 64, &memory, &error) == HX_OS_OK);
	CHECK(((uintptr_t)memory & 63) == 0);
	CHECK(hx_os_context_destroy(&context, &error) == HX_OS_BUSY);
	CHECK(context != NULL);

	hx_os_memory_free(memory);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_text_validate_utf8("hello", 5, &offset) == HX_OS_OK);
	CHECK(hx_os_text_validate_utf8("\xf0\x9f\x98\x80", 4, &offset) ==
		HX_OS_OK);
	CHECK(hx_os_text_validate_utf8("\xc0\x80", 2, &offset) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_text_validate_utf8("\xed\xa0\x80", 3, &offset) ==
		HX_OS_INVALID_ENCODING);
	CHECK(hx_os_event_create(context, 1, 0, &data.ready, NULL) == HX_OS_OK);
	CHECK(hx_os_event_create(context, 0, 0, &data.stop, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_key_create(context, cleanup, &data, &data.key, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_thread_create(context, worker, &data, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(data.ready, hx_os_deadline_after_ms(1000),
		      NULL) == HX_OS_OK);
	CHECK(hx_os_tls_key_destroy(&data.key, NULL) == HX_OS_BUSY);

	before = hx_os_monotonic_ns();
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(10),
		      &result, NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_monotonic_ns() >= before);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_BUSY);

	hx_os_event_set(data.stop);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(1000),
		      &result, NULL) == HX_OS_OK);
	CHECK(result == &data);
	CHECK(hx_os_atomic_load(&data.cleaned, HX_OS_ACQUIRE) == 1);
	CHECK(hx_os_thread_join_until(thread, 0, NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_key_destroy(&data.key, NULL) == HX_OS_OK);

	hx_os_event_destroy(data.ready);
	hx_os_event_destroy(data.stop);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	puts("core contracts passed");

	return 0;
}
