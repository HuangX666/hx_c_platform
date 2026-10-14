#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_url *url = NULL;
	hx_string address = HX_STRING_INIT, name = HX_STRING_INIT;
	hx_os_status status = hx_os_url_parse(
		NULL, "https://example.com/devices?request=42", &url, NULL);

	if (status == HX_OS_OK)
		status = hx_os_url_query_set(url,
			hx_string_view_from_cstr("name"),
			hx_string_view_from_cstr("门禁 A"), NULL);
	if (status == HX_OS_OK)
		status = hx_os_url_part(url, HX_OS_URL_FULL, &address, NULL);
	if (status == HX_OS_OK)
		status = hx_os_url_query_get(
			url, hx_string_view_from_cstr("name"), &name, NULL);
	if (status == HX_OS_OK) {
		puts(hx_string_c_str(&address));
		printf("Name: %s\n", hx_string_c_str(&name));
	}
	else
		fprintf(stderr, "URL: %s\n", hx_os_status_name(status));

	hx_os_url_destroy(&url);
	hx_string_destroy(&name);
	hx_string_destroy(&address);
	return status == HX_OS_OK ? 0 : 1;
}
