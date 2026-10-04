#include "system_private.h"
#include <windows.h>
#include <shellapi.h>

uint64_t hx_os_i_process_id(void)
{
	return GetCurrentProcessId();
}

hx_os_status hx_os_i_host_name(hx_string *out, hx_os_error *error)
{
	wchar_t buffer[256];
	DWORD count = 256;

	if (!GetComputerNameExW(ComputerNameDnsHostname, buffer, &count))
		return hx_os_i_win32(error, GetLastError());

	return hx_string_assign_encoded(out, HX_OS_UTF16LE, buffer,
		(size_t)count * sizeof(*buffer), error);
}

hx_os_status hx_os_i_arguments(
	hx_os_context *context, hx_os_string_list **out, hx_os_error *error)
{
	int count, i;

	wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &count);

	size_t bytes = 0, offset = 0;
	char *block = NULL;
	hx_os_status status = HX_OS_OK;

	if (!arguments)
		return hx_os_i_win32(error, GetLastError());

	if (count < 0 || (unsigned)count > HX_OS_I_SYSTEM_ITEMS) {
		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
		goto done;
	}

	for (i = 0; i < count; ++i) {
		int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			arguments[i], -1, NULL, 0, NULL, NULL);
		if (!needed) {
			status = hx_os_i_error(error, HX_OS_INVALID_ENCODING,
				HX_OS_ERROR_WIN32, GetLastError());
			goto done;
		}

		if ((size_t)needed > HX_OS_I_SYSTEM_BYTES - bytes) {
			status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
			goto done;
		}

		bytes += (size_t)needed;
	}

	block = hx_os_i_alloc(context, bytes, 0);
	if (!block) {
		status = hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);
		goto done;
	}

	for (i = 0; i < count; ++i) {
		int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
			arguments[i], -1, block + offset, (int)(bytes - offset),
			NULL, NULL);
		if (!written) {
			status = hx_os_i_error(error, HX_OS_INVALID_ENCODING,
				HX_OS_ERROR_WIN32, GetLastError());
			goto done;
		}

		offset += (size_t)written;
	}

	status = hx_os_i_strings_from_block(
		context, block, bytes, 1, out, error);

done:
	hx_os_memory_free(block);
	LocalFree(arguments);

	return status;
}

hx_os_status hx_os_i_emulated(int *out, hx_os_error *error)
{
	typedef BOOL(WINAPI * hx_os_wow64_fn)(HANDLE, USHORT *, USHORT *);

	HMODULE kernel = GetModuleHandleW(L"kernel32.dll");

	FARPROC symbol =
		kernel ? GetProcAddress(kernel, "IsWow64Process2") : NULL;
	hx_os_wow64_fn function = NULL;
	BOOL legacy;

	memcpy(&function, &symbol, sizeof(function));
	if (function) {
		USHORT process_machine, native_machine;

		if (!function(GetCurrentProcess(), &process_machine,
			    &native_machine))
			return hx_os_i_win32(error, GetLastError());
		*out = process_machine != IMAGE_FILE_MACHINE_UNKNOWN;
	}
	else {
		if (!IsWow64Process(GetCurrentProcess(), &legacy))
			return hx_os_i_win32(error, GetLastError());

		*out = legacy != FALSE;
	}

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

uint64_t hx_os_i_cpu_native_features(void)
{
#if defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC)
#ifdef PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
	return IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE)
		? HX_OS_CPU_NEON
		: 0;
#else
	return 0;
#endif
#else
	return 0;
#endif
}
