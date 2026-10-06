# Windows version, window and GPU queries

[简体中文](WINDOWS_UTILITIES.zh-CN.md) · [README](../README.md)

## 1. Read a DLL or executable version

Pass a UTF-8 file path. The library resolves relative paths against the current directory, converts the Windows path, bounds the resource allocation and releases it before returning. This reads the resource without executing the target file. It does not use DLL search paths.

```c
hx_os_windows_version_info version;
hx_os_error error;

hx_os_status status = hx_os_windows_version_query(
	"plugin.dll", &version, sizeof(version), &error);
if (status == HX_OS_OK) {
	printf("%u.%u.%u.%u\n", (unsigned)version.file[0],
		(unsigned)version.file[1], (unsigned)version.file[2],
		(unsigned)version.file[3]);
}
```

Include `<hx_os.h>` and `<stdio.h>`. The four components are major/minor/build/revision; `product` contains the corresponding product version. Flags, type and subtype retain Windows resource meanings. `size` and `version` describe the written v1 record. All structure queries require enough caller capacity, write only the current record and leave both the output and extra trailing bytes unchanged on failure.

For text such as `FileDescription`, `CompanyName`, `ProductName` or `FileVersion`, write directly into a string:

```c
hx_string description = HX_STRING_INIT;

hx_os_status status = hx_os_windows_version_string(
	"plugin.dll", "FileDescription", &description, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&description));

hx_string_destroy(&description);
```

The default searches translations in resource order and returns the first containing the field, without changing the process locale. `_string_ex` selects a language/code-page pair; `0x0804, 1200` selects Simplified Chinese with Unicode storage, when present. Pair `0, 0` requests the default search. Missing fields or absent resources return `NOT_FOUND`; invalid resources, permission failures and allocation errors remain explicit. Resource bytes are limited to 16 MiB and translation entries to 256. A field name is 1–64 ASCII letters/digits/underscores. Native errors are preserved in `hx_os_error`.

## 2. Inspect an existing window

The library does not create a GUI or depend on Qt. Cast an existing Windows `HWND` to `uintptr_t` and query it. These handles are borrowed; the caller controls the window lifetime. Windows can reuse a destroyed handle.

```c
hx_os_windows_window_info info;
hx_string title = HX_STRING_INIT;
uintptr_t window = (uintptr_t)existing_hwnd;

hx_os_status status = hx_os_windows_window_query(
	window, &info, sizeof(info), NULL);
if (status == HX_OS_OK)
	printf("Process: %u; thread: %u\n", info.process_id, info.thread_id);

status = hx_os_windows_window_title(window, &title, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&title));

hx_string_destroy(&title);
```

`existing_hwnd` comes from the host application. Include its native Windows headers in that integration code; the public library header does not expose those types. Qt code may pass its existing window ID on Windows. For a window in the current process, call the title query on that window's owning thread. A different thread gets `WOULD_BLOCK` before a message is sent. The owning-thread operation can reenter its window procedure and has no hard deadline. For another process the native API reads the caption; it does not retrieve arbitrary remote control text. [Microsoft documents these title-reading differences](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextw).

The window record includes process/thread IDs, nearest monitor, bounds and visible/minimized/maximized flags. A visible style does not prove that the window is unobscured. Bounds use Windows screen coordinates, right/bottom exclusive; native DPI virtualization and invisible resize borders can affect them. No DPI mode is changed. These are observations of changing UI state, not an atomic desktop snapshot.

`hx_os_windows_window_class` returns the UTF-8 class name. `hx_os_windows_window_process_path` returns the owning executable path with normal access checks, without elevation. Text buffers start small and grow within a 32,767 UTF-16-unit limit; truncation reports `LIMIT`, never a successful shortened string. String outputs use their own context and remain unchanged on failure. Serialize access to each destination string.

## 3. Identify monitors and GPUs

Pass `info.monitor` to `hx_os_windows_monitor_query` for monitor/work-area rectangles, a native device name and the primary flag. Pass the same handle to `hx_os_windows_monitor_gpu_query` to find the adapter owning that display output. On hybrid systems this does not necessarily identify the GPU rendering an application's content.

`hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), &error)` enumerates current adapters starting at zero; `NOT_FOUND` ends enumeration. It returns UTF-8 description, LUID, vendor/device/subsystem/revision IDs, memory capacities and a software-adapter flag. Names need not be unique. Memory values are native capacities, not current free memory. LUIDs are comparable within the current OS session, not permanent IDs across reboot; enumeration indices can change after hot-plug. A new call creates a fresh factory, so repeated index calls do not form an atomic snapshot.

The adapter uses DXGI 1.1, available on Windows 7. It creates no rendering device or COM apartment, starts no worker, and releases each DXGI reference before returning. Do not invoke it from `DllMain`. Native graphics work has no hard deadline. Lookup is bounded to 256 adapters and 256 outputs per adapter. A headless/service/remote environment can return an explicit unsupported, unavailable or not-found result. [CreateDXGIFactory1 contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1).

## 4. C++11 and a runnable example

Include `<hx/Windows.hpp>`. In a function returning `hx_os_status`:

```cpp
auto version = hx::Windows::fileVersion("plugin.dll");
if (!version)
	return version.status();

auto description = hx::Windows::versionString("plugin.dll", "FileDescription");
if (!description)
	return description.status();

std::puts(description.value().c_str());
return HX_OS_OK;
```

Include `<cstdio>` for `std::puts`. The facade also provides `window`, `windowTitle`, `windowClass`, `windowProcessPath`, `monitor`, `gpu` and `monitorGpu`, returning `Result` without exceptions. It reuses the C implementation and preserves native error details.

Build `hx_os_windows_info` and run `build/bin/Release/hx_os_windows_info.exe build/bin/Release/hx_os_test_windows_util.exe` with the Visual Studio generator. [windows_info.c](../examples/windows_info.c) is the complete program. The fixture has version `1.2.345.678` and description `Window helper fixture`; GPU output depends on the machine. The CLI example uses SYSTEM_UTILS for UTF-8 command-line arguments, so Chinese paths do not depend on the Windows ANSI code page. The query library itself does not require SYSTEM_UTILS.

## 5. Modules and verification

`HX_OS_ENABLE_WINDOWS_VERSION` and `HX_OS_ENABLE_WINDOWS_DESKTOP` both default to ON and can be disabled independently. Version queries link the system Version library; desktop queries link User32/DXGI/DXGUID. CMake exports and pkg-config static dependencies carry these links. Other operating systems keep the declarations and return `NOT_SUPPORTED`. Availability queries indicate a compiled adapter, not the presence of a particular window, display, resource or GPU.

Tests use owned resources, a hidden window, UTF-8 paths and injected allocator failures. They cover capacity preservation, resource translations, long and empty titles, cross-thread rejection, monitor/GPU observations, feature switches and context reclamation. See [validation](VALIDATION.md) for exact native, Win32, sanitizer and installed-consumer results. No Win7/8.1 target run is implied by the use of baseline-compatible APIs.
