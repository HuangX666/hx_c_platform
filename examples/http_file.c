#include <hx_os.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	hx_os_http *http = NULL;
	hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
	hx_os_http_stream_options options = HX_OS_HTTP_STREAM_OPTIONS_INIT;
	hx_os_error error = {0};
	hx_os_status status, closed;
	int cancellable =
		argc > 1 && !strcmp(argv[argc - 1], "--cancellable-dns");

	if (cancellable)
		--argc;

	if (argc < 4 || argc > 5 ||
		(strcmp(argv[1], "download") && strcmp(argv[1], "upload"))) {
		fprintf(stderr,
			"Usage: %s download|upload URL FILE [CA.pem] "
			"[--cancellable-dns]\n",
			argv[0]);
		return 2;
	}
	options.request.timeout_ms = 30000;
	options.request.ca_file = argc == 5 ? argv[4] : NULL;

	status = hx_os_http_create(NULL, &http, &error);
	if (status == HX_OS_OK && cancellable)
		status = hx_os_http_use_cancellable_dns(http, NULL, &error);
	if (status == HX_OS_OK) {
		if (!strcmp(argv[1], "download"))
			status = hx_os_http_download_file(http, argv[2],
				argv[3], 0, &options, &response, &error);
		else
			status = hx_os_http_upload_file(http, "PUT", argv[2],
				argv[3], NULL, 0, &options, &response, &error);
	}
	if (status == HX_OS_OK)
		printf("HTTP %u: %s %s\n", response.status_code, argv[1],
			argv[3]);
	else {
		char message[512];
		size_t required;

		if (hx_os_error_message(&error, message, sizeof(message),
			    &required) == HX_OS_OK)
			fprintf(stderr, "%s\n", message);
		else
			fprintf(stderr, "%s\n", hx_os_status_name(status));
	}
	if (hx_os_http_pending_file(http))
		fprintf(stderr, "Cleanup pending: %s\n",
			hx_os_http_pending_file(http));

	closed = hx_os_http_close(&http, &error);
	if (closed != HX_OS_OK) {
		fprintf(stderr, "Close failed: %s\n",
			hx_os_status_name(closed));
		status = closed;
	}
	hx_os_http_response_destroy(&response);

	return status == HX_OS_OK ? 0 : 1;
}
