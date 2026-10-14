# Direct use of native dependencies

[简体中文](NATIVE_DEPENDENCIES.zh-CN.md) · [README](../README.md)

## 1. Link once

The convenience API and the native SDK can coexist. Use the installed package, or the existing source target:

```cmake
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE hx_c_platform::native)
```

`native` links hx_c_platform and every enabled native SDK target. For a smaller dependency set, link the core and selected targets individually.

| Target | Native headers | Enabled with |
| --- | --- | --- |
| hx_c_platform::sqlite | sqlite3.h, sqlite3ext.h | PERSISTENT_QUEUE with RANDOM and THREAD_EX |
| hx_c_platform::openssl | openssl/evp.h, openssl/ssl.h | CRYPTO, ARCHIVE, HTTP or URL |
| hx_c_platform::curl | curl/curl.h | HTTP / URL |
| hx_c_platform::utf8proc | utf8proc.h | UNICODE |
| hx_c_platform::archive | archive.h, archive_entry.h | ARCHIVE and its prerequisites |
| hx_c_platform::zlib | zlib.h | ARCHIVE / COMPRESSION |
| hx_c_platform::lzma | lzma.h | ARCHIVE / COMPRESSION |
| hx_c_platform::zstd | zstd.h | ARCHIVE / COMPRESSION |

Disabled SDKs do not export their target. The five privately embedded configuration-parser backends and private KCP symbols are not native SDK exports; their internal symbol isolation remains intact.

## 2. A native SQLite call

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

This is an application-owned SQLite connection. It uses the same installed SQLite SDK as the persistent queue, without acquiring a queue handle. OpenSSL EVP/SSL, libcurl easy/multi/URL, utf8proc and archive/compression APIs remain callable in the same manner. The wrapper does not replace their native semantics.

The complete [native_dependencies.c](../examples/native_dependencies.c) example calls SQLite, EVP, CURLU, utf8proc and enabled compression SDKs directly:

```sh
cmake --build build-shared-linux --target hx_os_native_dependencies
build-shared-linux/bin/hx_os_native_dependencies
```

Use your existing matching build directory. Windows uses `--config Debug` and `bin/Debug/hx_os_native_dependencies.exe`.

## 3. Ownership and initialization

Create and release each native object through its matching native API: sqlite3_close, EVP_*_free, curl_easy_cleanup/curl_url_cleanup, archive_*_free, and the allocator documented by the native provider. These objects are independent from objects owned by hx_c_platform. Existing platform-handle borrowing contracts are described in [native interoperability](NATIVE_INTEROP.md).

Native process-wide configuration and cleanup are host responsibilities. Do not change SQLite/OpenSSL global allocators or call sqlite3_shutdown/OPENSSL_cleanup while this library or another consumer still uses that runtime. Balance native curl_global_init/cleanup and finish all associated work before releasing the host's initialization. Library allocator injection controls its own storage; native providers retain their native allocation policy.

Use the shipped headers, import libraries and runtime as one set. Do not mix a different OpenSSL/libcurl/SQLite build into the process and assume compatible objects can cross between them. Native C++ callers continue to obey the project's exception-free first-party policy.

## 4. Installation and deployment

The installation includes the selected SDK headers, CMake imports and required artifacts. Shared runtimes use `bin/` on Windows and `lib/` on Linux. Deploy Windows DLLs alongside the executable or put that installation's `bin/` on PATH. Linux library symlink chains and local runtime search paths are retained.

Use the installed consumer example to check header discovery, linking and runtime deployment on your target; desktop results do not establish Android, board or older-system support. Preserve the [third-party notices](../THIRD_PARTY_NOTICES.md) when redistributing the native SDK.

The prepared SDK can be selected with HX_OS_DEPENDENCY_PREFIX or ordinary CMAKE_PREFIX_PATH. Installation uses the include directories and package locations of the imported targets actually selected by find_package, including their Unix library symlink chains. No vendor SDK files are rewritten.

For a static hx_c_platform library, `pkg-config --libs --static hx_c_platform` includes enabled curl, utf8proc, hx_sqlite3 and platform dependencies (including libm and librt where needed). SQLite and the default curl/utf8proc providers remain shared libraries; static core linkage does not make the whole application static. Set PKG_CONFIG_PATH to the installed library's lib/pkgconfig plus the prepared SDK's lib/pkgconfig so its OpenSSL/archive metadata can be resolved. CMake consumers use the exported targets instead.
