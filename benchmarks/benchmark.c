#include "hx_os.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define ITERATIONS 1000000

static void report(const char *name, uint64_t start)
{
	uint64_t elapsed = hx_os_monotonic_ns() - start;
	printf("%s: %" PRIu64 " ns total, %.2f ns/operation\n", name, elapsed,
		(double)elapsed / ITERATIONS);
}

int main(void)
{
	hx_os_memory_pool *pool = NULL;
	hx_os_atomic atomic = HX_OS_ATOMIC_INIT(0);
	hx_string string = HX_STRING_INIT;
	uint64_t start;
	int i;
	start = hx_os_monotonic_ns();
	for (i = 0; i < ITERATIONS; ++i)
		(void)hx_os_atomic_fetch_add(&atomic, 1, HX_OS_RELAXED);
	report("atomic fetch_add", start);
	start = hx_os_monotonic_ns();
	for (i = 0; i < ITERATIONS; ++i) {
		hx_string_clear(&string);
		if (hx_string_append_cstr(
			    &string, "a short UTF-8 string", NULL) != HX_OS_OK)
			return 1;
	}
	report("string clear + append (capacity reused)", start);
	hx_string_destroy(&string);
	if (hx_os_capabilities() & HX_OS_CAP_MEMORY_POOLS) {
		if (hx_os_memory_pool_create(NULL, NULL, &pool, NULL) !=
			HX_OS_OK)
			return 1;
		start = hx_os_monotonic_ns();
		for (i = 0; i < ITERATIONS; ++i) {
			void *memory;
			if (hx_os_memory_pool_alloc(
				    pool, 128, 0, &memory, NULL) != HX_OS_OK)
				return 1;
			hx_os_memory_pool_free(pool, memory);
		}
		report("pool allocate + free (128 bytes, cached)", start);
		(void)hx_os_memory_pool_destroy(&pool, NULL);
	}
	printf("platform=%s cpu=%s capabilities=%" PRIu64 "\n",
		hx_os_platform_name(), hx_os_cpu_name(), hx_os_capabilities());
	puts("Development measurements; not an hx_c_log acceptance baseline.");
	return 0;
}
