#define COBJMACROS
#include "windows_util_private.h"

#include <dxgi.h>

static hx_os_status hx_os_desktop_error(hx_os_error *error, DWORD code)
{
	return code ? hx_os_i_win32(error, code)
		    : hx_os_i_error(error, HX_OS_IO_ERROR, 0, 0);
}

static hx_os_windows_rect hx_os_desktop_rect(RECT value)
{
	hx_os_windows_rect out;

	out.left = value.left;
	out.top = value.top;
	out.right = value.right;
	out.bottom = value.bottom;

	return out;
}

hx_os_status hx_os_i_windows_window_query(
	uintptr_t window, hx_os_windows_window_info *out, hx_os_error *error)
{
	hx_os_windows_window_info value = {0};
	HWND handle = (HWND)window;
	DWORD process = 0, thread;
	RECT rectangle;

	thread = GetWindowThreadProcessId(handle, &process);
	if (!thread || !GetWindowRect(handle, &rectangle))
		return hx_os_desktop_error(error, GetLastError());

	value.size = sizeof(value);
	value.version = 1;
	value.monitor =
		(uintptr_t)MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
	value.bounds = hx_os_desktop_rect(rectangle);
	value.process_id = process;
	value.thread_id = thread;
	if (IsWindowVisible(handle))
		value.flags |= HX_OS_WINDOWS_WINDOW_VISIBLE;
	if (IsIconic(handle))
		value.flags |= HX_OS_WINDOWS_WINDOW_MINIMIZED;
	if (IsZoomed(handle))
		value.flags |= HX_OS_WINDOWS_WINDOW_MAXIMIZED;

	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_desktop_process_text(
	DWORD process, wchar_t *text, DWORD *units, hx_os_error *error)
{
	HANDLE handle =
		OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process);
	DWORD code;

	if (!handle)
		return hx_os_desktop_error(error, GetLastError());

	SetLastError(0);
	if (!QueryFullProcessImageNameW(handle, 0, text, units)) {
		code = GetLastError();
		(void)CloseHandle(handle);
		if (code == ERROR_INSUFFICIENT_BUFFER)
			return hx_os_i_error(error, HX_OS_OUTPUT_FULL,
				HX_OS_ERROR_WIN32, code);

		return hx_os_desktop_error(error, code);
	}
	if (!CloseHandle(handle))
		return hx_os_desktop_error(error, GetLastError());

	return HX_OS_OK;
}

hx_os_status hx_os_i_windows_window_text(
	uintptr_t window, int kind, hx_string *out, hx_os_error *error)
{
	HWND handle = (HWND)window;
	DWORD process = 0, thread, code = 0;
	DWORD capacity = kind == 1 ? 257u : 256u, units;
	wchar_t *text;
	hx_os_status status = HX_OS_OK;

	thread = GetWindowThreadProcessId(handle, &process);
	if (!thread)
		return hx_os_desktop_error(error, GetLastError());
	if (kind == 0 && process == GetCurrentProcessId() &&
		thread != GetCurrentThreadId())
		return hx_os_i_error(error, HX_OS_WOULD_BLOCK, 0, 0);

	for (;;) {
		text = hx_os_i_alloc(out->context, capacity * sizeof(*text), 0);
		if (!text)
			return hx_os_i_error(error, HX_OS_NO_MEMORY, 0, 0);

		text[0] = 0;
		units = capacity;
		if (kind == 2)
			status = hx_os_desktop_process_text(
				process, text, &units, error);
		else {
			int result;

			SetLastError(0);
			result = kind == 0
				? GetWindowTextW(handle, text, (int)capacity)
				: GetClassNameW(handle, text, (int)capacity);
			code = GetLastError();
			if (result < 0 || (!result && (code || kind == 1)))
				status = hx_os_desktop_error(error, code);
			else {
				units = (DWORD)result;
				status = HX_OS_OK;
			}
		}

		if (status == HX_OS_OK && units >= capacity - 1)
			status = HX_OS_OUTPUT_FULL;
		if (status == HX_OS_OK)
			status = hx_string_assign_wide(out, text, units, error);

		hx_os_memory_free(text);
		if (status != HX_OS_OUTPUT_FULL)
			return status;
		if (capacity == 32769 || kind == 1)
			return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

		capacity = capacity > 16384 ? 32769 : capacity * 2;
	}
}

hx_os_status hx_os_i_windows_monitor_query(
	uintptr_t monitor, hx_os_windows_monitor_info *out, hx_os_error *error)
{
	MONITORINFOEXW native;
	hx_os_windows_monitor_info value = {0};
	size_t required;
	hx_os_status status;

	memset(&native, 0, sizeof(native));
	native.cbSize = sizeof(native);
	if (!GetMonitorInfoW((HMONITOR)monitor, (MONITORINFO *)&native))
		return hx_os_desktop_error(error, GetLastError());

	value.size = sizeof(value);
	value.version = 1;
	value.bounds = hx_os_desktop_rect(native.rcMonitor);
	value.work_area = hx_os_desktop_rect(native.rcWork);
	if (native.dwFlags & MONITORINFOF_PRIMARY)
		value.flags = HX_OS_WINDOWS_MONITOR_PRIMARY;

	status = hx_os_text_from_wide(native.szDevice, wcslen(native.szDevice),
		value.device_name, sizeof(value.device_name), &required, error);
	if (status == HX_OS_OK)
		*out = value;

	return status;
}

static hx_os_status hx_os_gpu_error(hx_os_error *error, HRESULT code)
{
	hx_os_status status = HX_OS_IO_ERROR;

	if (code == DXGI_ERROR_NOT_FOUND)
		status = HX_OS_NOT_FOUND;
	else if (code == E_OUTOFMEMORY)
		status = HX_OS_NO_MEMORY;
	else if (code == E_ACCESSDENIED)
		status = HX_OS_ACCESS_DENIED;
	else if (code == DXGI_ERROR_UNSUPPORTED || code == E_NOINTERFACE)
		status = HX_OS_NOT_SUPPORTED;
	else if (code == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
		status = HX_OS_BUSY;

	return hx_os_i_error(error, status, HX_OS_ERROR_HRESULT, code);
}

static hx_os_status hx_os_gpu_description(
	IDXGIAdapter1 *adapter, hx_os_windows_gpu_info *out, hx_os_error *error)
{
	DXGI_ADAPTER_DESC1 native;
	hx_os_windows_gpu_info value = {0};
	size_t units = 0, required;
	hx_os_status status;
	HRESULT code = IDXGIAdapter1_GetDesc1(adapter, &native);

	if (FAILED(code))
		return hx_os_gpu_error(error, code);

	while (units < 128 && native.Description[units])
		++units;
	if (units == 128)
		return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);

	status = hx_os_text_from_wide(native.Description, units,
		value.description, sizeof(value.description), &required, error);
	if (status != HX_OS_OK)
		return status;

	value.size = sizeof(value);
	value.version = 1;
	value.luid = (uint64_t)(uint32_t)native.AdapterLuid.HighPart << 32 |
		(uint64_t)native.AdapterLuid.LowPart;
	value.dedicated_video_bytes = native.DedicatedVideoMemory;
	value.dedicated_system_bytes = native.DedicatedSystemMemory;
	value.shared_system_bytes = native.SharedSystemMemory;
	value.vendor_id = native.VendorId;
	value.device_id = native.DeviceId;
	value.subsystem_id = native.SubSysId;
	value.revision = native.Revision;
	if (native.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		value.flags = HX_OS_WINDOWS_GPU_SOFTWARE;

	*out = value;

	return hx_os_i_error(error, HX_OS_OK, 0, 0);
}

static hx_os_status hx_os_gpu_has_monitor(IDXGIAdapter1 *adapter,
	uintptr_t monitor, int *found, hx_os_error *error)
{
	UINT index;

	*found = 0;
	for (index = 0; index < 256; ++index) {
		IDXGIOutput *output = NULL;
		DXGI_OUTPUT_DESC description;
		HRESULT code =
			IDXGIAdapter1_EnumOutputs(adapter, index, &output);

		if (code == DXGI_ERROR_NOT_FOUND)
			return HX_OS_OK;
		if (FAILED(code))
			return hx_os_gpu_error(error, code);

		code = IDXGIOutput_GetDesc(output, &description);
		IDXGIOutput_Release(output);
		if (FAILED(code))
			return hx_os_gpu_error(error, code);
		if ((uintptr_t)description.Monitor == monitor) {
			*found = 1;

			return HX_OS_OK;
		}
	}

	return hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
}

hx_os_status hx_os_i_windows_gpu_query(uint32_t index, uintptr_t monitor,
	hx_os_windows_gpu_info *out, hx_os_error *error)
{
	IDXGIFactory1 *factory = NULL;
	HRESULT code =
		CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
	hx_os_status status;

	if (FAILED(code))
		return hx_os_gpu_error(error, code);

	status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	for (; index < 256; ++index) {
		IDXGIAdapter1 *adapter = NULL;
		int found = !monitor;

		code = IDXGIFactory1_EnumAdapters1(factory, index, &adapter);
		if (FAILED(code)) {
			status = hx_os_gpu_error(error, code);

			break;
		}

		if (monitor)
			status = hx_os_gpu_has_monitor(
				adapter, monitor, &found, error);
		else
			status = HX_OS_OK;
		if (status == HX_OS_OK && found)
			status = hx_os_gpu_description(adapter, out, error);

		IDXGIAdapter1_Release(adapter);
		if (status != HX_OS_OK || found)
			break;

		status = hx_os_i_error(error, HX_OS_LIMIT, 0, 0);
	}

	IDXGIFactory1_Release(factory);

	return status;
}
