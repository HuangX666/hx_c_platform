#include "hx_os.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void report(const char *name, uint64_t start, size_t iterations)
{
	uint64_t elapsed = hx_os_monotonic_ns() - start;
	printf("%s: %" PRIu64 " ns total, %.2f ns/operation\n", name, elapsed,
		(double)elapsed / iterations);
}

int main(int argc, char **argv)
{
	hx_os_memory_pool *pool = NULL;

	hx_os_atomic atomic = HX_OS_ATOMIC_INIT(0);

	hx_string string = HX_STRING_INIT;
	uint64_t start;
	size_t i, iterations = 1000000;

	if (argc > 1) {
		char *end;
		unsigned long value;

		errno = 0;
		value = strtoul(argv[1], &end, 10);
		if (argc != 2 || errno || !argv[1][0] || argv[1][0] == '-' ||
			*end || !value || value > 100000000) {
			fprintf(stderr,
				"Usage: %s [iterations: 1..100000000]\n",
				argv[0]);
			return 1;
		}
		iterations = (size_t)value;
	}
	printf("iterations=%zu\n", iterations);

	start = hx_os_monotonic_ns();

	for (i = 0; i < iterations; ++i)
		(void)hx_os_atomic_fetch_add(&atomic, 1, HX_OS_RELAXED);

	report("atomic fetch_add", start, iterations);
	start = hx_os_monotonic_ns();

	for (i = 0; i < iterations; ++i) {
		hx_string_clear(&string);
		if (hx_string_append_cstr(
			    &string, "a short UTF-8 string", NULL) != HX_OS_OK)
			return 1;
	}

	report("string clear + append (capacity reused)", start, iterations);

	hx_string_destroy(&string);
	if (hx_os_capabilities() & HX_OS_CAP_MEMORY_POOLS) {
		if (hx_os_memory_pool_create(NULL, NULL, &pool, NULL) !=
			HX_OS_OK)
			return 1;
		start = hx_os_monotonic_ns();

		for (i = 0; i < iterations; ++i) {
			void *memory;

			if (hx_os_memory_pool_alloc(
				    pool, 128, 0, &memory, NULL) != HX_OS_OK)
				return 1;
			hx_os_memory_pool_free(pool, memory);
		}

		report("pool allocate + free (128 bytes, cached)", start,
			iterations);

		(void)hx_os_memory_pool_destroy(&pool, NULL);
	}

	printf("platform=%s cpu=%s capabilities=%" PRIu64 "\n",
		hx_os_platform_name(), hx_os_cpu_name(), hx_os_capabilities());
	puts("Development measurements; not an hx_c_log acceptance baseline.");

	return 0;
}
