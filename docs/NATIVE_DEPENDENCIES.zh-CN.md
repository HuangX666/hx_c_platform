# 直接调用底层依赖库

[English](NATIVE_DEPENDENCIES.md) · [README](../README.zh-CN.md)

**需要便利层以外的 SDK 功能时，继续用同一套底层库。** 例如已有 SQLite 代码仍可 include sqlite3.h、执行 SQL，不必为使用消息队列接口另带一套 SQLite。下面先说明 CMake 链接目标，再给一个只在内存中创建 sample 表的完整程序；成功退出码为 0，不会创建磁盘数据库。使用前仍需按[安装包接入教程](USAGE.zh-CN.md#installed-project)提供 prefix。

## 1. 一次接入

便利接口和底层原生 API 可以一起使用。安装后，业务项目这样接入：

```cmake
find_package(hx_c_platform CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE hx_c_platform::native)
```

`native` 包含 hx_c_platform 和本次构建启用的原生 SDK。需要减少依赖时，也可以分别链接核心和下面的目标。

| 目标 | 原生头文件 | 启用条件 |
| --- | --- | --- |
| hx_c_platform::sqlite | sqlite3.h、sqlite3ext.h | PERSISTENT_QUEUE，并启用 RANDOM、THREAD_EX |
| hx_c_platform::openssl | openssl/evp.h、openssl/ssl.h | CRYPTO、ARCHIVE、HTTP 或 URL |
| hx_c_platform::curl | curl/curl.h | HTTP / URL |
| hx_c_platform::utf8proc | utf8proc.h | UNICODE |
| hx_c_platform::archive | archive.h、archive_entry.h | ARCHIVE 及其前置模块 |
| hx_c_platform::zlib | zlib.h | ARCHIVE / COMPRESSION |
| hx_c_platform::lzma | lzma.h | ARCHIVE / COMPRESSION |
| hx_c_platform::zstd | zstd.h | ARCHIVE / COMPRESSION |

被裁剪的 SDK 不导出对应目标。现有五个配置解析后端和 KCP 使用的内部符号继续隔离，没有通过这个目标公开为原生 SDK。

## 2. 直接使用 SQLite

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

这个连接由业务自己持有，使用与持久化队列相同的 SQLite SDK，不需要先创建队列对象。同样可以直接调用 OpenSSL 的 EVP／SSL、libcurl 的 easy／multi／URL、utf8proc 以及各压缩库的 API；原生 API 的行为由对应库规定。

完整的 [native_dependencies.c](../examples/native_dependencies.c) 示例直接调用了 SQLite、EVP、CURLU、utf8proc 和已启用的压缩 SDK：

```sh
cmake --build build-shared-linux --target hx_os_native_dependencies
build-shared-linux/bin/hx_os_native_dependencies
```

复用已有构建目录。Windows 添加 `--config Debug`，运行 `bin/Debug/hx_os_native_dependencies.exe`。

## 3. 对象归属与初始化

业务创建的原生对象，通过对应 API 释放，例如 sqlite3_close、EVP_*_free、curl_easy_cleanup／curl_url_cleanup、archive_*_free。这些对象与 hx_c_platform 内部对象分别持有；原生库返回的内存按照它自己的分配规则释放。已有操作系统句柄借用接口见[原生对象互操作](NATIVE_INTEROP.zh-CN.md)。

进程级原生配置和清理由宿主协调。库或其他消费者仍在使用时，不要更换 SQLite／OpenSSL 的全局分配器，也不要调用 sqlite3_shutdown／OPENSSL_cleanup。原生 curl_global_init／cleanup 要配对，相关工作完成后才释放宿主的初始化。本库分配器负责本库自己的内存，原生库仍使用它们的分配策略。

头文件、导入库和运行库使用同一套安装包。不要额外混入另一个版本的 OpenSSL／libcurl／SQLite，再把两套库的对象交叉使用。业务直接调用原生接口，不改变自有 C++ 源码禁用异常的约定。

## 4. 安装与发布

安装包带有所选 SDK 的头文件、CMake 导入文件和运行所需产物。Windows 动态库放在 `bin/`，Linux 放在 `lib/`。Windows 发布时把 DLL 放在程序旁边，或把这套安装包的 `bin/` 加入 PATH。Linux 保留共享库符号链接和本地运行库搜索路径。

在自己的目标环境运行安装后的消费者示例，检查头文件、链接与运行库部署；桌面结果不能代替 Android、板卡或旧系统验收。发布底层 SDK 时保留[第三方许可说明](../THIRD_PARTY_NOTICES.zh-CN.md)。

可以通过 HX_OS_DEPENDENCY_PREFIX 或普通的 CMAKE_PREFIX_PATH 选择准备好的 SDK。安装时使用 find_package 实际选中目标的头文件目录和包配置位置，同时保留 Unix 库的符号链接链，不改写第三方 SDK 文件。

静态 hx_c_platform 的 `pkg-config --libs --static hx_c_platform` 会包含已启用的 curl、utf8proc、hx_sqlite3 及平台依赖，包括需要的 libm、librt。SQLite 及默认 curl／utf8proc 仍为共享库；静态链接本库不等于整个程序全部静态链接。PKG_CONFIG_PATH 需要包含本库安装目录的 lib/pkgconfig 和准备好的 SDK 的 lib/pkgconfig，以找到 OpenSSL／归档库的元数据。CMake 消费者直接使用导出目标。
