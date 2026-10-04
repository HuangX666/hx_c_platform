#include "sysinfo_private.h"

#include <windows.h>
#include <psapi.h>
#include <stdio.h>

static hx_os_status hx_os_sys_physical(uint64_t *out, hx_os_error *error)
{
	DWORD bytes = 0, code;
	unsigned char *data;
	size_t offset = 0;
	uint64_t count = 0;

	if (GetLogicalProcessorInformationEx(
		    RelationProcessorCore, NULL, &bytes) ||
		GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return hx_os_i_win32(error, GetLastError());
	if (!bytes || bytes > 16 * 1024 * 1024)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	data = hx_os_i_alloc(NULL, bytes, 0);
	if (!data)
		return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

	if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
		    (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)data, &bytes)) {
		code = GetLastError();

		hx_os_memory_free(data);

		return hx_os_i_win32(error, code);
	}

	while (offset < bytes) {
		SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *entry =
			(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *)(data +
				offset);

		if (bytes - offset < sizeof(DWORD) * 2 ||
			entry->Size < sizeof(DWORD) * 2 ||
			entry->Size > bytes - offset) {
			hx_os_memory_free(data);

			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
		}

		if (entry->Relationship == RelationProcessorCore)
			++count;

		offset += entry->Size;
	}

	hx_os_memory_free(data);
	if (!count)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	*out = count;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_value(int field, uint64_t *out, hx_os_error *error)
{
	MEMORYSTATUSEX memory;
	SYSTEM_INFO info;
	uint64_t value;

	if (field == HX_OS_SYS_PHYSICAL)
		return hx_os_sys_physical(out, error);

	if (field == HX_OS_SYS_TOTAL || field == HX_OS_SYS_AVAILABLE) {
		memset(&memory, 0, sizeof(memory));
		memory.dwLength = sizeof(memory);
		if (!GlobalMemoryStatusEx(&memory))
			return hx_os_i_win32(error, GetLastError());

		value = field == HX_OS_SYS_TOTAL ? memory.ullTotalPhys
						 : memory.ullAvailPhys;
	}
	else if (field == HX_OS_SYS_LOGICAL) {
		value = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
		if (!value)
			return hx_os_i_win32(error, GetLastError());
	}
	else {
		GetNativeSystemInfo(&info);
		value = info.dwPageSize;
	}

	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_disk(
	const char *path, hx_os_sys_disk_space_info *out, hx_os_error *error)
{
	hx_os_error local;
	wchar_t *wide;
	ULARGE_INTEGER available, total, free_bytes;
	DWORD code;

	if (!error)
		error = &local;

	wide = hx_os_i_windows_path(NULL, path, error);
	if (!wide)
		return error->status;

	code = GetDiskFreeSpaceExW(wide, &available, &total, &free_bytes)
		? ERROR_SUCCESS
		: GetLastError();

	hx_os_memory_free(wide);
	if (code)
		return hx_os_i_win32(error, code);

	out->total_bytes = total.QuadPart;
	out->available_bytes = available.QuadPart;
	out->free_bytes = free_bytes.QuadPart;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_memory(
	hx_os_sys_proc_memory_info *out, hx_os_error *error)
{
	PROCESS_MEMORY_COUNTERS counters;
	MEMORY_BASIC_INFORMATION region;
	SYSTEM_INFO info;
	uintptr_t address, limit;
	uint64_t virtual_bytes = 0;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);
	if (!GetProcessMemoryInfo(
		    GetCurrentProcess(), &counters, sizeof(counters)))
		return hx_os_i_win32(error, GetLastError());
	GetSystemInfo(&info);
	address = (uintptr_t)info.lpMinimumApplicationAddress;
	limit = (uintptr_t)info.lpMaximumApplicationAddress;

	while (address <= limit) {
		uintptr_t base;

		if (!VirtualQuery(
			    (const void *)address, &region, sizeof(region)))
			return hx_os_i_win32(error, GetLastError());
		if (region.State == MEM_COMMIT || region.State == MEM_RESERVE)
			virtual_bytes += region.RegionSize;

		base = (uintptr_t)region.BaseAddress;
		if (!region.RegionSize ||
			region.RegionSize > UINTPTR_MAX - base)
			break;
		if (base + region.RegionSize <= address)
			return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

		address = base + region.RegionSize;
	}

	out->resident_bytes = counters.WorkingSetSize;
	out->virtual_bytes = virtual_bytes;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_version(
	int numbers[3], char text[256], hx_os_error *error)
{
	typedef LONG(WINAPI * hx_os_rtl_version_fn)(OSVERSIONINFOW *);

	hx_os_rtl_version_fn version_fn;
	FARPROC symbol;
	OSVERSIONINFOW version;

	HMODULE module = GetModuleHandleW(L"ntdll.dll");
	if (!module)
		return hx_os_i_win32(error, GetLastError());

	symbol = GetProcAddress(module, "RtlGetVersion");
	if (!symbol)
		return hx_os_i_error(error, HX_OS_NOT_SUPPORTED, 0, 0);

	memcpy(&version_fn, &symbol, sizeof(version_fn));
	memset(&version, 0, sizeof(version));
	version.dwOSVersionInfoSize = sizeof(version);
	if (version_fn(&version) != 0)
		return hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);

	numbers[0] = (int)version.dwMajorVersion;
	numbers[1] = (int)version.dwMinorVersion;
	numbers[2] = (int)version.dwBuildNumber;
	(void)snprintf(text, 256, "Windows %d.%d.%d", numbers[0], numbers[1],
		numbers[2]);

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

hx_os_status hx_os_i_sys_cpu(uint64_t *nanoseconds, hx_os_error *error)
{
	FILETIME creation, exit_time, kernel, user;
	uint64_t ticks;

	if (!GetProcessTimes(
		    GetCurrentProcess(), &creation, &exit_time, &kernel, &user))
		return hx_os_i_win32(error, GetLastError());
	ticks = ((uint64_t)kernel.dwHighDateTime << 32) + kernel.dwLowDateTime;
	ticks += ((uint64_t)user.dwHighDateTime << 32) + user.dwLowDateTime;
	if (ticks > UINT64_MAX / 100)
		return hx_os_i_error(error, HX_OS_RANGE, 0, 0);

	*nanoseconds = ticks * 100;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}
