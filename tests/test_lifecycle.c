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

typedef struct allocation_state {
	hx_os_atomic calls;
	hx_os_atomic fail_at;
	hx_os_atomic live;
	hx_os_memory_pool *probe_pool;
	hx_os_status probe_status;
	int probe;
} allocation_state;

static void *allocate(size_t bytes, void *user)
{
	allocation_state *state = user;
	int64_t call =
		hx_os_atomic_fetch_add(&state->calls, 1, HX_OS_RELAXED) + 1;
	void *memory;

	if (call == hx_os_atomic_load(&state->fail_at, HX_OS_RELAXED))
		return NULL;

	memory = malloc(bytes);
	if (memory)
		(void)hx_os_atomic_fetch_add(&state->live, 1, HX_OS_RELAXED);

	return memory;
}

static void deallocate(void *memory, void *user)
{
	allocation_state *state = user;

	if (memory) {
		if (state->probe) {
			void *attempt = NULL;

			state->probe = 0;
			state->probe_status = hx_os_memory_pool_alloc(
				state->probe_pool, 200000, 0, &attempt, NULL);
			if (attempt)
				hx_os_memory_pool_free(
					state->probe_pool, attempt);
		}

		free(memory);
		(void)hx_os_atomic_fetch_add(&state->live, -1, HX_OS_RELAXED);
	}
}

static void fail_after(allocation_state *state, int64_t count)
{
	hx_os_atomic_store(&state->fail_at,
		hx_os_atomic_load(&state->calls, HX_OS_RELAXED) + count,
		HX_OS_RELAXED);
}

static void test_failures(hx_os_context *context, allocation_state *state)
{
	hx_os_status status;
	hx_string string;
	int64_t i;

	hx_string_init(&string, context);
	CHECK(hx_string_assign_cstr(&string, "unchanged", NULL) == HX_OS_OK);

	fail_after(state, 1);
	CHECK(hx_string_reserve(&string, 4096, NULL) == HX_OS_NO_MEMORY);
	CHECK(!strcmp(hx_string_c_str(&string), "unchanged"));

	hx_string_destroy(&string);
	if (hx_os_capabilities() & HX_OS_CAP_THREAD_POOL) {
		for (i = 1; i < 32; ++i) {
			hx_os_thread_pool *pool;

			fail_after(state, i);

			status = hx_os_thread_pool_create(
				context, NULL, &pool, NULL);
			if (status == HX_OS_OK) {
				CHECK(hx_os_thread_pool_close_until(pool,
					      HX_OS_POOL_DRAIN,
					      hx_os_deadline_after_ms(1000),
					      NULL) == HX_OS_OK);
				CHECK(hx_os_thread_pool_destroy(&pool, NULL) ==
					HX_OS_OK);
			}
			else
				CHECK(status == HX_OS_NO_MEMORY && !pool);

			CHECK(hx_os_context_allocations(context) == 0);
			if (status == HX_OS_OK)
				break;
		}

		CHECK(i < 32);
	}

	if (hx_os_capabilities() & HX_OS_CAP_LOOP) {
		hx_os_network *network;

		hx_os_atomic_store(&state->fail_at, 0, HX_OS_RELAXED);
		CHECK(hx_os_network_create(context, &network, NULL) ==
			HX_OS_OK);

		for (i = 1; i < 16; ++i) {
			hx_os_loop *loop;

			fail_after(state, i);

			status = hx_os_loop_create(network, NULL, &loop, NULL);
			if (status == HX_OS_OK) {
				CHECK(hx_os_loop_close_until(loop,
					      hx_os_deadline_after_ms(1000),
					      NULL) == HX_OS_OK);
				CHECK(hx_os_loop_destroy(&loop, NULL) ==
					HX_OS_OK);
			}
			else
				CHECK(status == HX_OS_NO_MEMORY && !loop);

			CHECK(hx_os_context_allocations(context) == 1);
			if (status == HX_OS_OK)
				break;
		}

		CHECK(i < 16);
		CHECK(hx_os_network_destroy(&network, NULL) == HX_OS_OK);
	}

	hx_os_atomic_store(&state->fail_at, 0, HX_OS_RELAXED);
}

typedef struct tls_data {
	hx_os_tls_key *key;
	hx_os_event *entered;
	hx_os_event *release;
} tls_data;

static void tls_cleanup(void *value, void *user)
{
	tls_data *data = user;

	CHECK(value == data);

	hx_os_event_set(data->entered);
	CHECK(hx_os_event_wait_until(data->release, HX_OS_DEADLINE_INFINITE,
		      NULL) == HX_OS_OK);
}

static void *tls_worker(void *user)
{
	tls_data *data = user;

	CHECK(hx_os_tls_set(data->key, data, NULL) == HX_OS_OK);

	return NULL;
}

static void test_tls(hx_os_context *context)
{
	tls_data data;
	hx_os_thread *thread;

	CHECK(hx_os_event_create(context, 1, 0, &data.entered, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_create(context, 1, 0, &data.release, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_tls_key_create(context, tls_cleanup, &data, &data.key,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_thread_create(context, tls_worker, &data, &thread, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_event_wait_until(data.entered,
		      hx_os_deadline_after_ms(1000), NULL) == HX_OS_OK);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(5), NULL,
		      NULL) == HX_OS_TIMEOUT);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_BUSY);
	CHECK(hx_os_tls_key_destroy(&data.key, NULL) == HX_OS_BUSY);

	hx_os_event_set(data.release);
	CHECK(hx_os_thread_join_until(thread, hx_os_deadline_after_ms(1000),
		      NULL, NULL) == HX_OS_OK);
	CHECK(hx_os_thread_destroy(&thread, NULL) == HX_OS_OK);
	CHECK(hx_os_tls_key_destroy(&data.key, NULL) == HX_OS_OK);

	hx_os_event_destroy(data.entered);
	hx_os_event_destroy(data.release);
}

static void test_release_budget(hx_os_context *context, allocation_state *state)
{
	hx_os_memory_pool_options options;
	void *memory;

	hx_os_memory_pool_options_init(&options);
	options.maximum_bytes = 300000;
	options.cached_bytes = 0;
	CHECK(hx_os_memory_pool_create(
		      context, &options, &state->probe_pool, NULL) == HX_OS_OK);
	CHECK(hx_os_memory_pool_alloc(
		      state->probe_pool, 200000, 0, &memory, NULL) == HX_OS_OK);

	state->probe = 1;

	hx_os_memory_pool_free(state->probe_pool, memory);
	CHECK(state->probe == 0 && state->probe_status == HX_OS_LIMIT);
	CHECK(hx_os_memory_pool_destroy(&state->probe_pool, NULL) == HX_OS_OK);
}

typedef struct memory_worker_data {
	hx_os_memory_pool *pool;
} memory_worker_data;

static void *memory_worker(void *user)
{
	memory_worker_data *data = user;
	size_t i;

	for (i = 0; i < 2000; ++i) {
		void *memory;
		size_t bytes = i % 2048 + 1;

		CHECK(hx_os_memory_pool_alloc(data->pool, bytes, 64, &memory,
			      NULL) == HX_OS_OK);

		memset(memory, 0xa5, bytes);
		CHECK(hx_os_memory_pool_realloc(data->pool, &memory, bytes + 43,
			      NULL) == HX_OS_OK);
		CHECK(((unsigned char *)memory)[bytes - 1] == 0xa5);

		hx_os_memory_pool_free(data->pool, memory);
		if (i % 17 == 0)
			hx_os_memory_pool_collect(data->pool);
	}

	return NULL;
}

static void test_concurrent_memory(hx_os_context *context)
{
	memory_worker_data data;
	hx_os_thread *threads[4];
	size_t i;

	CHECK(hx_os_memory_pool_create(context, NULL, &data.pool, NULL) ==
		HX_OS_OK);

	for (i = 0; i < 4; ++i)
		CHECK(hx_os_thread_create(context, memory_worker, &data,
			      &threads[i], NULL) == HX_OS_OK);

	for (i = 0; i < 4; ++i) {
		CHECK(hx_os_thread_join_until(threads[i],
			      hx_os_deadline_after_ms(5000), NULL,
			      NULL) == HX_OS_OK);
		CHECK(hx_os_thread_destroy(&threads[i], NULL) == HX_OS_OK);
	}

	CHECK(hx_os_memory_pool_destroy(&data.pool, NULL) == HX_OS_OK);
}

int main(void)
{
	allocation_state state;
	hx_os_context_options options;
	hx_os_context *context;

	memset(&state, 0, sizeof(state));
	hx_os_context_options_init(&options);
	options.allocator.allocate = allocate;
	options.allocator.deallocate = deallocate;
	options.allocator.user = &state;
	CHECK(hx_os_context_create(&options, &context, NULL) == HX_OS_OK);

	test_failures(context, &state);
	test_tls(context);
	if (hx_os_capabilities() & HX_OS_CAP_MEMORY_POOLS) {
		test_release_budget(context, &state);
		test_concurrent_memory(context);
	}

	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	CHECK(hx_os_atomic_load(&state.live, HX_OS_RELAXED) == 0);

	puts("allocation, TLS and concurrent lifetime contracts passed");

	return 0;
}
