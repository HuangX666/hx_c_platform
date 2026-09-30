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
int main(void)
{
	hx_os_calldata data = HX_OS_CALLDATA_INIT, copy;
	hx_string_view view = {"中文\0😀", sizeof("中文\0😀") - 1}, found;
	char large[513];
	size_t i;
	int boolean = 5, marker;
	int64_t integer = 99;
	double real = 0;
	void *pointer = NULL;
	if (!(hx_os_capabilities() & HX_OS_CAP_SIGNALS)) {
		CHECK(hx_os_calldata_set_i64(&data, "value", 1, NULL) ==
			HX_OS_NOT_SUPPORTED);
		return 0;
	}
	CHECK(hx_os_calldata_init(&data, sizeof(data) - 1) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_os_calldata_init(&data, sizeof(data)) == HX_OS_OK);
	CHECK(hx_os_calldata_set_bool(&data, "bool", 1, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_set_i64(&data, "integer", INT64_MIN, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_calldata_set_double(&data, "double", 3.25, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_calldata_set_pointer(&data, "pointer", &marker, NULL) ==
		HX_OS_OK);
	CHECK(hx_os_calldata_set_string(&data, "文字", view, NULL) == HX_OS_OK);
	CHECK(hx_os_calldata_get_bool(&data, "bool", &boolean, NULL) ==
			HX_OS_OK &&
		boolean == 1);
	CHECK(hx_os_calldata_get_i64(&data, "integer", &integer, NULL) ==
			HX_OS_OK &&
		integer == INT64_MIN);
	CHECK(hx_os_calldata_get_double(&data, "double", &real, NULL) ==
			HX_OS_OK &&
		real == 3.25);
	CHECK(hx_os_calldata_get_pointer(&data, "pointer", &pointer, NULL) ==
			HX_OS_OK &&
		pointer == &marker);
	CHECK(hx_os_calldata_get_string(&data, "文字", &found, NULL) ==
			HX_OS_OK &&
		found.size == view.size &&
		!memcmp(found.data, view.data, view.size));
	copy = data;
	CHECK(hx_os_calldata_init(&data, sizeof(data)) == HX_OS_OK);
	CHECK(hx_os_calldata_get_string(&copy, "文字", &found, NULL) ==
			HX_OS_OK &&
		!memcmp(found.data, view.data, view.size));
	for (i = 0; i < 1000; ++i) {
		CHECK(hx_os_calldata_set_string(&copy, "文字", found, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_calldata_get_string(&copy, "文字", &found, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_calldata_get_i64(&copy, "bool", &integer, NULL) ==
			HX_OS_TYPE_MISMATCH &&
		integer == INT64_MIN);
	CHECK(hx_os_calldata_get_i64(&copy, "missing", &integer, NULL) ==
			HX_OS_NOT_FOUND &&
		integer == INT64_MIN);
	data = copy;
	memset(large, 'x', sizeof(large));
	view.data = large;
	view.size = sizeof(large);
	CHECK(hx_os_calldata_set_string(&copy, "large", view, NULL) ==
		HX_OS_LIMIT);
	CHECK(!memcmp(&data, &copy, sizeof(data)));
	view.data = "\xff";
	view.size = 1;
	CHECK(hx_os_calldata_set_string(&copy, "bad", view, NULL) ==
			HX_OS_INVALID_ENCODING &&
		!memcmp(&data, &copy, sizeof(data)));
	CHECK(hx_os_calldata_set_bool(&copy, "bool", 2, NULL) == HX_OS_INVALID);
	CHECK(hx_os_calldata_init(&data, sizeof(data)) == HX_OS_OK);
	for (i = 0; i < 16; ++i) {
		char name[16];
		(void)snprintf(name, sizeof(name), "n%u", (unsigned)i);
		CHECK(hx_os_calldata_set_i64(&data, name, (int64_t)i, NULL) ==
			HX_OS_OK);
	}
	CHECK(hx_os_calldata_set_i64(&data, "overflow", 17, NULL) ==
		HX_OS_LIMIT);
	CHECK(hx_os_calldata_set_i64(&data, "n0", 42, NULL) == HX_OS_OK);
	return 0;
}
