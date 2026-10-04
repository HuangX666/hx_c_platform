#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

int main(int argc, char **argv)
{
	typedef uint32_t(HX_OS_CALL * hx_plugin_abi_fn)(void);

	hx_plugin_abi_fn abi;
	hx_os_context *context = NULL;
	hx_os_library *library = NULL;
	hx_os_library *missing = NULL;
	hx_string diagnostic = HX_STRING_INIT;
	hx_string saved = HX_STRING_INIT;
	hx_os_error error;
	void *address = NULL;
	size_t invalid;

	CHECK(argc == 2);
	CHECK(hx_os_library_close(&library, NULL) == HX_OS_OK);
	CHECK(hx_os_library_open(NULL, NULL, 0, &library, NULL) ==
		HX_OS_INVALID);
	CHECK(hx_os_library_open(NULL, argv[1], 7, &library, NULL) ==
		HX_OS_INVALID);
	if (!(hx_os_capabilities() & HX_OS_CAP_LIBRARY)) {
		CHECK(hx_os_library_open(NULL, argv[1], 0, &library, &error) ==
				HX_OS_NOT_SUPPORTED &&
			!library);
		CHECK(error.status == HX_OS_NOT_SUPPORTED);

		return 0;
	}

	CHECK(hx_os_library_open_ex(NULL, "hx-library-does-not-exist", 0,
		      &missing, &diagnostic, &error) < HX_OS_OK);
	CHECK(!missing && diagnostic.size && error.domain != HX_OS_ERROR_NONE);
	CHECK(hx_os_text_validate_utf8(
		      diagnostic.data, diagnostic.size, &invalid) == HX_OS_OK);
	CHECK(hx_string_copy(&saved, &diagnostic, NULL) == HX_OS_OK);
	CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
	CHECK(hx_os_library_open(context, argv[1], HX_OS_LIBRARY_LOCAL,
		      &library, &error) == HX_OS_OK);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
	CHECK(hx_os_library_symbol(library, "hx_plugin_abi_version", &address,
		      &error) == HX_OS_OK &&
		address);

	memcpy(&abi, &address, sizeof(abi));
	CHECK(abi() == hx_os_abi_version());
	CHECK(hx_os_library_bind(library, "hx_plugin_abi_version", &abi,
		      sizeof(abi), NULL) == HX_OS_OK);
	CHECK(abi() == hx_os_abi_version());
	CHECK(hx_os_library_bind(library, "hx_plugin_abi_version", &abi, 1,
		      NULL) == HX_OS_INVALID);
	CHECK(hx_os_library_symbol_ex(library, "hx_no_such_symbol", &address,
		      &diagnostic, &error) == HX_OS_NOT_FOUND);
	CHECK(!address && diagnostic.size);
	CHECK(hx_os_text_validate_utf8(
		      diagnostic.data, diagnostic.size, &invalid) == HX_OS_OK);
	CHECK(saved.size &&
		hx_string_compare(&saved, hx_string_view_of(&diagnostic)) != 0);
	CHECK(hx_os_library_close_ex(&library, &diagnostic, &error) ==
		HX_OS_OK);
	CHECK(!library && !diagnostic.size);
	CHECK(hx_os_context_allocations(context) == 0);
	CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);

	hx_string_destroy(&diagnostic);
	hx_string_destroy(&saved);
	CHECK(hx_os_library_open(NULL, argv[1], HX_OS_LIBRARY_GLOBAL, &library,
		      NULL) == HX_OS_OK);
	CHECK(hx_os_library_close(&library, NULL) == HX_OS_OK);

	puts("dynamic library contracts passed");

	return 0;
}
