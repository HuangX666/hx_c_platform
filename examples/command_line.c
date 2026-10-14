#include <hx_os.h>
#include <stdio.h>

int main(int argc, char **argv)
{
	const hx_os_cli_option options[] = {
		{"help", "Show usage", 'h', HX_OS_CLI_FLAG, 0},
		{"port", "Listen port", 'p', HX_OS_CLI_VALUE, 0},
		{"name", "Device name", 'n', HX_OS_CLI_VALUE, 0}};
	hx_os_cli *arguments = NULL;
	hx_string text = HX_STRING_INIT;
	int64_t port = 8080;
	hx_os_status status = hx_os_cli_parse_process(NULL, argc,
		(const char *const *)argv, options,
		sizeof(options) / sizeof(options[0]), &arguments, NULL);

	if (status == HX_OS_OK && hx_os_cli_count(arguments, "help")) {
		status = hx_os_cli_help("hx_os_command_line", options,
			sizeof(options) / sizeof(options[0]), &text, NULL);
		if (status == HX_OS_OK)
			puts(hx_string_c_str(&text));
	}
	else if (status == HX_OS_OK) {
		if (hx_os_cli_count(arguments, "port"))
			status = hx_os_cli_i64(arguments, "port", &port, NULL);
		if (status == HX_OS_OK)
			status = hx_string_assign_cstr(&text, "door-1", NULL);
		if (status == HX_OS_OK && hx_os_cli_count(arguments, "name"))
			status =
				hx_os_cli_value(arguments, "name", &text, NULL);
		if (status == HX_OS_OK)
			printf("Device %s, port %lld\n", hx_string_c_str(&text),
				(long long)port);
	}
	if (status != HX_OS_OK)
		fprintf(stderr, "Arguments: %s\n", hx_os_status_name(status));

	hx_os_cli_destroy(&arguments);
	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
