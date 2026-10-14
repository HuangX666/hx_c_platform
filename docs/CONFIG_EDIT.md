# Edit INI, save a backup and apply settings

[简体中文](CONFIG_EDIT.zh-CN.md) · [README](../README.md)

## 1. Edit an INI section and key

`hx_os_ini_create()` creates an empty document; `hx_os_ini_load()` loads an existing UTF-8 file. Set strings with `hx_os_ini_set(&document, "network", "port", "8080", &error)`. The library creates missing sections/keys. It replaces the owner only after a complete new document is ready. If you retained the previous document, its nodes remain valid and unchanged.

`hx_os_ini_get(document, section, key, &text, &error)` returns a copied `hx_string`. `hx_os_ini_remove_key()` removes one key; `hx_os_ini_remove_section()` removes its whole section. NULL or an empty section selects global keys, while section removal requires a nonempty name. Names are literal and case-sensitive: slashes, dots and tildes do not introduce paths. A section name cannot replace a scalar global key, nor vice versa.

INI values remain strings. There is no array syntax, implicit numeric conversion, comment preservation or whitespace-preserving editing. The existing INI profile rejects unrepresentable text, such as embedded newlines or leading/trailing whitespace that would disappear on reading. Failure preserves the owner. The document's allocator and limits apply; edited trees remain immutable and can be shared by retaining them. Serialize or save separately to write the file.

## 2. Run a complete example

[config_edit.c](../examples/config_edit.c) creates the following file:

```ini
[network]
host=localhost
port=8080
```

From the repository root:

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_config_edit
# Linux/macOS:
./build/bin/hx_os_config_edit
# Windows PowerShell:
.\build\bin\Release\hx_os_config_edit.exe
```

It prints `Saved preferences.ini` and writes to the working directory. Run it once to create the file, change the port in the file to `9000`, then run it again. The new primary contains `8080`, while `preferences.ini.bak` contains your previous `9000` version. Run the same executable with `--restore` to explicitly restore that previous version. Do this demonstration in a disposable directory, not on your application's production configuration.

In C++11, `<hx/Config.hpp>` provides the same operations through `ConfigDocument::emptyIni()`, `setIni()`, `ini()`, `removeIniKey()` and `removeIniSection()`. For example, this complete function creates one port setting:

```cpp
hx::Result<hx::ConfigDocument> network_preferences() noexcept
{
	auto document = hx::ConfigDocument::emptyIni();
	if (!document)
		return hx::Failure(document.error());

	auto changed = document.value().setIni("network", "port", "8080");
	if (!changed)
		return hx::Failure(changed.error());

	return document;
}
```

Check the result, then use `saveBackup("preferences.ini", HX_OS_CONFIG_INI)` or ordinary `save()`. `ConfigDocument` copies retain their previous immutable tree; editing one facade does not edit another copy. No exceptions are involved.

## 3. What backup saving guarantees

`hx_os_config_save_backup()` works with each supported serialized format, not just INI. It first serializes the new document, then reads and validates the existing primary within the selected limits. A valid primary is copied byte-for-byte into `path + ".bak"` using atomic publication; the new primary is published afterward. The two paths must be exclusively managed during this operation.

| Situation | Result |
| --- | --- |
| Primary does not exist | Create the primary; leave any previous backup unchanged |
| Primary is valid | Publish its exact bytes to `.bak`, then publish the new primary |
| Primary is corrupt or exceeds limits | Fail without replacing either file; keep an existing usable backup |
| Backup publication fails | Keep the primary unchanged |
| Primary publication fails after backup succeeds | Preserve the published backup and report failure; no automatic rollback |

The optional `hx_os_config_backup_result` separately reports `backup` and `primary` publication and parent-directory synchronization. Success means publication. It does not turn two file updates into a power-loss-safe transaction. The lower-level atomic file operation syncs the file and then attempts directory sync; Windows currently reports directory sync as unsupported. Temporary cleanup failure can leave a recoverable temporary file, as with existing atomic file saving.

`hx_os_config_restore_backup()` explicitly reads `.bak`, parses it, then publishes its exact bytes to the primary. Invalid, oversized or missing backups do not replace the primary. The backup remains available. Ordinary `hx_os_config_load()` never restores automatically. In C++, call `ConfigDocument::restoreBackup(path, format)`; the result contains the parsed recovered document. An optional output records publication/directory-sync details.

## 4. Apply one set of settings to another

For existing mutable settings, use `hx_settings_apply(current, incoming, &applied, &error)` or `current.apply(incoming)` in C++. The source's explicit values are snapshotted first; its defaults are not copied. Unmentioned destination keys and destination defaults stay in place.

For example, if current settings contain `{port: 8080, title: "app"}` and incoming settings contain `{port: 9000, retries: 3}`, applying them produces `{port: 9000, title: "app", retries: 3}`. A nested object/array replaces the corresponding whole value; members are not recursively merged. User pointers remain borrowed; other values are copied into the destination's context.

Each key is committed and notifies observers separately, in byte-sorted key order. The first failure stops the operation while preserving earlier successes. The C output or C++ result's carried value reports how many keys succeeded, including on failure. This matches the existing `Settings::update()` behavior. Reentrant observers and concurrent writers follow the usual setter rules; this is not a transaction. Applying a settings object to itself is valid. `hx_settings_apply_value()` / `Settings::apply(SettingValue)` accepts a retained object snapshot when you already have one; scalar and array roots return TYPE_MISMATCH.

## 5. Limits and costs

INI edits build one replacement tree and validate the INI representation with the existing serializer's counting pass; they do not allocate an extra serialized text buffer. Old retained trees can coexist with the new tree. Repeated single-key edits therefore copy the tree repeatedly; use ordinary configuration construction or settings snapshots for large batch workloads.

Backup/recovery reads are bounded by configuration options. Old bytes, parsed trees and serialized new text can coexist; limits apply to each documented buffer/tree rather than a process-wide quota. These operations are synchronous and intended for configuration persistence, not a logging hot path. Settings apply copies one value and performs the existing root update per key, adding no background worker. See [platform support](PLATFORMS.md) for target boundaries.
