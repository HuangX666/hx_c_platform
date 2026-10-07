/* Compile the real formatter with controlled native message text. */
#undef HX_OS_SHARED
#include "../src/internal.h"
#include <windows.h>
#include <stdio.h>

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)

static DWORD WINAPI test_format_message(DWORD flags, LPCVOID source, DWORD code,
	DWORD language, LPWSTR buffer, DWORD capacity, va_list *arguments)
{
	size_t i;

	(void)flags;
	(void)source;
	(void)language;
	(void)arguments;
	CHECK(capacity >= 686);

	if (code == 1) {
		buffer[0] = 0x4e00;
		buffer[1] = 0;

		return 1;
	}

	if (code == 2) {
		for (i = 0; i < 682; ++i)
			buffer[i] = 0x4e00;

		buffer[682] = L'A';
		buffer[683] = L'B';
		buffer[684] = 0x4e00;
		buffer[685] = 0;

		return 685;
	}

	if (code == 3) {
		buffer[0] = 0xdc00;
		buffer[1] = 0;

		return 1;
	}

	return 0;
}

static void test_crypto_text(uint64_t code, char *out, size_t capacity)
{
	(void)code;
	(void)out;
	(void)capacity;
	/* This fixture replaces only Windows OS messages. Crypto has its own
	 * native-runtime test and must not be queried here. */
	CHECK(0);
}

#define hx_os_i_crypto_error_text test_crypto_text
#define FormatMessageW test_format_message
#include "../src/base.c"
#undef FormatMessageW
#undef hx_os_i_crypto_error_text

static void test_snapshot(uint32_t domain, int64_t code, int expect_text)
{
	hx_os_error error = {HX_OS_IO_ERROR, domain, code, 7};
	hx_os_error original = error;
	char buffer[4096], prefix[128];
	size_t required, measured, invalid;

	(void)snprintf(prefix, sizeof(prefix),
		"I/O error [domain=%u code=%" PRId64 "]", domain, code);
	CHECK(hx_os_error_message(&error, NULL, 0, &required) ==
		HX_OS_OUTPUT_FULL);
	CHECK(hx_os_error_message(&error, buffer, sizeof(buffer), &measured) ==
		HX_OS_OK);
	CHECK(required == measured && measured == strlen(buffer) + 1);
	CHECK(!strncmp(buffer, prefix, strlen(prefix)));
	CHECK(expect_text ? !strcmp(buffer + strlen(prefix), ": \xe4\xb8\x80")
			  : !strcmp(buffer, prefix));
	CHECK(hx_os_text_validate_utf8(buffer, strlen(buffer), &invalid) ==
		HX_OS_OK);
	CHECK(!memcmp(&error, &original, sizeof(error)));
}

int main(void)
{
	static const uint32_t domains[] = {HX_OS_ERROR_WIN32,
		HX_OS_ERROR_WINSOCK, HX_OS_ERROR_HRESULT, HX_OS_ERROR_GAI};
	size_t i;

	for (i = 0; i < sizeof(domains) / sizeof(domains[0]); ++i) {
		test_snapshot(domains[i], 1, 1);
		test_snapshot(domains[i], 2, 0);
		test_snapshot(domains[i], 3, 0);
		test_snapshot(domains[i], 4, 0);
	}

	puts("Windows native text failure fallback passed");

	return 0;
}
