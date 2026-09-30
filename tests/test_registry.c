#include <hx_os.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "line %d: %s\n", __LINE__, #value);    \
			exit(1);                                               \
		}                                                              \
	} while (0)
#if defined(_WIN32)
static wchar_t hx_owned_path[256];
static int hx_owned_key;
static void hx_registry_test_cleanup(void)
{
	if (hx_owned_key)
		(void)RegDeleteKeyW(HKEY_CURRENT_USER, hx_owned_path);
}
#endif
int main(void)
{
	hx_os_registry *key = NULL, *reader = NULL, *duplicate = NULL;
	hx_os_context *context = NULL;
	hx_string text = HX_STRING_INIT;
	uint32_t value = 99;
	hx_os_error error;
	if (!(hx_os_capabilities() & HX_OS_CAP_REGISTRY)) {
		CHECK(hx_os_registry_open_user(NULL, "disabled",
			      HX_OS_REGISTRY_READ, &key,
			      NULL) == HX_OS_NOT_SUPPORTED &&
			!key);
		CHECK(hx_os_registry_read_u32(NULL, NULL, &value, NULL) ==
				HX_OS_NOT_SUPPORTED &&
			value == 99);
		return 0;
	}
#if defined(_WIN32)
	{
		char path[256];
		wchar_t wide[256];
		HKEY native;
		char result[64] = "unchanged";
		size_t required;
		unsigned flags = HX_OS_REGISTRY_READ | HX_OS_REGISTRY_WRITE |
			HX_OS_REGISTRY_CREATE | HX_OS_REGISTRY_EXCLUSIVE;
		const unsigned char odd[] = {0x41};
		const wchar_t unterminated[] = {L'o', L'k'};
		const wchar_t embedded[] = {L'a', 0, L'b', 0};
		/* Only this exclusive, uniquely named leaf is removed below.
		 * No existing user key/value is modified or recursively
		 * deleted.
		 */
		(void)snprintf(path, sizeof(path),
			"Software\\hx_platform_test_%lu_%llx_%s",
			(unsigned long)GetCurrentProcessId(),
			(unsigned long long)hx_os_monotonic_ns(), "中文");
		CHECK(hx_os_context_create(NULL, &context, NULL) == HX_OS_OK);
		CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path,
			-1, hx_owned_path, 256));
		CHECK(atexit(hx_registry_test_cleanup) == 0);
		CHECK(hx_os_registry_open_user(
			      context, path, flags, &key, &error) == HX_OS_OK);
		hx_owned_key = 1;
		CHECK(hx_os_registry_open_user(context, path, flags, &duplicate,
			      NULL) == HX_OS_EXISTS &&
			!duplicate);
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_BUSY);
		CHECK(hx_os_registry_write_string(
			      key, "名字😀", "值中文😀", NULL) == HX_OS_OK);
		CHECK(hx_os_registry_read_string(key, "名字😀", &text, NULL) ==
				HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "值中文😀"));
		CHECK(hx_os_registry_read_string_buffer(key, "名字😀", result,
			      1, &required, NULL) == HX_OS_OUTPUT_FULL &&
			!strcmp(result, "unchanged"));
		CHECK(required == strlen("值中文😀") + 1);
		CHECK(hx_os_registry_read_string_buffer(key, "名字😀", result,
			      sizeof(result), &required, NULL) == HX_OS_OK &&
			!strcmp(result, "值中文😀"));
		CHECK(hx_os_registry_write_u32(key, NULL, UINT32_MAX, NULL) ==
			HX_OS_OK);
		CHECK(hx_os_registry_read_u32(key, "", &value, NULL) ==
				HX_OS_OK &&
			value == UINT32_MAX);
		CHECK(hx_os_registry_read_u32(key, "名字😀", &value, NULL) ==
				HX_OS_TYPE_MISMATCH &&
			value == UINT32_MAX);
		CHECK(hx_os_registry_read_string(key, NULL, &text, NULL) ==
				HX_OS_TYPE_MISMATCH &&
			!strcmp(hx_string_c_str(&text), "值中文😀"));
		CHECK(hx_os_registry_read_string(key, "missing", &text,
			      &error) == HX_OS_NOT_FOUND &&
			error.domain == HX_OS_ERROR_WIN32);
		CHECK(hx_os_registry_open_user(context, path,
			      HX_OS_REGISTRY_READ, &reader, NULL) == HX_OS_OK);
		CHECK(hx_os_registry_write_u32(reader, "denied", 1, NULL) ==
			HX_OS_ACCESS_DENIED);
		CHECK(hx_os_registry_close(&reader, NULL) == HX_OS_OK);
		CHECK(MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 256));
		CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, wide, 0, KEY_SET_VALUE,
			      &native) == ERROR_SUCCESS);
		CHECK(RegSetValueExW(native, L"odd", 0, REG_SZ, odd,
			      sizeof(odd)) == ERROR_SUCCESS);
		CHECK(RegSetValueExW(native, L"unterminated", 0, REG_SZ,
			      (const BYTE *)unterminated,
			      sizeof(unterminated)) == ERROR_SUCCESS);
		CHECK(RegSetValueExW(native, L"embedded", 0, REG_SZ,
			      (const BYTE *)embedded,
			      sizeof(embedded)) == ERROR_SUCCESS);
		CHECK(RegSetValueExW(native, L"expand", 0, REG_EXPAND_SZ,
			      (const BYTE *)L"%HX_LITERAL%",
			      sizeof(L"%HX_LITERAL%")) == ERROR_SUCCESS);
		CHECK(RegCloseKey(native) == ERROR_SUCCESS);
		CHECK(hx_os_registry_read_string(key, "odd", &text, NULL) ==
			HX_OS_SYNTAX);
		CHECK(hx_os_registry_read_string(
			      key, "embedded", &text, NULL) == HX_OS_SYNTAX);
		CHECK(hx_os_registry_read_string(
			      key, "unterminated", &text, NULL) == HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "ok"));
		CHECK(hx_os_registry_read_string(key, "expand", &text, NULL) ==
				HX_OS_OK &&
			!strcmp(hx_string_c_str(&text), "%HX_LITERAL%"));
		CHECK(hx_os_registry_close(&key, NULL) == HX_OS_OK && !key);
		CHECK(RegDeleteKeyW(HKEY_CURRENT_USER, wide) == ERROR_SUCCESS);
		hx_owned_key = 0;
		CHECK(hx_os_context_destroy(&context, NULL) == HX_OS_OK);
	}
#else
	(void)reader;
	(void)duplicate;
	(void)context;
	(void)error;
	CHECK(0); /* Non-Windows capability must remain absent. */
#endif
	hx_string_destroy(&text);
	return 0;
}
