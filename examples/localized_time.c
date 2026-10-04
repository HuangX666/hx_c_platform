#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_text_lookup *labels = NULL;
	hx_string label = HX_STRING_INIT, date = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status = hx_os_text_lookup_create(NULL, &labels, &error);
	if (status == HX_OS_OK)
		status = hx_os_text_lookup_add(
			labels, "Created", "创建时间", &error);

	if (status == HX_OS_OK)
		status = hx_os_text_lookup_get_string(
			labels, "Created", &label, &error);

	if (status == HX_OS_OK)
		status = hx_os_wall_time_format_utc_string(
			hx_os_wall_time_ms(), NULL, &date, &error);

	if (status == HX_OS_OK)
		printf("%s: %s\n", hx_string_c_str(&label),
			hx_string_c_str(&date));
	else
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	hx_string_destroy(&date);
	hx_string_destroy(&label);
	hx_os_text_lookup_destroy(&labels);

	return status == HX_OS_OK ? 0 : 1;
}
