#include <hx_os.h>
#include <cstring>

int main()
{
	hx_string text = HX_STRING_INIT;
	if (hx_string_assign_cstr(&text, "C ABI", NULL) != HX_OS_OK)
		return 1;
	const int result = std::strcmp(hx_string_c_str(&text), "C ABI");
	hx_string_destroy(&text);
	if (hx_os_capabilities() & HX_OS_CAP_CONFIG) {
		hx_os_config_document *document = NULL;
		const hx_os_config_node *value = NULL;
		int enabled = 0;
		if (hx_os_config_parse(NULL, HX_OS_CONFIG_JSON,
			    "{\"enabled\":true}", 16, NULL, &document,
			    NULL) != HX_OS_OK)
			return 1;
		const hx_os_status status = hx_os_config_pointer(
			hx_os_config_root(document), "/enabled", &value);
		const hx_os_status boolean =
			hx_os_config_get_bool(value, &enabled);
		hx_os_config_release(document);
		if (status != HX_OS_OK || boolean != HX_OS_OK || !enabled)
			return 1;
	}
	return result == 0 ? 0 : 1;
}
