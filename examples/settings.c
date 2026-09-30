#include <hx_os.h>
#include <stdio.h>
int main(void)
{
	hx_settings *settings = NULL;
	hx_string label = HX_STRING_INIT, json = HX_STRING_INIT;
	int64_t port;
	hx_os_error error;
	hx_os_status status = hx_settings_create(NULL, &settings, &error);
	if (status == HX_OS_OK)
		status = hx_settings_set_default_i64(
			settings, "port", 80, &error);
	if (status == HX_OS_OK)
		status = hx_settings_set_i64(settings, "port", 8080, &error);
	if (status == HX_OS_OK)
		status = hx_settings_set_string(
			settings, "label", "服务😀", &error);
	if (status == HX_OS_OK)
		status = hx_settings_get_i64(settings, "port", &port, &error);
	if (status == HX_OS_OK)
		status = hx_settings_get_string(
			settings, "label", &label, &error);
	if (status == HX_OS_OK)
		printf("%s: %lld\n", hx_string_c_str(&label), (long long)port);
	if (status == HX_OS_OK && (hx_os_capabilities() & HX_OS_CAP_CONFIG) &&
		(hx_os_capabilities() & HX_OS_CAP_CONFIG_SERIALIZE)) {
		status = hx_settings_to_json(settings, &json, &error);
		if (status == HX_OS_OK)
			puts(hx_string_c_str(&json));
	}
	if (settings) {
		hx_os_status closed = hx_settings_destroy(&settings, NULL);
		if (status == HX_OS_OK)
			status = closed;
	}
	hx_string_destroy(&label);
	hx_string_destroy(&json);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));
	return status == HX_OS_OK ? 0 : 1;
}
