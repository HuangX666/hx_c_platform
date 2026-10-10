# Files and directories in archives

[简体中文](ARCHIVE.zh-CN.md) · [README](../README.md) · [Dependency builds](DEPENDENCIES.md)

## 1. Choose the operation

Include `<hx_os.h>` or the smaller `<hx_os_archive.h>`. Normal calls need no global initialization, archive handle, manual read loop or explicit codec setup.

| Task | C entry point | C++11 entry point |
| --- | --- | --- |
| Pack one file or a directory | `hx_os_archive_pack` | `hx::Archive::pack` |
| Extract to a directory | `hx_os_archive_unpack` | `hx::Archive::unpack` |
| Extract one named regular file | `hx_os_archive_extract_file` | `hx::Archive::extractFile` |
| Visit entries without extracting | `hx_os_archive_visit` | `hx::Archive::forEach` |

For example, packing `records/` containing `device.json` and `images/photo.png` produces those relative names in the archive. It does not add another `records/` directory. Packing one file stores its basename. Empty directories are preserved; links and special files are rejected when packing.

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

Existing destinations are preserved by default. A failed operation reports a status; it never silently skips an unsupported codec, unsafe name or existing file.

## 2. Build and use the complete program

ARCHIVE is enabled by default and requires FILE_UTILS, CONTAINERS and the prepared libarchive/OpenSSL/compression SDK. Disable it with `HX_OS_ENABLE_ARCHIVE=OFF`; functions remain linkable and return `HX_OS_NOT_SUPPORTED`, and `hx_os_archive_available()` returns 0. Disable both ARCHIVE and CRYPTO to omit OpenSSL entirely.

Build the SDK as described in [DEPENDENCIES](DEPENDENCIES.md), then configure the library with its absolute prefix:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHX_OS_DEPENDENCY_PREFIX=/absolute/project/build-deps/prefix -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel 4
```

The runnable C program is [archive_files.c](../examples/archive_files.c). It uses these same public calls, prints copied diagnostics on failure and reports how many files were published. The example converts Windows command-line arguments to UTF-8 using the existing argument helper.

```sh
./build/bin/hx_os_archive_files pack records records.zip
./build/bin/hx_os_archive_files list records.zip
./build/bin/hx_os_archive_files unpack records.zip restored
./build/bin/hx_os_archive_files extract records.zip device.json selected.json
```

On a Visual Studio Release build, use `build\bin\Release\hx_os_archive_files.exe`. The `records` input directory must already contain your files. `restored` is created when needed. All paths and passwords use UTF-8. The program is deliberately conservative: it does not overwrite existing files.

The same core supports installed pure C consumers through `find_package(hx_c_platform CONFIG REQUIRED)` and `hx_c_platform::hx_c_platform`. C++ consumers link `hx_c_platform::cpp`. No archive headers from third parties appear in the public API.

## 3. Formats and passwords

| Output name / explicit format | File | Directory | Password |
| --- | --- | --- | --- |
| `.zip` / `HX_OS_ARCHIVE_ZIP` | Yes | Yes | AES-256 ZIP |
| `.tar` / `HX_OS_ARCHIVE_TAR` | Yes | Yes | No |
| `.tar.gz`, `.tgz` / `HX_OS_ARCHIVE_TAR_GZIP` | Yes | Yes | No |
| `.tar.xz`, `.txz` / `HX_OS_ARCHIVE_TAR_XZ` | Yes | Yes | No |
| `.tar.zst`, `.tar.zstd` / `HX_OS_ARCHIVE_TAR_ZSTD` | Yes | Yes | No |
| `.7z` / `HX_OS_ARCHIVE_7ZIP` | Yes | Yes | Optional native 7-Zip backend |
| `.gz` / `HX_OS_ARCHIVE_GZIP` | Yes | No | No |
| `.xz` / `HX_OS_ARCHIVE_XZ` | Yes | No | No |
| `.zst`, `.zstd` / `HX_OS_ARCHIVE_ZSTD` | Yes | No | No |

`AUTO` infers the packing format from the extension. Reading identifies container content, including an encrypted 7z renamed to `.blob`. A raw compression extension selects a single decoded file; an explicit format removes ambiguity. For raw streams, the extracted name is the archive basename with its final extension removed, or `data` if no useful basename remains. Stored Gzip filenames are not used as destination paths. Empty files and concatenated Gzip members are supported.

The default backend is libarchive with zlib, liblzma, Zstd and OpenSSL. Gzip/XZ/Zstd outer streams use native streaming decoders in our adapter so integrity checks finish even after a TAR reader reaches its logical end. There is no external `tar`, `gzip` or `7z` command fallback. ZIP uses Deflate; ordinary 7z uses libarchive's selected codec. `preset` selects FAST, BALANCED (default) or SMALL; it is not an identical numeric compression level across formats.

Password ZIP requires only options:

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

For `HX_OS_ERROR_ARCHIVE`, `native_code` retains the native code. A negative `cause_code` interpreted as `int64_t` is libarchive’s return value (with its errno in `native_code`); GZIP/XZ/ZSTD format constants identify outer-decoder errors; zero identifies native 7-Zip HRESULT/operation codes. The optional diagnostic provides copied readable detail. Native text is limited to a valid UTF-8 prefix; if none remains, the fallback is `Archive operation failed: <status>`, using the final status. Copying a diagnostic is best-effort if its allocator fails; the returned status and native error remain authoritative.

Use the same options with `unpack_ex` or `extract_file_ex` to decrypt. Wrong passwords fail; the decoder's copied diagnostic distinguishes details when available. Do not assume every backend maps every password failure to `AUTH_FAILED`: some native failures are `IO_ERROR` with archive details. ZIP AES does not hide filenames. ZIP password bytes have incompatible conventions across tools: this API uses UTF-8, while Windows 7-Zip converts through CP_ACP. Use an ASCII password for that ZIP interoperability; a non-ASCII ZIP password is not guaranteed to work in that external program. This does not affect the native 7z UTF-16 password interface. CRCs detect corruption; they do not authenticate a sender. Encrypted 7z likewise does not supply the application-level authenticated-envelope contract of the separate [crypto module](CRYPTO.md).

The example reads `HX_ARCHIVE_PASSWORD` from its environment instead of accepting it as a command-line argument. A real application can pass its own in-memory password. An environment variable is a demonstration input method, not a secret store.

## 4. Password and filename encryption in 7z

This is a separate optional backend. It does not change the default backend or link the native 7-Zip implementation into the core. Enable `HX_OS_ENABLE_7ZIP=ON`; LIBRARY and PATHS must also be available. This builds a small first-party C++11 shared adapter with exceptions disabled. The core remains C99 and communicates with it through a private C ABI. Native 7-Zip can use exceptions internally as allowed for third-party code; the used native COM interfaces contain native errors before returning.

The pinned 7-Zip source is the independent `deps/7zip` submodule (26.04). A host runtime can be built without editing or generating files in that source tree:

```sh
cmake -S tools/sevenzip -B build-7zip
cmake --build build-7zip --config Release
```

Windows requires a Visual Studio developer shell. The result is `build-7zip/runtime/7z.dll` on Windows and `build-7zip/runtime/7z.so` on Linux. The helper's build path must not contain spaces because of upstream makefile rules. The helper now accepts Linux ARM and Android cross builds from a POSIX host/WSL with the target toolchain. For Android it selects the NDK target/API, keeps libc++ inside this optional runtime and uses Bionic's libc thread symbols. See [cross-build commands](DEPENDENCIES.md#6-cross-build-linux-arm-and-android). Apple build/runtime validation is deferred. FetchContent uses the same pin if explicitly selected.

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;

options.password = password_from_your_application;
options.seven_zip_library = absolute_trusted_7zip_runtime_path;
options.flags = HX_OS_ARCHIVE_ENCRYPT_NAMES;

hx_os_status status = hx_os_archive_pack_ex(NULL,
	"records", "private.7z", &options, NULL, NULL);
```

Omit ENCRYPT_NAMES when only content encryption is wanted. `seven_zip_library` must be an absolute trusted DLL/so path. `seven_zip_adapter` can explicitly name the first-party adapter; otherwise it is loaded beside the core library, or beside the executable for a static core. There is no PATH lookup for either top-level module. Normal OS rules still govern their dependent runtime libraries. The adapter is installed in `bin` on Windows and `lib` on POSIX; a statically linked POSIX application should provide its path or deploy it beside the executable.

`hx_os_archive_7zip_available()` reports whether the bridge was compiled, not whether a native DLL is installed. Each synchronous operation releases its streams and native COM objects before unloading its module references. Exclude concurrent removal or replacement of runtime files. The adapter checks the native module version and interface layout before creating COM objects; a mismatch with pinned 26.04 returns NOT_SUPPORTED. p7zip and pre-23 Unix interfaces are not interchangeable. Private unloads are ordered with a short mutex, without holding it across codec work, application callbacks, injected allocators or diagnostic allocation.

The optional native backend has LGPL/BSD/unRAR terms separate from the default permissive stack. Its runtime is not automatically copied into the core installation. Retain the exact upstream notices and satisfy the obligations for the components you distribute; dynamic loading does not erase those terms. See [third-party notices](../THIRD_PARTY_NOTICES.md).

## 5. Limits, cancellation and partial results

| Default option | Value / meaning |
| --- | --- |
| `max_archive_bytes` | 4 GiB input file or output archive |
| `max_content_bytes` | 4 GiB file contents processed by packing/extraction; also rejects a single larger declared entry |
| `max_decoded_bytes` | 8 GiB outer Gzip/XZ/Zstd stream, including TAR headers, padding and skipped contents |
| `max_entries` | 65,536 inspected records |
| `max_depth` | 64 path components |
| `max_path_bytes` | 65,536 UTF-8 bytes including terminator |
| `max_memory_bytes` | 16 MiB first-party allocations, including accounting overhead |
| `workers` | One codec worker by default; 1–32 codec parallelism request, with possible fixed native helper threads |
| `deadline` | Infinite by default; finite values use the existing monotonic absolute deadline |

Native codec memory, dictionaries, solid blocks, interface-owned strings, runtime objects and filesystem caches are outside the first-party allocator budget. Output caps are not a hard memory or CPU sandbox. Inputs for which strong isolation is necessary belong in an application-owned helper process. No hard deadline or forced interruption of native code/filesystem calls is promised. Native ordinary-7z encoding can use a temporary spool outside the first-party memory budget; the source-content cap bounds input processed, while the output cap is enforced when data reaches our output stream.

Options and strings are borrowed until return. A progress callback may return `HX_OS_CANCELLED` to stop. Default libarchive callbacks use the calling thread; native 7-Zip progress can arrive on a codec thread. Progress callbacks do not overlap, do not run under library locks and may coalesce intermediate notifications. A callback borrows its snapshot only until it returns. Never throw or `longjmp` across this C boundary. Coordinate any UI handoff yourself.

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

`entries/files/directories` count inspected records. `content_bytes` counts processed source or extracted bytes; it is zero for metadata-only listing. `archive_bytes` is the greatest file offset reached, not a sum that double-counts seeks. `published` counts destination files already made visible: one for a completed pack, potentially several during unpack. A failure can carry nonzero progress. No worker or caller buffer is retained after the synchronous function returns.

## 6. Filesystem and failure behavior

Packing creates a sibling temporary, writes the archive, syncs it and publishes it atomically. The parent must already exist. By default, an existing target returns EXISTS. `HX_OS_ARCHIVE_OVERWRITE` permits replacement. Parent-directory sync is best effort, following `file_save_atomic`; success does not promise survival of every power-loss scenario. Packing an archive inside its source directory is rejected.

Extraction creates directories as needed and writes each regular file into a temporary under its selected parent. It publishes that file after the native entry checks. With `HX_OS_ARCHIVE_SYNC_FILES`, file data is synced and POSIX parent-directory sync is also requested; a later failure preserves the fact that publication already happened. Windows offers the file sync request without claiming equivalent directory durability.

Extraction is not a directory transaction. Earlier completed files and newly created directories remain if a later entry, callback or outer compression trailer fails. The current unpublished temporary is removed where possible; cleanup errors can leave a recoverable temporary. Do not treat a failed overall result as proof that the destination is unchanged. A metadata visit or skipped entry does not validate its payload CRC or authentication.

Absolute paths, parent traversal, drive/stream syntax, Windows device names and invalid UTF-8 fail. Leading `./` is normalized. Names are kept portable across the supported native filesystems; some otherwise valid POSIX names are deliberately unrepresentable. Symbolic/hard links, reparse points and special files are never restored. Existing regular files are replaced only with OVERWRITE. Duplicate target names fail by default; full extraction with OVERWRITE processes them in archive order. Native 7z single-entry selection rejects ambiguous duplicate names.

The operation anchors directory traversal with native handles/descriptors and never changes process working directory. Existing links below the destination are rejected. The caller keeps input/output trees stable; this is not protection against arbitrary concurrent namespace changes, including edits to Windows reparse metadata. Input files are not snapshotted.

Encrypted plaintext staging requires a filesystem capable of restricting access. POSIX verifies owner/mode and clears inherited Darwin ACLs; Windows uses a protected owner-only DACL. Unsupported filesystems report failure, including Windows-mounted WSL paths that cannot enforce the requested mode. Our transient data buffers are cleared on release; this does not promise erasure of all vendor buffers, caches or storage media. Existing OpenSSL error entries are preserved across default-backend work; marks are outside OpenSSL's public save/restore guarantee. Static OpenSSL users still follow its explicit thread cleanup and unload contract.

POSIX libarchive metadata conversion uses a temporary per-thread UTF-8 locale (`C.UTF-8`, or `en_US.UTF-8`). Progress/visit callbacks and injected allocator calls restore the caller's locale while executing. Overlapping operations share an immutable locale whose lifetime uses two short mutex sections per call. No lock spans codec work or callbacks, and the locale is released after the last operation. Process-global locale is not changed. A target missing both locale names returns NOT_SUPPORTED. Native Windows wide strings and native 7-Zip UTF-16 interface strings are converted inside the adapters.

## 7. C++11 direct calls

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

The C++ facade forwards to the C implementation and uses `Result<hx_os_archive_result>`. When a result carries a value, partial counters remain accessible even on failure. `forEach` accepts a `noexcept` callback returning void, a status or `Result<void>`. There is no exception propagation, hidden background queue or separate C++ codec implementation.

## 8. Run the archive example

```sh
build/bin/hx_os_archive_files pack input-folder records.zip
build/bin/hx_os_archive_files list records.zip
build/bin/hx_os_archive_files unpack records.zip restored-folder
```

Prepare input-folder first; the output archive and restored-folder should not already exist. Windows executables are under build/bin/Release with .exe suffix. See the earlier sections for options. The example needs no private interoperability/failure-injection tests; historical platform results are in the validation records.
