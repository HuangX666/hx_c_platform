#include <hx_os_base.h>
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
	uint64_t start = 0, end = 0;
	hx_os_error error;
	hx_os_status status = hx_os_high_resolution_read(&start, &error);

	// Deadlines and interval timestamps belong to separate clock domains.
	if (status == HX_OS_OK)
		status = hx_os_sleep_until(hx_os_deadline_after_ms(20), &error);
	if (status == HX_OS_OK)
		status = hx_os_high_resolution_read(&end, &error);
	if (status != HX_OS_OK) {
		fprintf(stderr, "clock/wait: %s (native=%" PRId64 ")\n",
			hx_os_status_name(status), error.native_code);
		return 1;
	}
	if (end < start) {
		fprintf(stderr, "interval clock moved backwards\n");
		return 1;
	}

	// Subtract integer timestamps before converting to a floating unit.
	printf("Requested wait: 20 ms; measured: %.3f ms (%" PRIu64 " ns)\n",
		(double)(end - start) / 1000000.0, end - start);
	return 0;
}
