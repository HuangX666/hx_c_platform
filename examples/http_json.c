#include <hx_os.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	hx_os_http_options options = HX_OS_HTTP_OPTIONS_INIT;
	hx_os_http_response response = HX_OS_HTTP_RESPONSE_INIT;
	hx_string text = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status;

	if (argc < 2 || argc > 3) {
		fprintf(stderr,
			"Usage: hx_os_http_json URL [trusted-ca.pem]\n");
		return 2;
	}
	if (argc == 3)
		options.ca_file = argv[2];
	status = hx_os_http_get(argv[1], &options, &response, &error);
	if (status == HX_OS_OK)
		status = hx_os_http_response_text(&response, &text, &error);
	if (status == HX_OS_OK) {
		printf("HTTP %u\n", response.status_code);
		puts(hx_string_c_str(&text));
	}
	else {
		char message[512];
		size_t required;

		(void)hx_os_error_message(
			&error, message, sizeof(message), &required);
		fprintf(stderr, "%s\n", message);
	}

	hx_string_destroy(&text);
	hx_os_http_response_destroy(&response);
	return status == HX_OS_OK ? 0 : 1;
}
