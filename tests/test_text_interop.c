#include "utility_test.h"
#include <wchar.h>

static void test_wide(void)
{
	const char text[] = "A\0中文😀";
	const wchar_t wide[] = L"A\0中文😀";
	const size_t units = sizeof(wide) / sizeof(wide[0]) - 1;
	wchar_t output[16], invalid[] = {(wchar_t)0xd800};
	char bytes[32];
	size_t required = 99;
	hx_string string = HX_STRING_INIT;

	CHECK(hx_os_text_to_wide(text, sizeof(text) - 1, NULL, 0, &required,
		      NULL) == HX_OS_OUTPUT_FULL);
	CHECK(required == units + 1);
	wmemset(output, L'X', 16);
	CHECK(hx_os_text_to_wide(text, sizeof(text) - 1, output, units,
		      &required, NULL) == HX_OS_OUTPUT_FULL);
	CHECK(output[0] == L'X' && output[15] == L'X');
	CHECK(hx_os_text_to_wide(text, sizeof(text) - 1, output, 16, &required,
		      NULL) == HX_OS_OK);
	CHECK(!memcmp(output, wide, sizeof(wide)));
	CHECK(hx_os_text_from_wide(wide, units, NULL, 0, &required, NULL) ==
		HX_OS_OUTPUT_FULL);
	CHECK(required == sizeof(text));
	CHECK(hx_os_text_from_wide(wide, units, bytes, sizeof(bytes), &required,
		      NULL) == HX_OS_OK);
	CHECK(!memcmp(bytes, text, sizeof(text)));

	CHECK(hx_string_assign_wide(&string, wide, units, NULL) == HX_OS_OK);
	CHECK(string.size == sizeof(text) - 1);
	CHECK(!memcmp(string.data, text, sizeof(text)));
	CHECK(hx_string_assign_wide(&string, invalid, 1, NULL) != HX_OS_OK);
	CHECK(!memcmp(string.data, text, sizeof(text)));
	memset(bytes, 'X', sizeof(bytes));
	CHECK(hx_os_text_from_wide(invalid, 1, bytes, sizeof(bytes), &required,
		      NULL) != HX_OS_OK);
	CHECK(bytes[0] == 'X' && required == 0);
	wmemset(output, L'X', 16);
	CHECK(hx_os_text_to_wide("\xff", 1, output, 16, &required, NULL) ==
		HX_OS_INVALID_ENCODING);
	CHECK(output[0] == L'X' && required == 0);
	CHECK(hx_os_text_from_wide(wide, SIZE_MAX, bytes, sizeof(bytes),
		      &required, NULL) == HX_OS_RANGE);
	CHECK(hx_os_text_to_wide(NULL, 0, output, 16, &required, NULL) ==
		HX_OS_OK);
	CHECK(required == 1 && output[0] == 0);
	CHECK(hx_os_text_from_wide(NULL, 0, bytes, sizeof(bytes), &required,
		      NULL) == HX_OS_OK);
	CHECK(required == 1 && bytes[0] == 0);
	hx_string_destroy(&string);
}

static void test_stdio(void)
{
	const char *path = "hx-stdio-中文😀.bin";
	const char data[] = "first\r\nsecond\n\0last";
	char bytes[sizeof(data)];
	hx_os_error error;
	FILE *stream;

	CHECK(!hx_os_fopen_utf8(path, "w,ccs=UTF-8", &error));
	CHECK(error.status == HX_OS_INVALID);
	CHECK(!hx_os_fopen_utf8(path, "r++", &error));
	CHECK(error.status == HX_OS_INVALID);
	CHECK(!hx_os_fopen_utf8("\xff", "rb", &error));
	CHECK(error.status == HX_OS_INVALID_ENCODING);
	CHECK(!hx_os_fopen_utf8(NULL, "rb", &error));
	CHECK(error.status == HX_OS_INVALID);

	stream = hx_os_fopen_utf8(path, "wb", &error);
	CHECK(stream && error.status == HX_OS_OK);
	CHECK(fwrite(data, 1, sizeof(data), stream) == sizeof(data));
	CHECK(hx_os_fclose(&stream, &error) == HX_OS_OK && !stream);
	stream = hx_os_fopen_utf8(path, "rb", &error);
	CHECK(stream && error.status == HX_OS_OK);
	CHECK(fread(bytes, 1, sizeof(bytes), stream) == sizeof(bytes));
	CHECK(!memcmp(bytes, data, sizeof(data)));
	CHECK(fgetc(stream) == EOF && feof(stream));
	CHECK(hx_os_fclose(&stream, &error) == HX_OS_OK && !stream);
	CHECK(hx_os_fclose(&stream, &error) == HX_OS_OK);
	CHECK(hx_os_file_remove(path, NULL) == HX_OS_OK);
	CHECK(!hx_os_fopen_utf8(path, "rb", &error));
	CHECK(error.status == HX_OS_NOT_FOUND);
}

int main(void)
{
	test_wide();
	test_stdio();
	puts("native wide text and UTF-8 stdio passed");

	return 0;
}
