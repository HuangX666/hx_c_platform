#include <hx_os.h>
#include <stdio.h>

static hx_os_status report_match(
	size_t pattern, size_t offset, size_t bytes, void *user)
{
	(void)user;
	printf("Pattern %zu at byte %zu, length %zu\n", pattern, offset, bytes);
	return HX_OS_OK;
}

int main(void)
{
	int32_t values[] = {7, -3, 9, 1, 0}, sample[2];
	hx_os_rng rng;
	hx_os_matcher *matcher = NULL;
	hx_os_bloom *bloom = NULL;
	hx_os_bytes_view patterns[] = {{"warning", 7}, {"error", 5}};
	hx_os_bytes_view text = {"warning: device error", 21};
	size_t written = 0, offset = 0;
	int maybe = 0;
	hx_os_status status;

	hx_os_rng_seed(&rng, 42);
	status = hx_os_array_shuffle(values, 5, sizeof(values[0]), &rng, NULL);
	if (status == HX_OS_OK)
		status = hx_os_array_sample(values, 5, sizeof(values[0]),
			sample, 2, &rng, &written, NULL);
	if (status == HX_OS_OK)
		printf("Sample: %d, %d\n", (int)sample[0], (int)sample[1]);
	if (status == HX_OS_OK)
		status = hx_os_radix_sort_i32(NULL, values, 5, 0, NULL);
	if (status == HX_OS_OK)
		printf("Sorted: %d .. %d\n", (int)values[0], (int)values[4]);
	if (status == HX_OS_OK)
		status = hx_os_bytes_find(
			NULL, text, patterns[1], 0, 0, &offset, NULL);
	if (status == HX_OS_OK)
		printf("First error: byte %zu\n", offset);
	if (status == HX_OS_OK)
		status = hx_os_matcher_create(
			NULL, patterns, 2, 0, &matcher, NULL);
	if (status == HX_OS_OK)
		status = hx_os_matcher_scan(
			matcher, text, 0, report_match, NULL, &written, NULL);
	if (status == HX_OS_OK)
		status = hx_os_bloom_create(NULL, 1000, 0.01, 0, &bloom, NULL);
	if (status == HX_OS_OK)
		status = hx_os_bloom_insert(bloom, patterns[1], NULL);
	if (status == HX_OS_OK)
		status = hx_os_bloom_contains(bloom, patterns[1], &maybe, NULL);
	if (status == HX_OS_OK)
		printf("Bloom: %s\n", maybe ? "possibly present" : "absent");

	hx_os_matcher_destroy(&matcher);
	hx_os_bloom_destroy(&bloom);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	return status == HX_OS_OK ? 0 : 1;
}
