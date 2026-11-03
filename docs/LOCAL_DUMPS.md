# Windows crash files with WER LocalDumps

[简体中文](LOCAL_DUMPS.zh-CN.md) · [Windows utilities](WINDOWS_UTILITIES.md) · [Analysis tools](ANALYSIS.md) · [README](../README.md)

**Use this module to explicitly ask Windows to save a local `.dmp` file after an application crashes.** It configures Windows; it does not catch exceptions, write a dump itself, diagnose memory corruption, recover a damaged process, monitor hangs or upload customer data. Enabling/building/linking the module changes no Windows settings.

The C99 API is in `<hx_os_local_dumps.h>` and `<hx_os.h>`; the exception-free C++11 facade is `<hx/LocalDumps.hpp>`. `HX_OS_ENABLE_LOCAL_DUMPS` defaults ON and is independent of REGISTRY, PROCESS and SYSTEM_UTILS. Windows links the system Advapi32 library. Other platforms and module-off builds retain the functions and return `HX_OS_NOT_SUPPORTED`; `hx_os_local_dumps_available()` reports only compiled support.

## 1. Configure the customer application during installation

The usual place for this call is an authorized, elevated installer. Ordinary customers should not need to run the business application as administrator on every launch. Settings persist after the installer/process exits; there is no RAII owner whose destruction removes them.

This complete C program configures **MyApp.exe**, not the installer itself. Replace that basename before running it. Build using the [C project setup](USAGE.md#c-project).

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

	puts("Configured. Verify actual crash collection on the target system.");
	return 0;
}
```

`application=NULL` selects the current **host EXE**, including when the call originates in a DLL. An installer should normally pass the customer's EXE name explicitly. Only a single UTF-8 basename ending in `.exe` is accepted, not a path, wildcard or DLL. The limit is 255 UTF-16 units. Windows matches application names without case sensitivity; different installations with the same basename share the setting in that registry view.

`options=NULL` selects full dumps, count 3, the calling process's registry view, and the literal folder `%LOCALAPPDATA%\CrashDumps`. Windows expands that folder for the crashing account, not the installer account. Defaults are intentionally more diagnostic than Windows' own mini-dump/count-10 defaults. A full dump can be large and contain passwords, documents or other sensitive application data. Count limits file quantity, not total disk bytes.

The function writes only this application's child key under `HKLM\Software\Microsoft\Windows\Windows Error Reporting\LocalDumps`. It does not change global WER values, adjust permissions, elevate the process or create directories. `ACCESS_DENIED` means the call lacked rights; native details remain in `hx_os_error`. Do not call from `DllMain`, a signal handler or an exception handler.

## 2. Choose folder, count, dump type and registry view

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

Initialize the options before changing fields. Version 1 accepts MINI or FULL, positive counts, and DEFAULT/VIEW32/VIEW64. Custom dump flags are not exposed. A custom folder is a strict UTF-8 absolute Windows path or a leading `%VARIABLE%` reference that expands to one. It is stored as `REG_EXPAND_SZ`, without expansion during configuration. Paths are bounded to 32,767 UTF-16 units. Provision a custom directory with write access for the application account; do not solve access failures by making customer dumps publicly readable.

DEFAULT uses the configuring process's registry view. This is not automatic discovery of the target EXE architecture. An installer targeting another architecture must select the intended view. On 64-bit Windows, a deployment supporting x86/x64 collection can explicitly configure both VIEW32 and VIEW64 and verify collection for each target; these are separate operations, not an atomic two-view change. No hardcoded `Wow6432Node` path or process-global view change is used. Reuse the same selected view for query and removal.

## 3. Existing settings and safe removal

The module never silently overwrites an existing application key. It creates three WER values (`DumpFolder`, `DumpCount`, `DumpType`) and a private DWORD `HxLocalDumpsVersion=1` marker. Repeating enable succeeds only when that complete managed key exactly matches the requested values, with no extra values or child keys. Any other existing key returns `EXISTS` unchanged, including an identical unmarked configuration created by someone else.

```c
hx_os_status status = hx_os_local_dumps_remove("MyApp.exe", NULL, &error);
```

This removes a default configuration created by this module, or succeeds if the application key is absent. For custom settings, pass the original options instead of NULL. If the configuration is foreign, incomplete, edited or extended, removal returns `BUSY` and preserves it. The literal folder string must match, not merely expand to the same directory. Removal is not recursive and does not delete existing `.dmp` files or parent/global keys. Removing a per-application override can expose global WER settings, so it does **not** prove all collection is disabled.

Serialize installation/removal and exclude other registry writers for this application, including other processes. The multi-value writes are not a transaction and the target should not be crashing during installation. On a write failure the implementation attempts to delete only its newly created leaf. A cleanup failure is retained as a Win32 `error.cause_code`; inspect the key before retrying. A process interruption can also leave partial settings. There is no hidden force-overwrite or force-delete mode. A native close failure can be reported after settings have changed; query before deciding how to retry.

## 4. Query without changing Windows

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

`fields` contains HAS_TYPE/HAS_COUNT/HAS_FOLDER for values actually present. Missing numeric fields are zero and a missing folder is empty. Folder text is returned literally; REG_SZ/REG_EXPAND_SZ are accepted for inspection. Invalid types, encoding and excessive sizes fail explicitly. Both outputs remain unchanged on failure; a successful query writes only the known info structure and uses the string's own context.

`managed` identifies a complete library-marked layout with no additional values/subkeys. It is not a security credential or proof that the known values have not been edited. Removal additionally checks the expected values. Query is an observation, not a transaction against external edits.

`NOT_FOUND` means no key for this application in this view. It does **not** mean Windows collection is disabled: global WER settings and Windows defaults are not merged into this query. Neither `available()` nor successful configuration/query proves dump delivery, folder permissions or compatible application crash handling.

## 5. C++11 and the safe command-line example

Include `<hx/LocalDumps.hpp>`, then inside a function returning `hx_os_status`:

```cpp
auto enabled = hx::LocalDumps::enable("MyApp.exe");
if (!enabled)
	return enabled.status();

auto settings = hx::LocalDumps::query("MyApp.exe");
if (!settings)
	return settings.status();

return HX_OS_OK;
```

`LocalDumps::options()` returns initialized C options. `enable`, `query` and `remove` reuse the C functions and return `Result`; `query` owns its folder string. Destruction does not remove persistent settings.

The complete public [local_dumps.c](../examples/local_dumps.c) defaults to usage text, with no changes and no deliberate crash. With examples enabled in your existing MSVC build:

```powershell
cmake --build build-shared-msvc --config Release --target hx_os_local_dumps
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe query MyApp.exe 64
```

Only an explicit `enable` or `remove` command writes settings. In an authorized elevated terminal, for a program you manage:

```powershell
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe enable MyApp.exe 64
.\build-shared-msvc\bin\Release\hx_os_local_dumps.exe remove MyApp.exe 64
```

An optional final folder argument applies to enable/remove; both use full dumps/count 3. `-` as the application explicitly selects the example's own EXE. The CLI uses SYSTEM_UTILS for UTF-8 arguments; the underlying LocalDumps API does not require it.

## 6. What to collect from customers

Keep the exact released EXE/DLL/PDB files and matching source on the developer side. The customer normally needs only the runtime files; Windows can save a dump without PDBs installed. After a crash, request the `.dmp`, full application version, time, logs and reproduction steps. Default files are normally under the crashing user's `%LOCALAPPDATA%\CrashDumps`; service accounts have their own profiles. Transfer dumps privately and define retention/deletion policy.

Use Visual Studio or WinDbg with matching PDBs to inspect the crash location. An access violation can be straightforward, but a heap-corruption crash may occur long after the original bad write. A dump is a snapshot, not a complete history. Complement it with memory-checking builds and reproduction tests.

A hang normally needs a separate live/manual dump, for example Task Manager's process-details dump command before terminating the application. Silent corruption, leaks, normal exit, forced termination and power failure are not automatically diagnosed by LocalDumps. Applications with their own crash reporting and automatic debugger launch can prevent this collection mechanism. LocalDumps operates independently of ordinary WER report submission; it does not enable uploading reports to Microsoft.

## 7. Verification boundaries

Test configuration and collection separately. Maintainer regressions redirect HKLM only inside an isolated test process to a uniquely owned temporary HKCU tree. They exercise real Windows registry calls, Unicode, conflicts, cleanup, permissions, views and unchanged outputs without configuring any actual application. Static tests additionally inject write/rollback failures. Non-Windows/module-off tests verify `NOT_SUPPORTED`. These tests do not prove that a customer machine produces a `.dmp`.

Before release, separately verify real crash collection on the target Windows versions, application architectures/accounts and folders, using a disposable test program and explicitly authorized machine settings. Keep crash-test files private and remove only settings created for that test. Do not induce a crash in a production application with unsaved work.

References: [Microsoft LocalDumps configuration](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps), [registry redirection and views](https://learn.microsoft.com/en-us/windows/win32/winprog64/accessing-an-alternate-registry-view).
