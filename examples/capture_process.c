#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	hx_os_process_capture *capture = NULL;
	hx_os_process_result result;
	hx_os_error error;
	hx_os_status status;
	int stream;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: capture_process <executable> [arguments...]\n");

		return 1;
	}

	status = hx_os_process_capture_run(NULL, argv[1], argv + 2, NULL,
		hx_os_deadline_after_ms(2000), &capture, &error);
	if (!capture) {
		fprintf(stderr, "Start failed: %s\n",
			hx_os_status_name(status));

		return 1;
	}

	/* This example chooses to keep waiting and explicitly discard future
	 * output after a limit/error. An application can instead raise limits
	 * or keep the session for a later UI/event-loop action.
	 */
	while (status != HX_OS_OK) {
		fprintf(stderr, "Capture: %s; continuing to wait.\n",
			hx_os_status_name(status));
		if (status != HX_OS_TIMEOUT) {
			(void)hx_os_process_capture_discard(capture, 0);
			(void)hx_os_process_capture_discard(capture, 1);
		}

		status = hx_os_process_capture_wait_until(
			capture, hx_os_deadline_after_ms(2000), NULL, &error);
	}

	(void)hx_os_process_capture_wait_until(
		capture, HX_OS_DEADLINE_NOW, &result, NULL);

	for (stream = 0; stream < 2; ++stream) {
		const void *data;
		size_t bytes;

		(void)hx_os_process_capture_view(
			capture, stream, &data, &bytes);
		if (bytes)
			(void)fwrite(data, 1, bytes, stream ? stderr : stdout);
	}

	status = hx_os_process_capture_close_until(
		&capture, hx_os_deadline_after_ms(2000), &error);

	return status == HX_OS_OK && !result.signal_number && !result.exit_code
		? 0
		: 1;
}
