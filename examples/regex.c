#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_regex *regex = NULL;
	hx_os_regex_result match;
	hx_string changed = HX_STRING_INIT;
	hx_os_error error;
	const char *record = "device=Door_01 request=42";
	hx_os_status status;

	status = hx_os_regex_compile(NULL,
		hx_string_view_from_cstr(
			"device=(?<device>\\w+) request=(?<id>\\d+)"),
		NULL, &regex, &error);
	if (status == HX_OS_OK)
		status = hx_os_regex_find(regex,
			hx_string_view_from_cstr(record), 0, &match, &error);
	if (status == HX_OS_OK) {
		printf("Device: %.*s\n", (int)match.captures[1].size,
			record + match.captures[1].offset);
		printf("Request: %.*s\n", (int)match.captures[2].size,
			record + match.captures[2].offset);
		status = hx_os_regex_replace(regex,
			hx_string_view_from_cstr(record),
			hx_string_view_from_cstr("${device}: #${id}"), NULL,
			&changed, &error);
	}
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&changed));
	else {
		char message[256];
		size_t required;

		(void)hx_os_error_message(
			&error, message, sizeof(message), &required);
		fprintf(stderr, "%s\n", message);
	}

	hx_os_regex_destroy(&regex);
	hx_string_destroy(&changed);
	return status == HX_OS_OK ? 0 : 1;
}
