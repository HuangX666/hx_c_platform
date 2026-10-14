#include <hx_cache.h>
#include <stdio.h>

int main(void)
{
	hx_os_container_options options;
	hx_os_cache *cache = NULL;
	const void *value = NULL;
	int first = 20, second = 30, third = 40;
	hx_os_status status;

	hx_os_container_options_init(&options, sizeof(int));
	options.max_items = 2;
	options.max_bytes = 64 * 1024;

	status = hx_os_cache_create(NULL, &options, &cache, NULL);
	if (status == HX_OS_OK)
		status = hx_os_cache_put(cache, "sensor-a", 8, &first, NULL);
	if (status == HX_OS_OK)
		status = hx_os_cache_put(cache, "sensor-b", 8, &second, NULL);
	if (status == HX_OS_OK)
		status = hx_os_cache_get(cache, "sensor-a", 8, &value, NULL);
	if (status == HX_OS_OK)
		printf("Recent sensor-a: %d\n", *(const int *)value);

	// Reading sensor-a protected it; inserting sensor-c evicts sensor-b.
	if (status == HX_OS_OK)
		status = hx_os_cache_put(cache, "sensor-c", 8, &third, NULL);
	if (status == HX_OS_OK) {
		hx_os_status lookup =
			hx_os_cache_peek(cache, "sensor-b", 8, &value, NULL);

		if (lookup == HX_OS_NOT_FOUND)
			puts("sensor-b was evicted; cache retains two "
			     "entries.");
		else
			status = lookup == HX_OS_OK ? HX_OS_IO_ERROR : lookup;
	}

	// Expiry starts on write. Reading does not refresh its lifetime.
	if (status == HX_OS_OK)
		status = hx_os_cache_put_for(
			cache, "temporary", 9, &first, 10, NULL);
	if (status == HX_OS_OK)
		status = hx_os_sleep_until(hx_os_deadline_after_ms(20), NULL);
	if (status == HX_OS_OK) {
		hx_os_status lookup =
			hx_os_cache_get(cache, "temporary", 9, &value, NULL);

		if (lookup == HX_OS_NOT_FOUND)
			puts("The temporary value expired; load a fresh "
			     "result.");
		else
			status = lookup == HX_OS_OK ? HX_OS_IO_ERROR : lookup;
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "Cache failed: %s\n",
			hx_os_status_name(status));

	(void)hx_os_cache_destroy(&cache, NULL);

	return status == HX_OS_OK ? 0 : 1;
}
