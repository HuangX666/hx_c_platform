#include "internal.h"
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

typedef NTSTATUS(WINAPI *hx_os_bcrypt_random_fn)(
	BCRYPT_ALG_HANDLE, PUCHAR, ULONG, ULONG);

hx_os_status hx_os_i_random_bytes(
	void *buffer, size_t bytes, hx_os_error *error)
{
	wchar_t path[MAX_PATH];
	HMODULE module = NULL;
	hx_os_bcrypt_random_fn function = NULL;
	unsigned char *output = buffer;
	HCRYPTPROV provider = 0;

	UINT count = GetSystemDirectoryW(path, MAX_PATH);
	if (count && count < MAX_PATH - 12) {
		static const wchar_t suffix[] = L"\\bcrypt.dll";
		FARPROC symbol;

		memcpy(path + count, suffix, sizeof(suffix));
		module = LoadLibraryW(path);
		if (module) {
			symbol = GetProcAddress(module, "BCryptGenRandom");
			memcpy(&function, &symbol, sizeof(function));
		}
	}

	if (!function &&
		!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL,
			CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
		DWORD code = GetLastError();
		if (module)
			FreeLibrary(module);

		return hx_os_i_win32(error, code);
	}

	while (bytes) {
		ULONG chunk = bytes > ULONG_MAX ? ULONG_MAX : (ULONG)bytes;

		if (function) {
			NTSTATUS code = function(NULL, output, chunk,
				BCRYPT_USE_SYSTEM_PREFERRED_RNG);
			if (code < 0) {
				FreeLibrary(module);

				return hx_os_i_error(error, HX_OS_IO_ERROR,
					HX_OS_ERROR_NTSTATUS, code);
			}
		}
		else if (!CryptGenRandom(provider, chunk, output)) {
			DWORD code = GetLastError();
			(void)CryptReleaseContext(provider, 0);
			if (module)
				FreeLibrary(module);

			return hx_os_i_win32(error, code);
		}

		output += chunk;
		bytes -= chunk;
	}

	if (provider)
		(void)CryptReleaseContext(provider, 0);

	if (module)
		FreeLibrary(module);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
