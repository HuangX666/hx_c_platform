# Files and Directories in Archives

[简体中文](ARCHIVE.zh-CN.md) · [README](../README.md) · [Dependency Builds](DEPENDENCIES.md)

When your application needs to bundle log folders, configuration snapshots, or exported reports into `.zip`, `.tar.gz`, `.tar.zst`, or `.7z` archives—or safely extract incoming packages without risking path traversal (`..` / Zip Slip), symlink escapes, or decompression bombs—`hx_os_archive` provides a unified C99 and exception-free C++11 interface.

To try a complete round-trip first, create a directory named `records` containing a file `device.json` with `{"device":42}`, build `hx_os_archive_files` as shown in Section 2, and run `pack`, `list`, and `unpack`: `list` prints `device.json`, and the extracted file matches the original JSON. Start with ordinary unencrypted ZIP first, then explore password encryption and the optional native 7-Zip backend in Sections 3 and 4.

## 1. Choose the operation

Include `<hx_os.h>` or the standalone `<hx_os_archive.h>` header. Operations require no global initialization, manual codec setup, or chunk loops:

| Task | C Entry Point | C++11 Entry Point |
| --- | --- | --- |
| Pack a single file or directory into an archive | `hx_os_archive_pack` | `hx::Archive::pack` |
| Extract an archive into a destination directory | `hx_os_archive_unpack` | `hx::Archive::unpack` |
| Extract a single named regular file from an archive | `hx_os_archive_extract_file` | `hx::Archive::extractFile` |
| Inspect archive entries without extracting to disk | `hx_os_archive_visit` | `hx::Archive::forEach` |

**Relative Path Rules**: Packing a directory `records/` containing `device.json` and `images/photo.png` stores those relative paths directly at the archive root without adding an extra outer `records/` prefix; packing a single file stores its basename. Empty directories are preserved, whereas symlinks, hard links, and special device files in the source tree are rejected explicitly.

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_os_status status =
		hx_os_archive_pack("records", "records.zip", NULL);

	if (status != HX_OS_OK)
		fprintf(stderr, "Pack failed: %s\n", hx_os_status_name(status));

	return status == HX_OS_OK ? 0 : 1;
}
```

By default, existing destination files are preserved (returning `HX_OS_EXISTS` rather than overwriting).

## 2. Build and use the complete program

`HX_OS_ENABLE_ARCHIVE` is `ON` by default and requires `FILE_UTILS`, `CONTAINERS`, and the prebuilt `libarchive`, `OpenSSL`, `zlib`, `liblzma`, and `zstd` dependency stack. Setting `-DHX_OS_ENABLE_ARCHIVE=OFF` keeps all symbols linkable while `hx_os_archive_available()` returns `0` and archive calls return `HX_OS_NOT_SUPPORTED` (see [Native Dependency Targets](NATIVE_DEPENDENCIES.md) when trimming modules).

Build the dependency stack per the [Dependency Guide](DEPENDENCIES.md), then configure the library with `HX_OS_DEPENDENCY_PREFIX` and build `hx_os_archive_files`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHX_OS_DEPENDENCY_PREFIX=/absolute/project/build-deps/prefix \
  -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_archive_files
```

The runnable C program is [archive_files.c](../examples/archive_files.c). After creating a `records/` folder containing `device.json`, run these four commands to pack, list, unpack, and extract a single entry:

```sh
./build/bin/hx_os_archive_files pack records records.zip
./build/bin/hx_os_archive_files list records.zip
./build/bin/hx_os_archive_files unpack records.zip restored
./build/bin/hx_os_archive_files extract records.zip device.json selected.json
```

(On Windows Visual Studio Release builds, use `build\bin\Release\hx_os_archive_files.exe`.) All paths and passwords use UTF-8. Because the example refuses to overwrite existing files by default, remove `records.zip`, `restored`, and `selected.json` (or pick new output paths) before re-running.

Installed C projects link `hx_c_platform::hx_c_platform` via `find_package(hx_c_platform CONFIG REQUIRED)` (C++ projects link `hx_c_platform::cpp`), and public headers never expose third-party archive headers.

## 3. Formats and passwords

| Filename Extension / Explicit Format | Single File | Directory | Password Support |
| --- | --- | --- | --- |
| `.zip` / `HX_OS_ARCHIVE_ZIP` | Yes | Yes | AES-256 ZIP |
| `.tar` / `HX_OS_ARCHIVE_TAR` | Yes | Yes | No |
| `.tar.gz`, `.tgz` / `HX_OS_ARCHIVE_TAR_GZIP` | Yes | Yes | No |
| `.tar.xz`, `.txz` / `HX_OS_ARCHIVE_TAR_XZ` | Yes | Yes | No |
| `.tar.zst`, `.tar.zstd` / `HX_OS_ARCHIVE_TAR_ZSTD` | Yes | Yes | No |
| `.7z` / `HX_OS_ARCHIVE_7ZIP` | Yes | Yes | Via optional native 7-Zip backend (Section 4) |
| `.gz` / `HX_OS_ARCHIVE_GZIP` | Single file only | No | No |
| `.xz` / `HX_OS_ARCHIVE_XZ` | Single file only | No | No |
| `.zst`, `.zstd` / `HX_OS_ARCHIVE_ZSTD` | Single file only | No | No |

- **Format Detection & Single-File Streams**:
  - When packing with `HX_OS_ARCHIVE_AUTO`, the output extension selects the format; when reading, the library inspects container headers (even if an encrypted `.7z` was renamed to `.blob`).
  - For raw single-file compression streams (`.gz`, `.xz`, `.zst`), the extracted output filename is derived by stripping the final extension from the archive basename (or `data` if empty)—**embedded Gzip header filenames are never trusted as output paths**.
- **In-Process Codec Stack & Presets**:
  - The default backend links `libarchive`, `zlib`, `liblzma`, `Zstd`, and `OpenSSL` directly in-process (never spawning external `tar`, `gzip`, or `7z` CLI tools). Outer Gzip/XZ/Zstd streams run through native streaming decoders in our adapter so trailing checksums are verified even after an inner TAR stream reaches its logical end-of-archive marker.
  - `options.preset` selects `FAST`, `BALANCED` (default), or `SMALL`.

### Password-Protected AES-256 ZIP Archives

Pass a password through `hx_os_archive_options` to `hx_os_archive_pack_ex()`:

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
hx_string diagnostic = HX_STRING_INIT;

options.password = password_from_your_application;
options.diagnostic = &diagnostic;

hx_os_status status = hx_os_archive_pack_ex(NULL,
	"records", "private.zip", &options, NULL, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "%s\n", hx_string_c_str(&diagnostic));

hx_string_destroy(&diagnostic);
```

Pass the same `options` to `hx_os_archive_unpack_ex()` or `hx_os_archive_extract_file_ex()` to decrypt.

> [!NOTE]
> **ZIP Password Interoperability and Metadata Visibility**
> - **ASCII Passwords for Cross-Tool ZIP Exchange**: ZIP tools disagree on character encoding for non-ASCII passwords—this library uses UTF-8 bytes, whereas Windows 7-Zip converts through the legacy system code page (`CP_ACP`). **Use ASCII passwords when exchanging encrypted `.zip` files with external Windows 7-Zip tools** (the native `.7z` backend in Section 4 uses UTF-16LE passwords and is unaffected by code-page differences).
> - **Unencrypted ZIP Filenames**: Standard AES-256 ZIP encrypts file contents but leaves entry filenames visible in the central directory. To encrypt filenames as well, either use `.7z` with `HX_OS_ARCHIVE_ENCRYPT_NAMES` (Section 4) or encrypt the finished archive file with [`hx_os_crypto_encrypt_file()`](CRYPTO.md#3-encrypt-files-or-arbitrary-bytes).
> - **Diagnostics**: Not every backend maps wrong passwords specifically to `HX_OS_AUTH_FAILED`; some report `HX_OS_IO_ERROR` with detailed native codes and UTF-8 text in `options.diagnostic`.

## 4. Password and filename encryption in 7z

While the default `libarchive` backend reads and writes unencrypted `.7z` archives out of the box, **password-protected `.7z` archives and encrypted `.7z` header filenames (`HX_OS_ARCHIVE_ENCRYPT_NAMES`)** use a separate, opt-in **native 7-Zip backend**:
1. Configure the main library with `-DHX_OS_ENABLE_7ZIP=ON` (keeping `LIBRARY` and `PATHS` enabled). This builds a small exception-free C++11 adapter shared library that bridges the C99 core to native 7-Zip via a private C ABI.
2. Build the pinned `deps/7zip` (26.04) runtime library using the helper CMake project (run from a Visual Studio Developer Prompt on Windows, and ensure the build directory path **contains no spaces** due to upstream makefile rules):

```sh
cmake -S tools/sevenzip -B build-7zip
cmake --build build-7zip --config Release
```

This produces `build-7zip/runtime/7z.dll` on Windows and `build-7zip/runtime/7z.so` on Linux (see [Cross-Building Linux ARM and Android](DEPENDENCIES.md#6-cross-build-linux-arm-and-android) for embedded/mobile targets).

Pass the **absolute path** to that trusted native runtime in `options.seven_zip_library`:

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;

options.password = password_from_your_application;
options.seven_zip_library = absolute_trusted_7zip_runtime_path;
options.flags = HX_OS_ARCHIVE_ENCRYPT_NAMES;

hx_os_status status = hx_os_archive_pack_ex(NULL,
	"records", "private.7z", &options, NULL, NULL);
```

- **Explicit Path Loading**: Neither `seven_zip_library` nor `seven_zip_adapter` is ever searched via `PATH`. The adapter verifies the pinned 26.04 version and COM interface layout before creating objects, returning `HX_OS_NOT_SUPPORTED` for incompatible `p7zip` or older binaries.
- **Licensing**: Because the optional native 7-Zip runtime carries separate LGPL/BSD/unRAR terms, it is not copied into the default core installation automatically. Preserve its upstream license notices per [Third-Party Notices](../THIRD_PARTY_NOTICES.md) when distributing it.

## 5. Limits, cancellation and partial results

Every archive operation enforces bounded resource caps by default to guard against decompression bombs and runaway directory trees:

| Option Field | Default Cap & Meaning |
| --- | --- |
| `max_archive_bytes` | `4 GiB`: maximum input archive size or generated output archive size |
| `max_content_bytes` | `4 GiB`: maximum cumulative unpacked/packed payload bytes (also rejects individual entries declaring a larger size) |
| `max_decoded_bytes` | `8 GiB`: maximum decoded bytes from outer Gzip/XZ/Zstd streams (including TAR headers, padding, and skipped entries) |
| `max_entries` | `65,536`: maximum archive entries inspected per call |
| `max_depth` | `64`: maximum path component nesting depth |
| `max_path_bytes` | `65,536`: maximum UTF-8 path length in bytes (including null terminator) |
| `max_memory_bytes` | `16 MiB`: maximum first-party working buffer and metadata allocation budget |
| `workers` | `1` codec worker thread by default (configurable from `1` to `32`) |
| `deadline` | `INFINITE` by default; pass `hx_os_deadline_after_ms(...)` for a finite monotonic deadline |

Pass an initialized `hx_os_archive_result` struct to `_ex` functions to inspect progress counters (even if a call fails or is cancelled via a progress callback returning `HX_OS_CANCELLED`, `progress.published` reports how many files were already published to disk):

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;
hx_os_archive_result progress;

hx_os_archive_result_init(&progress, sizeof(progress));
options.deadline = hx_os_deadline_after_ms(5000);
options.max_content_bytes = 64 * 1024 * 1024;

hx_os_status status = hx_os_archive_unpack_ex(NULL,
	"records.zip", "restored", &options, &progress, NULL);

printf("Files already published: %u\n", progress.published);
```

## 6. Filesystem and failure behavior

- **Atomic Archive Packing vs. Per-File Extraction**:
  - **Packing (`pack`)** stages the archive in a sibling temporary file, syncs it, and publishes it atomically to the target path (packing an archive inside its own source directory is rejected, and existing targets return `HX_OS_EXISTS` unless `HX_OS_ARCHIVE_OVERWRITE` is set).
  - **Extraction (`unpack`)** stages each regular file in a temporary file under its target parent directory and publishes it once that entry passes integrity checks. **Directory extraction is not an all-or-nothing rollback transaction**: if a later entry fails, earlier published files and created subdirectories remain on disk (reflected in `progress.published`).
- **Path Sanitization & Zip-Slip Protection**:
  - Entries with absolute paths, parent traversal (`..`), drive letters or NTFS alternate data streams (`:`), reserved Windows device names, or invalid UTF-8 are rejected immediately (leading `./` prefixes are normalized).
  - Symbolic links, hard links, reparse points, and special files are **never restored** during extraction, and existing symlinks encountered beneath the destination directory are rejected to prevent symlink-following writes outside the target tree.
- **Private Staging Permissions & Thread-Local POSIX UTF-8 Locale**:
  - Extracting encrypted archives stages plaintext in owner-only temporary files (`0600` verified on POSIX; protected owner-only DACL on Windows), failing before writing plaintext if the target mount cannot enforce access restrictions.
  - On POSIX, `libarchive` header conversion temporarily applies a per-thread UTF-8 locale (`C.UTF-8` or `en_US.UTF-8` via `uselocale`), restoring the caller's thread locale during progress/visitor callbacks and custom allocator calls without ever mutating the process-global locale.

## 7. C++11 direct calls

Include `<hx/Archive.hpp>` to call the exception-free C++11 wrapper, which returns `hx::Result<hx_os_archive_result>`:

```cpp
#include <hx/Archive.hpp>
#include <cstdio>

int main()
{
	auto packed = hx::Archive::pack("records", "records.zip");

	if (!packed) {
		std::fprintf(stderr, "Pack failed: %s\n",
			hx_os_status_name(packed.status()));
		return 1;
	}

	auto listed = hx::Archive::forEach("records.zip",
		[](const hx_os_archive_entry &entry) noexcept {
			std::printf("%s\n", entry.path.data);
		});

	return listed ? 0 : 1;
}
```

`hx::Archive::forEach` accepts a `noexcept` callable returning `void`, `hx_os_status`, or `hx::Result<void>`, and partial progress counters remain accessible on `Result` values even when an operation stops early.

## 8. Run the archive example

```sh
build/bin/hx_os_archive_files pack input-folder records.zip
build/bin/hx_os_archive_files list records.zip
build/bin/hx_os_archive_files unpack records.zip restored-folder
```

Prepare `input-folder` beforehand and ensure `records.zip` and `restored-folder` do not already exist (on Windows, run `build/bin/Release/hx_os_archive_files.exe`). See [Platform Support](PLATFORMS.md) for target deployment requirements.
