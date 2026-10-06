#include <hx_os.h>
#include <stdio.h>

static hx_os_status HX_OS_CALL receive(
	const hx_os_calldata *data, void *user, hx_os_error *error)
{
	(void)data;
	(void)error;
	++*(size_t *)user;

	return HX_OS_OK;
}

int main(void)
{
	const size_t iterations = 200000;
	hx_os_signal_handler *handler = NULL;
	hx_os_status status;
	size_t calls = 0, i;
	uint64_t start, end;

	status = hx_os_signal_handler_create(NULL, &handler, NULL);
	if (status == HX_OS_OK)
		status = hx_os_signal_connect(
			handler, "sample", receive, &calls, NULL, NULL, NULL);
	for (i = 0; i < 1000 && status == HX_OS_OK; ++i)
		status = hx_os_signal_emit(handler, "sample", NULL, NULL, NULL);
	if (status == HX_OS_OK && calls != 1000)
		status = HX_OS_INVALID;

	calls = 0;
	start = hx_os_monotonic_ns();
	for (i = 0; i < iterations && status == HX_OS_OK; ++i)
		status = hx_os_signal_emit(handler, "sample", NULL, NULL, NULL);
	end = hx_os_monotonic_ns();
	if (status == HX_OS_OK && calls != iterations)
		status = HX_OS_INVALID;
	if (status == HX_OS_OK)
		printf("iterations=%zu ns_per_emit=%.2f\n", iterations,
			(double)(end - start) / iterations);

	if (handler) {
		hx_os_status closed = hx_os_signal_handler_close_until(
			&handler, HX_OS_DEADLINE_NOW, NULL);

		if (status == HX_OS_OK)
			status = closed;
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
