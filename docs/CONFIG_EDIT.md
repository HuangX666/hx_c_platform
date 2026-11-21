# Edit INI, save a backup and apply settings

[简体中文](CONFIG_EDIT.zh-CN.md) · [README](../README.md)

When your application needs to update a few `[section]` and `key` entries in an INI file, preserve a verified `.bak` backup before overwriting a configuration file on disk, or merge an incoming set of runtime preferences into an active settings container, use the editing and backup APIs described here.

Section 2 walks through a complete runnable example that sets `network.port` to `"8080"` and saves `preferences.ini` in the working directory. If you run the program once, manually edit the port in `preferences.ini` to `9000`, and run it a second time, the primary file is updated back to `8080` while your valid `9000` file is preserved in `preferences.ini.bak`; running the binary with `--restore` then restores `9000` to the primary file. Note that this is an **explicit save-and-restore workflow**, not an automatic background rollback service. Reuse the build setup described in the [example guide](../examples/README.md#run).

## 1. Edit an INI section and key

- **Create or load**: Call `hx_os_ini_create()` to start with an empty INI document, or `hx_os_ini_load()` to load an existing UTF-8 INI file.
- **Set a key**: Call `hx_os_ini_set(&document, "network", "port", "8080", &error)` to insert or update a string key/value pair (missing sections and keys are created automatically). Under the hood, editing constructs a replacement immutable tree and updates the caller's `document` pointer only after the new tree is completely built and validated. If you previously retained a reference to the old document, that old tree and its borrowed nodes remain valid and unchanged.
- **Read or remove**:
  - `hx_os_ini_get(document, section, key, &text, &error)` copies the string value into `text` (`hx_string`).
  - `hx_os_ini_remove_key()` removes a single key, and `hx_os_ini_remove_section()` removes an entire section.
  - Passing `NULL` or `""` for `section` targets root-level global keys, whereas `hx_os_ini_remove_section()` requires a non-empty section name.
  - Section and key names are matched as exact, case-sensitive literals—slashes `/`, dots `.`, and tildes `~` are ordinary characters, not path separators. A section cannot overwrite a global scalar key of the same name, nor vice versa.

> [!NOTE]
> **INI Representation Rules and Persistence:**
> All INI values are stored as strings without array syntax, implicit number conversion, or comment/whitespace preservation. Values that cannot round-trip through the INI grammar—such as strings containing embedded newlines or leading/trailing whitespace that would be trimmed on reload—are rejected immediately during `hx_os_ini_set()`, leaving the existing document untouched. In-memory edits do not touch the disk until you call a serialize or save function.

## 2. Run a complete example

The complete [config_edit.c](../examples/config_edit.c) example creates and updates the following INI file:

```ini
[network]
host=localhost
port=8080
```

From the repository root, configure and build the example target:

```sh
cmake -S . -B build -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_config_edit
# Linux/macOS:
./build/bin/hx_os_config_edit
# Windows PowerShell:
.\build\bin\Release\hx_os_config_edit.exe
```

Running the binary prints `Saved preferences.ini` and writes `preferences.ini` in the current working directory. Try the following sequence in a scratch directory (not on production config files):
1. Run the program once to create `preferences.ini` with `port=8080`.
2. Edit `preferences.ini` by hand and change `port` to `9000`.
3. Run the program a second time: `preferences.ini` is updated to `8080`, while `preferences.ini.bak` preserves your previous `9000` version.
4. Run the binary with `--restore` to explicitly restore the `9000` backup back to `preferences.ini`.

In C++11, include `<hx/Config.hpp>` to access `hx::ConfigDocument::emptyIni()`, `setIni()`, `ini()`, `removeIniKey()`, and `removeIniSection()`:

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

After checking that the result succeeded, call `saveBackup("preferences.ini", HX_OS_CONFIG_INI)` or `save()`. Copying a `ConfigDocument` shares the underlying immutable tree; calling `setIni()` on one instance points it at the newly created replacement tree without mutating other copies. No C++ exceptions are used.

## 3. What backup saving guarantees

`hx_os_config_save_backup()` works across all supported output formats (JSON, YAML, XML, INI, TOML, ENV, and properties), not just INI. It executes in three steps:
1. Serializes the new document in memory.
2. Reads and parses the existing primary file on disk (if present) within the configured limits to verify that it is valid.
3. Atomically publishes the existing primary file's **exact bytes** to `path + ".bak"`, and then atomically publishes the newly serialized bytes to `path` (callers must serialize access to both paths during the call).

| Scenario on disk | `hx_os_config_save_backup()` behavior |
| --- | --- |
| Primary file does not exist | Creates the primary file; leaves any existing `.bak` file untouched |
| Existing primary file is valid | Atomically publishes its exact bytes to `.bak`, then publishes the new primary file |
| Existing primary file is corrupt or exceeds limits | Fails without modifying either file (protecting a previously good `.bak` from being overwritten by a corrupt primary) |
| Publishing `.bak` fails | Fails immediately and leaves the primary file unchanged |
| Publishing `.bak` succeeds, but publishing the new primary fails | Preserves the newly published `.bak` and reports the error (no automatic rollback) |

- **Publication status & durability boundaries**: The optional `hx_os_config_backup_result` output struct reports `backup` publication, `primary` publication, and parent-directory sync status separately. Atomic file publication flushes the file and then attempts a parent-directory sync (Windows currently reports directory sync as unsupported). Note that updating two paths sequentially is not a power-loss-atomic multi-file transaction.
- **Explicit restoration (`hx_os_config_restore_backup`)**: Reads `path + ".bak"`, validates that it parses cleanly, and atomically publishes its exact bytes over the primary path. If `.bak` is missing, oversized, or malformed, the primary file and `.bak` remain untouched. Ordinary `hx_os_config_load()` never restores from `.bak` automatically. In C++11, call `hx::ConfigDocument::restoreBackup(path, format)` to restore the file and receive the parsed `ConfigDocument` in one step.

## 4. Apply one set of settings to another

To merge entries from one mutable runtime settings container (`hx_settings`) into another, call `hx_settings_apply(current, incoming, &applied, &error)` in C or `current.apply(incoming)` in C++11:

- **Explicit values only**: The function first snapshots `incoming`'s **explicitly set values** (ignoring `incoming`'s registered defaults). Keys not present in `incoming`—along with `current`'s own defaults—remain untouched. For example, applying `{port: 9000, retries: 3}` onto `{port: 8080, title: "app"}` produces `{port: 9000, title: "app", retries: 3}`.
- **Whole-value replacement for nested containers**: Nested objects and arrays replace the target key's value as a whole rather than recursively merging inner members. User pointers remain borrowed, while all other values are deep-copied into `current`'s memory context.
- **Commit order & observer notifications**: Keys are committed one by one in byte-sorted key order, notifying observers after each key. The first failure stops the loop while keeping earlier keys committed, and reports the number of applied keys via `applied` (or the C++ `Result` value), matching `Settings::update()`. If you already hold a retained object value snapshot, call `hx_settings_apply_value()` / `Settings::apply(SettingValue)` (passing a scalar or array root returns `HX_OS_TYPE_MISMATCH`).

## 5. Limits and costs

- **Cost of single-key edits**: Each `hx_os_ini_set()` or removal call constructs a replacement immutable tree and runs the serializer's counting pass to validate INI representability without allocating a temporary serialized buffer. Because repeated single-key calls copy the tree each time, use `hx_os_config_create()` or settings snapshots when building large documents in bulk.
- **Memory & I/O bounds**: Backup and restore file reads obey the limits in `hx_os_config_options`. During `save_backup`, the old file bytes, validation tree, and new serialized text may coexist briefly in memory, each bounded by its respective option limit. All file operations are synchronous and designed for configuration persistence rather than high-frequency logging paths. See [platform support](PLATFORMS.md) for target filesystem boundaries.
