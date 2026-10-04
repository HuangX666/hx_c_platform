#include <hx_os.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#endif
static uint64_t now(void)
{
#if defined(_WIN32)
	LARGE_INTEGER ticks, frequency;

	if (!QueryPerformanceFrequency(&frequency) ||
		!QueryPerformanceCounter(&ticks) || frequency.QuadPart <= 0)
		exit(1);
	return (uint64_t)ticks.QuadPart / (uint64_t)frequency.QuadPart *
		UINT64_C(1000000000) +
		(uint64_t)((double)((uint64_t)ticks.QuadPart %
				   (uint64_t)frequency.QuadPart) *
			1000000000.0 / (double)frequency.QuadPart);
#else
	uint64_t value;

	if (hx_os_monotonic_read(&value, NULL) != HX_OS_OK)
		exit(1);

	return value;
#endif
}

static void report(const char *name, uint64_t start, size_t iterations)
{
	printf("%s iterations=%lu wall_ns_per_op=%.2f\n", name,
		(unsigned long)iterations,
		(double)(now() - start) / (double)iterations);
}

static hx_os_status HX_OS_CALL count_call(
	hx_os_calldata *data, void *user, hx_os_error *error)
{
	uint64_t *count = user;

	(void)data;
	(void)error;
	++*count;

	return HX_OS_OK;
}

typedef struct hx_bench_point {
	double x;
	double y;
} hx_bench_point;
HX_VARIANT_DEFINE_TRIVIAL_TYPE(hx_bench_point_type, hx_bench_point);

int main(void)
{
	const size_t iterations = 100000;
	size_t i;
	uint64_t start, value = 0;

	uint64_t caps = hx_os_capabilities();
	hx_os_atomic atomic = HX_OS_ATOMIC_INIT(0);

	hx_string text = HX_STRING_INIT;

	printf("platform=%s cpu=%s pointer_bits=%u capabilities=%" PRIx64 "\n",
		hx_os_platform_name(), hx_os_cpu_name(),
		(unsigned)(sizeof(void *) * 8), caps);
#if defined(__ANDROID__)
	printf("android_build_api=%d\n", __ANDROID_API__);
#endif
	start = now();

	for (i = 0; i < iterations; ++i)
		(void)hx_os_atomic_fetch_add(&atomic, 1, HX_OS_RELAXED);

	report("atomic_fetch_add", start, iterations);
	if (hx_string_assign_cstr(&text, "AbCd中文😀", NULL) != HX_OS_OK)
		return 1;

	start = now();

	for (i = 0; i < iterations; ++i) {
		(void)hx_string_lower_ascii(&text, NULL);
		(void)hx_string_upper_ascii(&text, NULL);
	}

	report("ascii_case_pair", start, iterations);

	hx_string_destroy(&text);
	if (caps & HX_OS_CAP_VARIANT) {
		hx_variant variant = HX_VARIANT_INIT;
		hx_bench_point point = {2, 5};
		int32_t integer;
		const void *object;

		start = now();

		for (i = 0; i < iterations; ++i) {
			if (hx_variant_set_i32(&variant, (int32_t)i, NULL) !=
					HX_OS_OK ||
				hx_variant_get_i32(&variant, &integer, NULL) !=
					HX_OS_OK)
				return 1;
			value ^= (uint64_t)integer;
		}

		report("variant_i32_set_get", start, iterations);
		start = now();

		for (i = 0; i < iterations; ++i) {
			if (hx_variant_set_custom(&variant,
				    &hx_bench_point_type, &point,
				    NULL) != HX_OS_OK ||
				hx_variant_get_custom(&variant,
					&hx_bench_point_type, &object,
					NULL) != HX_OS_OK)
				return 1;
			value ^= (uint64_t)((const hx_bench_point *)object)->x;
		}

		report("variant_custom_set_get", start, iterations);

		hx_variant_destroy(&variant);
	}

	if (caps & HX_OS_CAP_WIDE_MATH) {
		start = now();

		for (i = 0; i < iterations; ++i) {
			uint64_t converted;

			if (hx_os_muldiv_u64((uint64_t)i, 1000000000, 30000,
				    HX_OS_ROUND_DOWN, &converted,
				    NULL) != HX_OS_OK)
				return 1;
			value ^= converted;
		}

		report("muldiv_u64", start, iterations);
	}

	if (caps & HX_OS_CAP_PROCEDURES) {
		hx_os_proc_handler *handler = NULL;
		uint64_t calls = 0;

		if (hx_os_proc_create(NULL, &handler, NULL) != HX_OS_OK ||
			hx_os_proc_register(handler, "count", count_call,
				&calls, NULL) != HX_OS_OK)
			return 1;
		start = now();

		for (i = 0; i < iterations; ++i)
			if (hx_os_proc_call(handler, "count", NULL, NULL) !=
				HX_OS_OK)
				return 1;

		report("named_procedure", start, iterations);
		if (hx_os_proc_destroy(&handler, NULL) != HX_OS_OK ||
			calls != iterations)
			return 1;
	}

	if (caps & HX_OS_CAP_PRECISE_WAIT) {
		uint64_t total = 0, worst = 0;

		for (i = 0; i < 30; ++i) {
			hx_os_deadline deadline = hx_os_deadline_after_ms(2);

			uint64_t late;

			if (hx_os_sleep_until_precise(deadline, NULL) !=
				HX_OS_OK)
				return 1;
			late = hx_os_monotonic_ns() - deadline;
			total += late;
			if (late > worst)
				worst = late;
		}

		printf("precise_wait samples=30 mean_late_ns=%" PRIu64
		       " max_late_ns=%" PRIu64 "\n",
			total / 30, worst);
	}

	printf("checksum=%" PRIu64 " counter=%" PRId64 "\n", value,
		hx_os_atomic_load(&atomic, HX_OS_RELAXED));

	return 0;
}
