#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>

static hx_os_status measure(unsigned features)
{
	const size_t iterations = 100000;
	hx_os_profiler *profiler = NULL;
	hx_os_status status;
	uint64_t start, end;
	size_t i;

#if defined(HX_PROFILE_BASELINE)
	(void)features;
	status = hx_os_profiler_create(NULL, &profiler, NULL);
#else
	status = hx_os_profiler_create_with_features(
		NULL, NULL, features, &profiler, NULL);
#endif
	if (status != HX_OS_OK)
		return status;
	status = hx_os_profiler_start_root(profiler, "warm", NULL);
	if (status == HX_OS_OK)
		status = hx_os_profiler_leave(profiler, NULL);

	start = hx_os_monotonic_ns();
	for (i = 0; i < iterations && status == HX_OS_OK; ++i) {
		status = hx_os_profiler_start_root(profiler, "warm", NULL);
		if (status == HX_OS_OK)
			status = hx_os_profiler_leave(profiler, NULL);
	}
	end = hx_os_monotonic_ns();
	if (status == HX_OS_OK)
		printf("features=%u iterations=%zu ns_per_interval=%.2f\n",
			features, iterations,
			(double)(end - start) / iterations);

	(void)hx_os_profiler_restart_thread(profiler, NULL);
	{
		hx_os_status closed = hx_os_profiler_destroy(&profiler, NULL);

		if (status == HX_OS_OK)
			status = closed;
	}

	return status;
}

int main(int argc, char **argv)
{
	unsigned features = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 0;
	hx_os_status status = measure(features);

	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
