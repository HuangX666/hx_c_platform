#include "hx_os.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "check failed at line %d: %s\n",       \
				__LINE__, #value);                             \
			exit(1);                                               \
		}                                                              \
	} while (0)

int main(void)
{
	uint64_t capabilities = hx_os_capabilities();

	hx_os_error error;

	if (!(capabilities & HX_OS_CAP_NETWORK)) {
		hx_os_network *network = NULL;

		CHECK(hx_os_network_create(NULL, &network, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!network && error.status == HX_OS_NOT_SUPPORTED);
	}

	if (!(capabilities & HX_OS_CAP_LOOP)) {
		hx_os_loop *loop = NULL;

		CHECK(hx_os_loop_create(NULL, NULL, &loop, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!loop && error.status == HX_OS_NOT_SUPPORTED);
	}

	if (!(capabilities & HX_OS_CAP_THREAD_POOL)) {
		hx_os_thread_pool *pool = NULL;
		int inside = -1;

		CHECK(hx_os_thread_pool_inside(NULL, &inside) ==
				HX_OS_NOT_SUPPORTED &&
			inside == -1);
		CHECK(hx_os_thread_pool_create(NULL, NULL, &pool, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!pool && error.status == HX_OS_NOT_SUPPORTED);
	}

	if (!(capabilities & HX_OS_CAP_MEMORY_POOLS)) {
		hx_os_memory_pool *pool = NULL;
		hx_os_object_pool *objects = NULL;
		hx_os_arena *arena = NULL;

		CHECK(hx_os_memory_pool_create(NULL, NULL, &pool, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_object_pool_create(NULL, 32, 0, 2, &objects,
			      &error) == HX_OS_NOT_SUPPORTED);
		CHECK(hx_os_arena_create(NULL, 4096, 0, &arena, &error) ==
			HX_OS_NOT_SUPPORTED);
		CHECK(!pool && !objects && !arena);
	}

	if (!(capabilities & HX_OS_CAP_LEGACY_TEXT)) {
		hx_os_text_converter *converter = NULL;

		CHECK(!hx_os_encoding_supported(HX_OS_GBK));
		CHECK(hx_os_text_converter_create(NULL, HX_OS_UTF8, HX_OS_GBK,
			      HX_OS_TEXT_STRICT, &converter,
			      &error) == HX_OS_NOT_SUPPORTED);
		CHECK(!converter && error.status == HX_OS_NOT_SUPPORTED);
	}

	puts("optional feature contracts passed");

	return 0;
}
