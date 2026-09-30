#include <hx_os.h>
#include <stdio.h>
#include "example_arguments.h"

static int hx_example_main(int argc, const char *const *argv)
{
	typedef uint32_t(HX_OS_CALL * hx_plugin_abi_fn)(void);
	typedef hx_os_status(HX_OS_CALL * hx_plugin_version_fn)(
		char *, size_t, size_t *);
	hx_os_library *library = NULL;
	hx_plugin_abi_fn abi;
	hx_plugin_version_fn version;
	hx_string diagnostic = HX_STRING_INIT;
	hx_os_error error;
	hx_os_status status;
	char text[128];
	size_t required;
	if (argc != 2) {
		fprintf(stderr, "Usage: load_plugin <module path>\n");
		return 1;
	}
	status = hx_os_library_open_ex(NULL, argv[1], HX_OS_LIBRARY_LOCAL,
		&library, &diagnostic, &error);
	if (status == HX_OS_OK) {
		status = hx_os_library_bind_ex(library, "hx_plugin_abi_version",
			&abi, sizeof(abi), &diagnostic, &error);
		if (status == HX_OS_OK) {
			if (abi() != hx_os_abi_version())
				status = HX_OS_NOT_SUPPORTED;
		}
	}
	if (status == HX_OS_OK) {
		status = hx_os_library_bind_ex(library,
			"hx_plugin_version_string", &version, sizeof(version),
			&diagnostic, &error);
		if (status == HX_OS_OK)
			status = version(text, sizeof(text), &required);
	}
	if (status == HX_OS_OK)
		printf("Compatible plugin: %s\n", text);
	else
		fprintf(stderr, "%s: %s\n", hx_os_status_name(status),
			hx_string_c_str(&diagnostic));
	{
		hx_os_status closed = hx_os_library_close(&library, NULL);
		if (status == HX_OS_OK)
			status = closed;
	}
	hx_string_destroy(&diagnostic);
	return status == HX_OS_OK ? 0 : 1;
}
