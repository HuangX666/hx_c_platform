#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_settings *settings = NULL;
	hx_settings_string_ref label = HX_SETTINGS_STRING_REF_INIT;
	hx_string json = HX_STRING_INIT;
	int64_t port;
	hx_os_error error;

	hx_os_status status = hx_settings_create(NULL, &settings, &error);
	if (status == HX_OS_OK)
		status = hx_settings_set_default_i64(
			settings, "port", 80, &error);

	if (status == HX_OS_OK) {
		hx_settings_update update = hx_settings_update_begin(settings);
		hx_settings_update_set_i64(&update, "port", 8080);
		hx_settings_update_set_string(&update, "label", "服务😀");

		status = hx_settings_update_finish(&update, NULL, &error);
	}

	if (status == HX_OS_OK)
		status = hx_settings_get_i64(settings, "port", &port, &error);

	if (status == HX_OS_OK)
		status = hx_settings_get_string_ref(
			settings, "label", &label, &error);

	if (status == HX_OS_OK)
		printf("%s: %lld\n", label.text.data, (long long)port);

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

	hx_settings_string_ref_destroy(&label);
	hx_string_destroy(&json);
	if (status != HX_OS_OK)
		fprintf(stderr, "%s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
