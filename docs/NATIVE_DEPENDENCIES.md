# Direct use of native dependencies

[简体中文](NATIVE_DEPENDENCIES.zh-CN.md) · [README](../README.md)

When your application needs to call an underlying third-party SDK directly—for example, when existing code includes `<sqlite3.h>` to run custom SQL queries, or calls `OpenSSL`, `libcurl`, `utf8proc`, or `zstd` APIs not covered by high-level wrappers—you can link against the exact same native dependency targets built and shipped with `hx_c_platform` instead of bundling duplicate copies of SQLite or OpenSSL.

Before consuming an installed package, configure your build prefix as shown in the [installed package setup](USAGE.md#installed-project).

## 1. Link once

High-level `hx_c_platform` wrappers and raw third-party SDK calls can coexist in the same target. In your project's `CMakeLists.txt`, link against `hx_c_platform::native`:

```cmake
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE hx_c_platform::native)
```

`hx_c_platform::native` links `hx_c_platform` plus every native SDK target enabled in that build. If you prefer finer-grained linkage, link `hx_c_platform` alongside only the specific SDK targets your target needs:

| Exported CMake Target | Native Headers | Enabled When |
| --- | --- | --- |
| `hx_c_platform::sqlite` | `sqlite3.h`, `sqlite3ext.h` | `HX_OS_ENABLE_PERSISTENT_QUEUE` (with `HX_OS_ENABLE_RANDOM` and `HX_OS_ENABLE_THREAD_EX`) |
| `hx_c_platform::openssl` | `openssl/evp.h`, `openssl/ssl.h` | `HX_OS_ENABLE_CRYPTO`, `HX_OS_ENABLE_ARCHIVE`, `HX_OS_ENABLE_HTTP`, or `HX_OS_ENABLE_URL` |
| `hx_c_platform::cares` | `ares.h` | `HX_OS_ENABLE_NETWORK` and `HX_OS_ENABLE_CARES` |
| `hx_c_platform::curl` | `curl/curl.h` | `HX_OS_ENABLE_HTTP` or `HX_OS_ENABLE_URL` |
| `hx_c_platform::utf8proc` | `utf8proc.h` | `HX_OS_ENABLE_UNICODE` |
| `hx_c_platform::archive` | `archive.h`, `archive_entry.h` | `HX_OS_ENABLE_ARCHIVE` and its prerequisites |
| `hx_c_platform::zlib` | `zlib.h` | `HX_OS_ENABLE_ARCHIVE` or `HX_OS_ENABLE_COMPRESSION` |
| `hx_c_platform::lzma` | `lzma.h` | `HX_OS_ENABLE_ARCHIVE` or `HX_OS_ENABLE_COMPRESSION` |
| `hx_c_platform::zstd` | `zstd.h` | `HX_OS_ENABLE_ARCHIVE` or `HX_OS_ENABLE_COMPRESSION` |

> [!NOTE]
> SDKs disabled at build time do not export their corresponding `hx_c_platform::<sdk>` target. The five privately embedded configuration-parser backends (JSON/YAML/TOML/XML/INI) and private KCP symbols remain internally isolated and are not exported as public SDK targets.

## 2. A native SQLite call

Once linked, your code can include `<sqlite3.h>` and manage its own SQLite connections directly. The complete C program below creates an in-memory `sample` table and exits with `0` without creating any file on disk:

```c
#include <sqlite3.h>

int main(void)
{
	sqlite3 *database = NULL;
	int status = sqlite3_open(":memory:", &database);

	if (status == SQLITE_OK)
		status = sqlite3_exec(database,
			"CREATE TABLE sample(id INTEGER)", NULL, NULL, NULL);

	if (database)
		sqlite3_close(database);

	return status == SQLITE_OK ? 0 : 1;
}
```

This `sqlite3 *` handle is owned entirely by the application—it uses the same SQLite runtime as `hx_os_persistent_queue` without needing to open a persistent queue first. You can call OpenSSL `EVP_*` / `SSL_*`, libcurl `curl_easy_*` / `curl_multi_*` / `CURLU`, `utf8proc`, and `libarchive` / `zlib` / `lzma` / `zstd` functions in the exact same way.

The complete [native_dependencies.c](../examples/native_dependencies.c) example exercises direct calls into SQLite, OpenSSL EVP, CURLU, `utf8proc`, and the enabled compression SDKs in a single executable:

```sh
# Linux / macOS (using an existing build directory):
cmake --build build-shared-linux --target hx_os_native_dependencies
./build-shared-linux/bin/hx_os_native_dependencies

# Windows PowerShell (Debug configuration example):
cmake --build build-shared-msvc --config Debug `
    --target hx_os_native_dependencies
.\build-shared-msvc\bin\Debug\hx_os_native_dependencies.exe
```

## 3. Ownership and initialization

- **Release native objects through their native APIs**: Every object you create directly through a third-party SDK must be freed with that SDK's own deallocation function (`sqlite3_close`, `EVP_MD_CTX_free`, `curl_easy_cleanup` / `curl_url_cleanup`, `archive_read_free`, etc.). These objects are independent of handles owned by `hx_c_platform`, and native SDK allocations continue to use the SDK's own allocator policy (injecting a custom `hx_context` allocator into `hx_c_platform` governs first-party allocations only). To borrow OS handles (`HANDLE`, `SOCKET`, `fd`, `pthread_t`) from objects owned by `hx_c_platform`, see [native interoperability](NATIVE_INTEROP.md).
- **Coordinate process-wide SDK state**: Never replace SQLite or OpenSSL global allocators or call `sqlite3_shutdown()` / `OPENSSL_cleanup()` while `hx_c_platform` or another consumer in the process is still using those runtimes. Balance any explicit `curl_global_init()` / `curl_global_cleanup()` calls and ensure all active transfers have finished before final cleanup.
- **Do not mix mismatched SDK builds**: Always use the headers, import libraries, and runtime binaries (`.dll` / `.so` / `.dylib`) from the same installation set. Never load a second, differently compiled copy of OpenSSL, libcurl, or SQLite into the same process and pass objects across them.

## 4. Installation and deployment

If you are starting from a prebuilt SDK package, follow the [installed-package guide](INSTALL.md) first to build and run the minimal C or C++ file-reading example (including `find_package` setup and Windows `PATH` / Linux `LD_LIBRARY_PATH` runtime configuration), then add direct native SDK calls as shown above:

- **Deploying shared libraries**: Installed Windows DLLs are placed under `<prefix>/bin/`, and Linux/macOS shared libraries under `<prefix>/lib/`. On Windows, copy the required DLLs next to your executable or add the installation's `bin/` directory to `PATH`; on Linux, preserve the shared-library symlink chains and runtime search paths (`RPATH` / `LD_LIBRARY_PATH`).
- **Selecting prebuilt dependencies at build time**: When building `hx_c_platform` from source, point `-DHX_OS_DEPENDENCY_PREFIX=<path>` or `CMAKE_PREFIX_PATH` at your prepared dependency SDK. `cmake --install` packages the headers, CMake targets, and Unix library symlink chains of the targets actually selected by `find_package` without modifying vendor files. Always retain the [third-party notices](../THIRD_PARTY_NOTICES.md) when redistributing native SDK binaries.
- **Static `pkg-config` consumers**: When linking against a static `hx_c_platform` library via `pkg-config --libs --static hx_c_platform`, the output flags include enabled `curl`, `utf8proc`, `hx_sqlite3`, and required system libraries (such as `libm` and `librt`). Note that SQLite and the default `curl` / `utf8proc` builds remain shared libraries—statically linking the `hx_c_platform` core does not make the entire executable statically linked. Set `PKG_CONFIG_PATH` to include both the installed library's `lib/pkgconfig` and the dependency SDK's `lib/pkgconfig` so transitive OpenSSL and `libarchive` metadata can be resolved (CMake consumers can simply link the exported CMake targets).
