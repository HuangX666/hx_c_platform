# Windows version, window and GPU queries

[简体中文](WINDOWS_UTILITIES.zh-CN.md) · [WER LocalDumps](LOCAL_DUMPS.md) · [README](../README.md)

Use the Windows utilities module (`<hx_os.h>` / `<hx/Windows.hpp>`) when your application needs to inspect PE version resources inside a `.dll` or `.exe` (such as four-part file/product versions, `FileDescription`, or `ProductName`), query an existing `HWND` window's geometry and monitor, or enumerate the system's DXGI GPU adapters.

To see the queries in action, build `hx_os_windows_info` from [windows_info.c](../examples/windows_info.c) following the [run instructions](../examples/README.md#run). Running it with no arguments enumerates the machine's GPUs (printing adapter names, LUIDs, and memory capacities); passing the path to a `.dll` or `.exe` that embeds version resources also prints that file's version metadata first. On non-Windows platforms, these entry points return `HX_OS_NOT_SUPPORTED`.

## 1. Read a DLL or executable version

Pass any UTF-8 file path to `hx_os_windows_version_query`. Internally, the library resolves relative paths against the current working directory (without searching DLL load paths), converts the path to UTF-16, reads the bounded version resource block, and frees temporary storage before returning—**inspecting the file passively without loading or executing the binary**:

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

Include `<hx_os.h>` and `<stdio.h>`:

- **Four-part version numbers and struct sizing**: `version.file` holds `[major, minor, build, revision]`, and `version.product` holds the corresponding four-part product version. `flags`, `type`, and `subtype` preserve the native Windows `VS_FIXEDFILEINFO` fields. Every struct query validates the caller-supplied buffer size and leaves the destination struct and any extra trailing bytes untouched on failure.
- **Named version strings (`hx_os_windows_version_string`)**: To read text fields such as `FileDescription`, `CompanyName`, `ProductName`, or `FileVersion`, query directly into an `hx_string`:

```c
hx_string description = HX_STRING_INIT;

hx_os_status status = hx_os_windows_version_string(
	"plugin.dll", "FileDescription", &description, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&description));

hx_string_destroy(&description);
```

By default, `hx_os_windows_version_string` searches the resource's translation table (`VarFileInfo\Translation`) in resource order and returns the first non-empty match without altering the process locale. Use `hx_os_windows_version_string_ex` to select a specific `(language, code_page)` pair (for example, `0x0804, 1200` for Simplified Chinese Unicode when present, or `0, 0` for the default search).

Missing fields or files without version resources return `HX_OS_NOT_FOUND`. Version resource blocks are capped at `16 MiB` with up to `256` translation entries, and field names consist of `1–64` ASCII letters, digits, or underscores.

## 2. Inspect an existing window

`hx_c_platform` does not create GUI windows or depend on toolkits like Qt. Instead, cast an existing Windows `HWND` from your host application to `uintptr_t` to query its geometry, title, class name, or owning process path (the handle is borrowed for the call; window lifetime remains owned by the caller):

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

- **Window geometry and state (`hx_os_windows_window_query`)**: Reports `process_id`, `thread_id`, nearest `monitor` handle, screen-coordinate bounds (right/bottom exclusive; subject to OS DPI virtualization and invisible resize borders without altering the process's DPI awareness mode), and visible/minimized/maximized flags (note that a `WS_VISIBLE` style bit does not guarantee the window is unobscured on screen).
- **Same-process vs. cross-process window titles (`hx_os_windows_window_title`)**:
  - As [Microsoft documents for `GetWindowTextW`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowtextw), querying a window owned by the **current process** sends a synchronous `WM_GETTEXT` message to its window procedure. To avoid cross-thread UI deadlocks, **call `hx_os_windows_window_title` on a same-process window only from its owning thread**; calling from another thread in the same process returns `HX_OS_WOULD_BLOCK` before any message is sent.
  - For a window owned by **another process**, the native OS call reads the window caption without retrieving arbitrary child-control text.
- **Window class and owning executable path**: `hx_os_windows_window_class` queries the UTF-8 window class name, and `hx_os_windows_window_process_path` queries the owning process's executable path using normal access rights without privilege elevation. Text buffers grow on demand up to `32,767` UTF-16 code units; potential truncation returns `HX_OS_LIMIT` rather than a silently shortened string.

## 3. Identify monitors and GPUs

- **Monitor geometry and display-output GPU**: Pass `info.monitor` to `hx_os_windows_monitor_query` to retrieve the monitor rectangle, work-area rectangle, native device name, and primary-monitor flag. Pass the same handle to `hx_os_windows_monitor_gpu_query` to identify the DXGI adapter driving that display output (note that on hybrid-graphics laptops, the adapter driving the display panel is not necessarily the discrete GPU rendering a given application's 3D workload).
- **Enumerating GPUs by index (`hx_os_windows_gpu_query`)**:
  - Call `hx_os_windows_gpu_query(index, &gpu, sizeof(gpu), &error)` starting from `index = 0` until it returns `HX_OS_NOT_FOUND`.
  - Each `hx_os_windows_gpu_info` record reports the UTF-8 adapter description, session-unique `LUID` (comparable within the current OS boot session, though not persistent across reboots), vendor/device/subsystem/revision PCI IDs, total dedicated and shared memory capacities (hardware/driver capacity rather than current free memory), and a software-adapter flag.
  - **DXGI 1.1 scope**: Uses DXGI 1.1 ([CreateDXGIFactory1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-createdxgifactory1), available since Windows 7), creating no D3D rendering device, COM apartment state, or background worker thread, and releasing all DXGI references before returning (bounded to `256` adapters and `256` outputs per adapter). **Never call GPU queries from `DllMain`**; headless, service, or remote sessions may return `HX_OS_NOT_SUPPORTED` or `HX_OS_NOT_FOUND`.

## 4. C++11 and a runnable example

Include `<hx/Windows.hpp>` in C++11 to access `hx::Result<T>` wrappers without exceptions:

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

(Include `<cstdio>` for `std::puts`.) The `hx::Windows` namespace also provides `window`, `windowTitle`, `windowClass`, `windowProcessPath`, `monitor`, `gpu`, and `monitorGpu`.

After building `hx_os_windows_info` with the Visual Studio generator, run it from the repository root:

```powershell
.\build\bin\Release\hx_os_windows_info.exe
```

You can also pass the path to any `.exe` or `.dll` containing version resources on the command line to print its version numbers and `FileDescription` alongside the machine's GPU list (note: the CLI example uses `HX_OS_ENABLE_SYSTEM_UTILS` to read native UTF-8 arguments so non-ASCII paths do not depend on the Windows ANSI code page, while the Windows query library itself does not depend on `SYSTEM_UTILS`).

## 5. Modules and verification

Two independent CMake options control these adapters (both `ON` by default):
- `HX_OS_ENABLE_WINDOWS_VERSION`: Enables PE version-resource queries and links `Version.lib`.
- `HX_OS_ENABLE_WINDOWS_DESKTOP`: Enables window, monitor, and DXGI GPU queries and links `User32.lib`, `DXGI.lib`, and `DXGUID.lib` (propagated automatically through CMake exports and static `pkg-config` metadata).

Non-Windows builds and builds with these options turned `OFF` retain the declarations and return `HX_OS_NOT_SUPPORTED`. See [platform support](PLATFORMS.md) for compiler and OS baseline details.
