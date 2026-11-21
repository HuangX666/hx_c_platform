# Windows crash files with WER LocalDumps

[简体中文](LOCAL_DUMPS.zh-CN.md) · [Windows utilities](WINDOWS_UTILITIES.md) · [Analysis tools](ANALYSIS.md) · [README](../README.md)

When a Windows application crashes on a customer machine due to an unhandled exception, a local `.dmp` crash dump is often the fastest way to pinpoint the root cause. The `WER LocalDumps` module (`<hx_os_local_dumps.h>` / `<hx/LocalDumps.hpp>`) provides an explicit API for configuring Windows Error Reporting (WER) to save a local `.dmp` file whenever a specified `.exe` crashes.

Keep the module's scope in mind: **it configures, queries, and removes per-application Windows `LocalDumps` registry settings**. It does not install an in-process exception handler, write minidumps itself, detect memory corruption prior to a crash, monitor hung processes, or upload customer data to Microsoft or any remote server. Simply compiling, linking, or loading the library never modifies Windows settings automatically.

`HX_OS_ENABLE_LOCAL_DUMPS` defaults to `ON` and is independent of `REGISTRY`, `PROCESS`, and `SYSTEM_UTILS` (linking `Advapi32.lib` on Windows). On non-Windows targets or when built with the module disabled, all functions remain linkable and return `HX_OS_NOT_SUPPORTED`; call `hx_os_local_dumps_available()` to check whether compiled Windows support is present.

## 1. Configure the customer application during installation

Because WER `LocalDumps` settings live under `HKEY_LOCAL_MACHINE` (`HKLM`), configuring them requires administrator privileges. The standard practice is to call `hx_os_local_dumps_enable` once from an **authorized, elevated installer or deployment script**. Because the registry setting persists across reboots, end users can run the application under a standard non-elevated user account on every launch.

The complete C program below configures local crash dumps for **`MyApp.exe`** (replace `"MyApp.exe"` with your application's executable basename before running; build using the [C project setup](USAGE.md#c-project)):

```c
#include <hx_os_local_dumps.h>
#include <stdio.h>

int main(void)
{
	hx_os_error error;
	hx_os_status status =
		hx_os_local_dumps_enable("MyApp.exe", NULL, &error);

	if (status != HX_OS_OK) {
		fprintf(stderr, "%s; native=%lld; cleanup=%lld\n",
			hx_os_status_name(status), (long long)error.native_code,
			(long long)error.cause_code);
		return 1;
	}

	puts("LocalDumps configured for MyApp.exe.");
	return 0;
}
```

- **Target executable basename (`application`)**:
  - Accepts a single UTF-8 filename ending in `.exe` (up to `255` UTF-16 code units)—**never a directory path, wildcard, or `.dll` name**.
  - Passing `application = NULL` selects the current **host `.exe` basename** (even if called from inside a DLL). An external installer (`setup.exe`) should therefore always pass the target application's `.exe` basename explicitly.
  - Windows matches executable names case-insensitively; two installations with the same `.exe` basename share the setting in that registry view.
- **Default settings (`options = NULL`)**:
  - **Dump type**: Full dump (`HX_OS_LOCAL_DUMPS_FULL`), chosen intentionally over Windows' built-in mini-dump default so engineers have complete memory state during post-mortem debugging.
  - **Dump count**: Up to `3` retained dump files.
  - **Registry view**: The calling process's native registry view (`HX_OS_LOCAL_DUMPS_VIEW_DEFAULT`).
  - **Dump folder**: The literal string `"%LOCALAPPDATA%\\CrashDumps"` (written as `REG_EXPAND_SZ` so Windows expands it at crash time under the **crashing process's user account**, rather than expanding it to the administrator's profile during installation).
- **Permissions and security boundaries**:
  - Writes only the `<MyApp.exe>` subkey under `HKLM\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps`, without altering global WER values, changing folder ACLs, elevating privileges, or creating directories on disk. Insufficient privileges return `HX_OS_ACCESS_DENIED` with native Win32 details preserved in `hx_os_error`.
  - **Data sensitivity**: Full dumps can be large and may contain sensitive in-memory data (credentials, tokens, or open documents), and limiting file count does not cap total disk bytes. Document dump handling in your privacy policy, and never call these configuration APIs from `DllMain`, a signal handler, or an exception handler.

## 2. Choose folder, count, dump type and registry view

To customize the dump folder, maximum file count, dump type (`HX_OS_LOCAL_DUMPS_MINI` or `HX_OS_LOCAL_DUMPS_FULL`), or 32/64-bit registry view, initialize `hx_os_local_dumps_options` with `hx_os_local_dumps_options_init`:

```c
hx_os_local_dumps_options options;
hx_os_error error;

hx_os_status status =
	hx_os_local_dumps_options_init(&options, sizeof(options));
if (status == HX_OS_OK) {
	options.dump_count = 5;
	options.dump_type = HX_OS_LOCAL_DUMPS_FULL;
	options.registry_view = HX_OS_LOCAL_DUMPS_VIEW64;
	options.dump_folder = "%LOCALAPPDATA%\\MyApp\\CrashDumps";
	status = hx_os_local_dumps_enable("MyApp.exe", &options, &error);
}
```

- **Custom folder format**: `dump_folder` must be a strict UTF-8 absolute Windows path or start with a `%VARIABLE%` reference that expands to one (up to `32,767` UTF-16 code units, stored unexpanded as `REG_EXPAND_SZ`). When provisioning a custom directory, grant write permissions to the account that runs the application without making customer crash dumps readable by untrusted users.
- **Selecting 32-bit vs. 64-bit registry views**: `HX_OS_LOCAL_DUMPS_VIEW_DEFAULT` uses the calling process's architecture rather than inspecting a target binary on disk. If a 32-bit installer configures a 64-bit application (or vice versa), select `HX_OS_LOCAL_DUMPS_VIEW64` or `HX_OS_LOCAL_DUMPS_VIEW32` explicitly. On 64-bit Windows, if you ship both x86 and x64 binaries with the same name, you can call `enable` once for `VIEW32` and once for `VIEW64`, and pass the same view when querying or removing.

## 3. Existing settings and safe removal

To avoid clobbering policies configured by system administrators or other tools, the module **never silently overwrites an existing application subkey**:

- **Managed marker and idempotent `enable`**: When creating a subkey, the library writes `DumpFolder`, `DumpCount`, `DumpType`, and a private marker value `HxLocalDumpsVersion = 1` (`REG_DWORD`). Repeating `hx_os_local_dumps_enable` succeeds (`HX_OS_OK`) only if the existing subkey carries `HxLocalDumpsVersion = 1`, matches all requested values, and has no extra values or subkeys. Any other pre-existing key returns `HX_OS_EXISTS` untouched.
- **Safe removal (`hx_os_local_dumps_remove`)**:

```c
hx_os_status status = hx_os_local_dumps_remove("MyApp.exe", NULL, &error);
```

Passing `options = NULL` removes a default configuration created by this module (or succeeds immediately if the subkey is already absent). If you enabled custom `options`, pass those same `options` to `remove`. If the key was created by another tool, lacks the library marker, has been modified (the unexpanded `DumpFolder` string must match literally), or contains extra values/subkeys, `remove` returns `HX_OS_BUSY` and leaves the key intact.

> [!NOTE]
> **Removal and Rollback Boundaries**
> - `remove` deletes only the per-application registry subkey; it does **not** delete existing `.dmp` files on disk or parent/global WER keys. Because global WER `LocalDumps` settings may still be configured on the machine, removing a per-application key does not guarantee that all crash dump collection is disabled system-wide.
> - Multi-value registry writes are not atomic transactions. Serialize installation/removal across processes. If a write fails mid-creation, the implementation attempts to delete its newly created subkey and records any rollback cleanup error in `error.cause_code` (query the key before deciding how to retry).

## 4. Query without changing Windows

Use `hx_os_local_dumps_query` to inspect an application's current setting in a chosen registry view without modifying the system:

```c
hx_os_local_dumps_info info;
hx_string folder = HX_STRING_INIT;
hx_os_error error;

hx_os_status status = hx_os_local_dumps_query("MyApp.exe",
	HX_OS_LOCAL_DUMPS_VIEW_DEFAULT, &info, sizeof(info), &folder, &error);
if (status == HX_OS_OK)
	printf("fields=%u type=%u count=%u managed=%u folder=%s\n",
		(unsigned)info.fields, (unsigned)info.dump_type,
		(unsigned)info.dump_count, (unsigned)info.managed,
		hx_string_c_str(&folder));

hx_string_destroy(&folder);
```

- **Present-field bitmask (`info.fields`)**: Combines `HX_OS_LOCAL_DUMPS_HAS_TYPE`, `HX_OS_LOCAL_DUMPS_HAS_COUNT`, and `HX_OS_LOCAL_DUMPS_HAS_FOLDER` to indicate which values exist in the subkey (missing numeric fields report `0`, and a missing folder reports `""`). Both `REG_EXPAND_SZ` and `REG_SZ` folder values are accepted and returned literally.
- **`info.managed` and `HX_OS_NOT_FOUND`**: `info.managed` is non-zero when the subkey carries the complete library marker layout with no extra values or subkeys. When no subkey exists for that application in the selected view, the query returns `HX_OS_NOT_FOUND` (which means no per-application override is present, not that global WER settings are disabled).

## 5. C++11 and the safe command-line example

Include `<hx/LocalDumps.hpp>` in C++11 to call `enable`, `query`, and `remove` via `hx::Result` without exceptions (destroying a `query` result releases its `hx::String` memory without removing persistent OS registry settings):

```cpp
auto enabled = hx::LocalDumps::enable("MyApp.exe");
if (!enabled)
	return enabled.status();

auto settings = hx::LocalDumps::query("MyApp.exe");
if (!settings)
	return settings.status();

return HX_OS_OK;
```

The public command-line example [local_dumps.c](../examples/local_dumps.c) prints usage text when run without arguments, making no registry changes and triggering no crash. From an MSVC build directory with `HX_OS_BUILD_EXAMPLES=ON`, you can build and run a read-only query from a normal shell:

```powershell
cmake --build build-shared-msvc --config Release --target hx_os_local_dumps
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe query MyApp.exe 64
```

Only an explicit `enable` or `remove` command writes to the registry. From an elevated administrator PowerShell, for an application you manage (passing `-` selects the example's own `.exe`, and an optional trailing argument sets a custom folder):

```powershell
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe enable MyApp.exe 64
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe remove MyApp.exe 64
```

## 6. What to collect from customers

1. **Archive matching binaries and PDBs at release time**: Keep the exact released `.exe`, `.dll`, and `.pdb` symbol files alongside the source commit on the engineering side. End users only need the runtime binaries—Windows writes `.dmp` files without needing `.pdb` symbols on the customer's machine.
2. **Collect crash artifacts**: After a crash, ask the user for the `.dmp` file from `%LOCALAPPDATA%\CrashDumps` (service accounts use their own profile directories), together with the exact software version, timestamp, logs, and reproduction steps.
3. **Open in Visual Studio or WinDbg**: Load the `.dmp` with matching `.pdb` symbols to inspect the faulting stack and variables. Because heap corruption can crash long after the invalid write occurred, pair post-mortem dumps with the memory-checking workflows in [Optional CPU and memory analysis](ANALYSIS.md).
4. **Hangs and crash-handler interactions**: Hung processes do not trigger `LocalDumps` automatically; capture a live dump manually (for example, via Task Manager's *Details -> Create dump file* command) before terminating the process. Also note that custom in-process crash reporters that swallow unhandled exceptions or registered JIT debuggers (`AeDebug`) can prevent WER `LocalDumps` from running.

## 7. Verification boundaries

Verify registry configuration and end-to-end OS crash dump delivery as separate steps:
- Automated unit tests redirect `HKLM` inside an isolated test process to a temporary `HKCU` test tree to verify Unicode handling, conflict detection, permissions, registry views, and rollback cleanup without touching machine-wide settings.
- Before shipping, verify actual `.dmp` creation on your target Windows versions, architectures, user accounts, and output folders using a disposable test executable, and remove the test setting afterward (never force a crash in a production application with unsaved work).

References: [Microsoft LocalDumps configuration](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps) and [Accessing an Alternate Registry View](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view).
