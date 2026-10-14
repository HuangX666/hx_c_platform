#include <hx_os_control.h>
#include <inttypes.h>
#include <stdio.h>

int main(void)
{
	hx_os_stopwatch watch;
	hx_os_backoff backoff;
	hx_os_backoff_options options = HX_OS_BACKOFF_OPTIONS_INIT;
	hx_os_token_bucket bucket;
	hx_os_deadline retry;
	hx_os_status status;
	uint64_t delay, elapsed;

	status = hx_os_stopwatch_start(&watch, NULL);
	if (status != HX_OS_OK)
		goto failed;

	options.seed = 17;
	options.max_retries = 3;
	status = hx_os_backoff_init(&backoff, &options, NULL);
	if (status != HX_OS_OK)
		goto failed;

	// Calculate delays; business attempts and sleeping stay explicit.
	while ((status = hx_os_backoff_next(&backoff, &delay, NULL)) ==
		HX_OS_OK)
		printf("Next retry delay: %" PRIu64 " ms\n", delay);
	if (status != HX_OS_EOF)
		goto failed;
	puts("Retry budget exhausted.");

	status = hx_os_token_bucket_init(&bucket, 2, 1, NULL);
	if (status == HX_OS_OK)
		status = hx_os_token_bucket_try_take(&bucket, 2, &retry, NULL);
	if (status != HX_OS_OK)
		goto failed;
	status = hx_os_token_bucket_try_take(&bucket, 1, &retry, NULL);
	if (status == HX_OS_WOULD_BLOCK)
		printf("Rate limited; suggested monotonic deadline: %" PRIu64
			" ns\n", retry);
	else if (status != HX_OS_OK)
		goto failed;

	status = hx_os_stopwatch_elapsed_ns(&watch, &elapsed, NULL);
	if (status != HX_OS_OK)
		goto failed;
	printf("Example interval: %" PRIu64 " ns\n", elapsed);

	return 0;

failed:
	fprintf(stderr, "Control operation failed: %s\n",
		hx_os_status_name(status));

	return 1;
}
