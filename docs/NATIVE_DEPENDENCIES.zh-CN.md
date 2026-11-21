# 直接调用底层依赖库

[English](NATIVE_DEPENDENCIES.md) · [README](../README.zh-CN.md)

当你的业务代码除了使用 `hx_c_platform` 提供的跨平台封装接口外，还需要直接调用底层第三方 SDK 的原生功能（例如已有的业务模块直接 `#include <sqlite3.h>` 执行自定义 SQL 查询，或直接调用 `OpenSSL`、`libcurl`、`utf8proc`、`zstd` 的底层 API）时，可以直接复用本库配套构建并导出的同一套底层依赖库，无需在项目中重复打包第二份 SQLite 或 OpenSSL。

在使用已安装的 SDK 包之前，请先按[安装包接入教程](USAGE.zh-CN.md#installed-project)配置好 `CMAKE_PREFIX_PATH`。

## 1. 一次接入

`hx_c_platform` 的高层便利接口与底层第三方原生 SDK 可以在同一个可执行文件或动态库中共存。在业务项目的 `CMakeLists.txt` 中，只需链接 `hx_c_platform::native` 聚合目标：

```cmake
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE hx_c_platform::native)
```

`hx_c_platform::native` 会自动链接 `hx_c_platform` 核心库以及当前构建中所有已启用的原生 SDK 目标。如果你希望控制链接范围、只引入实际用到的底层库，也可以单独链接核心目标与下表中的具体子目标：

| CMake 导出目标 | 暴露的原生头文件 | 启用条件（CMake 开关） |
| --- | --- | --- |
| `hx_c_platform::sqlite` | `sqlite3.h`、`sqlite3ext.h` | 启用 `HX_OS_ENABLE_PERSISTENT_QUEUE`（同时需启用 `HX_OS_ENABLE_RANDOM` 与 `HX_OS_ENABLE_THREAD_EX`） |
| `hx_c_platform::openssl` | `openssl/evp.h`、`openssl/ssl.h` | 启用 `HX_OS_ENABLE_CRYPTO`、`HX_OS_ENABLE_ARCHIVE`、`HX_OS_ENABLE_HTTP` 或 `HX_OS_ENABLE_URL` |
| `hx_c_platform::cares` | `ares.h` | 启用 `HX_OS_ENABLE_NETWORK` 与 `HX_OS_ENABLE_CARES` |
| `hx_c_platform::curl` | `curl/curl.h` | 启用 `HX_OS_ENABLE_HTTP` 或 `HX_OS_ENABLE_URL` |
| `hx_c_platform::utf8proc` | `utf8proc.h` | 启用 `HX_OS_ENABLE_UNICODE` |
| `hx_c_platform::archive` | `archive.h`、`archive_entry.h` | 启用 `HX_OS_ENABLE_ARCHIVE` 及其前置模块 |
| `hx_c_platform::zlib` | `zlib.h` | 启用 `HX_OS_ENABLE_ARCHIVE` 或 `HX_OS_ENABLE_COMPRESSION` |
| `hx_c_platform::lzma` | `lzma.h` | 启用 `HX_OS_ENABLE_ARCHIVE` 或 `HX_OS_ENABLE_COMPRESSION` |
| `hx_c_platform::zstd` | `zstd.h` | 启用 `HX_OS_ENABLE_ARCHIVE` 或 `HX_OS_ENABLE_COMPRESSION` |

> [!NOTE]
> 如果某个模块在编译 `hx_c_platform` 时被裁剪关闭，对应的 `hx_c_platform::<sdk>` 目标将不会被导出。此外，库内部私有的五个配置解析后端（JSON/YAML/TOML/XML/INI）以及内部 KCP 实现均经过了符号隔离，不作为公共原生 SDK 目标向外导出。

## 2. 直接使用 SQLite

链接 `hx_c_platform::native`（或 `hx_c_platform::sqlite`）后，业务代码可以直接包含 `<sqlite3.h>` 并操作自己的数据库连接。下面的完整程序在内存中创建一张 `sample` 表并以状态码 `0` 退出（不会在磁盘上生成数据库文件）：

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

这个 `sqlite3 *` 连接由业务代码独立持有和关闭，它与本库持久化队列（`hx_os_persistent_queue`）共享同一个底层 SQLite 运行库，但无需先创建队列对象。同理，你也可以直接调用 OpenSSL 的 `EVP_*` / `SSL_*`、libcurl 的 `curl_easy_*` / `curl_multi_*` / `CURLU`、`utf8proc` 以及 `libarchive` / `zlib` / `lzma` / `zstd` 的原生函数。

仓库中的完整示例 [native_dependencies.c](../examples/native_dependencies.c) 演示了在同一个程序中直接调用 SQLite、OpenSSL EVP、libcurl URL、`utf8proc` 以及各个已启用的压缩 SDK：

```sh
# Linux / macOS（复用已有构建目录）：
cmake --build build-shared-linux --target hx_os_native_dependencies
./build-shared-linux/bin/hx_os_native_dependencies

# Windows PowerShell（以 Debug 配置为例）：
cmake --build build-shared-msvc --config Debug `
    --target hx_os_native_dependencies
.\build-shared-msvc\bin\Debug\hx_os_native_dependencies.exe
```

## 3. 对象归属与初始化

- **谁创建谁释放**：业务代码直接调用原生 SDK 创建的对象，必须通过对应 SDK 的原生释放函数销毁（如 `sqlite3_close`、`EVP_MD_CTX_free`、`curl_easy_cleanup` / `curl_url_cleanup`、`archive_read_free` 等）。这些对象与 `hx_c_platform` 自身管理的对象完全独立；原生 SDK 分配的内存也遵循其自身的分配器规则（向 `hx_c_platform` 注入自定义 `hx_context` 分配器只接管本库自身的内存分配，不会替换底层第三方 SDK 的内存分配器）。如果你需要从本库已有的文件、套接字、线程或进程对象中借出操作系统底层句柄，请参阅[原生对象互操作](NATIVE_INTEROP.zh-CN.md)。
- **进程级全局状态协调**：当 `hx_c_platform` 或进程内的其他模块仍在使用对应 SDK 时，**切勿**在外部篡改 SQLite 或 OpenSSL 的进程级全局分配器，也**切勿**提前调用 `sqlite3_shutdown()` 或 `OPENSSL_cleanup()`。若业务代码显式调用了 `curl_global_init()`，请确保与 `curl_global_cleanup()` 严格配对，并在所有相关请求结束后再执行清理。
- **严禁混用不同版本的同名运行库**：编译、链接和运行部署时，请始终成套使用同一安装前缀下的头文件、导入库与动态运行库（`.dll` / `.so` / `.dylib`）。不要在同一个进程内混入另一套不同编译配置或不同版本的 OpenSSL、libcurl 或 SQLite，更不能将两套运行库创建的对象交叉传递。

## 4. 安装与发布

如果你手里只有编译打包好的 SDK 产物，建议先按[安装包接入教程](INSTALL.zh-CN.md)跑通最小 C/C++ 文件读取示例（其中包含完整的 `find_package` 命令以及 Windows `PATH` / Linux `LD_LIBRARY_PATH` 运行库配置说明），随后再按本文引入原生 SDK 调用：

- **运行库部署位置**：安装包的 Windows 动态库（`.dll`）位于 `<prefix>/bin/`，Linux/macOS 共享库位于 `<prefix>/lib/`。发布 Windows 应用程序时，请将所需的 `.dll` 放置在可执行文件同级目录下，或将安装包的 `bin/` 加入进程 `PATH`；在 Linux 上请保留共享库的 SONAME 符号链接链与运行库搜索路径（RPATH / `LD_LIBRARY_PATH`）。
- **构建时定位预编译依赖**：编译本库时，可通过 `-DHX_OS_DEPENDENCY_PREFIX=<path>` 或标准 `CMAKE_PREFIX_PATH` 指定预先准备好的第三方依赖 SDK。`cmake --install` 会自动提取 `find_package` 实际选中的目标头文件目录、CMake 配置及 Unix 符号链接链进行打包，不会篡改第三方源码或预编译产物。分发包含底层第三方 SDK 的软件时，请一并保留[第三方许可说明](../THIRD_PARTY_NOTICES.zh-CN.md)。
- **`pkg-config` 静态链接注意事项**：当构建静态版本的 `hx_c_platform` 时，执行 `pkg-config --libs --static hx_c_platform` 会自动输出已启用的 `curl`、`utf8proc`、`hx_sqlite3` 以及所需的系统库（如 `libm`、`librt`）。请注意，SQLite 以及默认配置下的 `curl` / `utf8proc` 依然以共享库形式提供——静态链接 `hx_c_platform` 核心库并不意味着整个可执行文件变成纯静态二进制。使用 `pkg-config` 时，请将本库安装目录的 `lib/pkgconfig` 与依赖 SDK 的 `lib/pkgconfig` 一并加入 `PKG_CONFIG_PATH`，以便正确解析 OpenSSL 与 `libarchive` 的传递依赖（CMake 用户直接链接导出目标即可，无需手动配置 `pkg-config`）。
