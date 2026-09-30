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
	const unsigned char encoded[] = {0x32, 0x22};
	hx_os_obfuscated value = HX_OS_OBFUSCATED_INIT(encoded, 0x5a);
	hx_string text = HX_STRING_INIT;
	char buffer[16] = "unchanged";
	size_t required = 0;
	if (!(hx_os_capabilities() & HX_OS_CAP_OBFUSCATION)) {
		CHECK(hx_os_obfuscate_decode(&value, buffer, sizeof(buffer),
			      &required, NULL) == HX_OS_NOT_SUPPORTED);
		return 0;
	}
	CHECK(hx_os_obfuscate_decode(&value, buffer, 2, &required, NULL) ==
			HX_OS_OUTPUT_FULL &&
		required == 3 && !strcmp(buffer, "unchanged"));
	CHECK(hx_os_obfuscate_decode(&value, buffer, sizeof(buffer), &required,
		      NULL) == HX_OS_OK &&
		!strcmp(buffer, "hx"));
	CHECK(hx_os_obfuscate_string(&value, &text, NULL) == HX_OS_OK &&
		!strcmp(hx_string_c_str(&text), "hx"));
	{
		const unsigned char invalid[] = {0xa5};
		value.data = invalid;
		value.bytes = 1;
		CHECK(hx_os_obfuscate_string(&value, &text, NULL) ==
				HX_OS_INVALID_ENCODING &&
			!strcmp(hx_string_c_str(&text), "hx"));
	}
	value.data = (const unsigned char *)buffer;
	value.bytes = 2;
	buffer[0] = (char)encoded[0];
	buffer[1] = (char)encoded[1];
	CHECK(hx_os_obfuscate_decode(&value, buffer, sizeof(buffer), &required,
		      NULL) == HX_OS_OK &&
		!strcmp(buffer, "hx"));
	value.data = NULL;
	value.bytes = 0;
	CHECK(hx_os_obfuscate_string(&value, &text, NULL) == HX_OS_OK &&
		!text.size);
	value.key = 0;
	CHECK(hx_os_obfuscate_string(&value, &text, NULL) == HX_OS_INVALID);
	hx_string_destroy(&text);
	return 0;
}
