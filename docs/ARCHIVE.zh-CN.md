# 文件与目录压缩

[English](ARCHIVE.md) · [README](../README.zh-CN.md) · [依赖构建](DEPENDENCIES.zh-CN.md)

当应用需要把日志目录、配置快照或导出报表打包成 `.zip`、`.tar.gz`、`.tar.zst`、`.7z` 归档文件，或者在升级固件、导入数据包时安全解压外部归档（严防目录穿越、符号链接逃逸和压缩炸弹撑爆内存/磁盘）时，`hx_os_archive` 提供了统一的跨平台打包、遍历与解压接口。

你可以用一个最小目录快速跑通完整流程：新建一个名为 `records` 的目录，在里面放一个内容为 `{"device":42}` 的 `device.json` 文件。按第 2 节构建好 `hx_os_archive_files` 后，依次执行 `pack`（打包为 `records.zip`）、`list`（查看包内条目）和 `unpack`（解压到新目录）：`list` 会打印 `device.json`，解压出的同名文件则完整保留原 JSON 内容。初次使用无需配置密码或可选的原生 7-Zip 后端，先跑通标准 ZIP，再按需查阅后文的加密选项。

## 1. 先选择你要做的事

包含 `<hx_os.h>`（或单独包含 `<hx_os_archive.h>`）。所有常用归档操作均为同步一步调用，无需全局初始化、无需手动维护底层解压器状态机或手写分块读写循环：

| 你要做什么 | C 接口 | C++11 接口 |
| --- | --- | --- |
| 将单个文件或整个目录打包为压缩包 | `hx_os_archive_pack` | `hx::Archive::pack` |
| 将压缩包完整解压到目标目录 | `hx_os_archive_unpack` | `hx::Archive::unpack` |
| 仅从压缩包中提取指定的单个普通文件 | `hx_os_archive_extract_file` | `hx::Archive::extractFile` |
| 只读遍历查看包内条目列表（不解压落盘） | `hx_os_archive_visit` | `hx::Archive::forEach` |

**相对路径约定**：假设 `records/` 目录下包含 `device.json` 和 `images/photo.png`，对 `"records"` 调用打包时，生成的压缩包内直接存储 `device.json` 和 `images/photo.png` 这两个相对路径，**不会在顶层额外多套一层 `records/` 目录**；若直接打包单个文件，则存储其文件名（Basename）。空目录会被保留；若源目录中存在符号链接、硬链接或特殊设备文件，打包会明确报错拒绝，绝不静默跳过。

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

默认情况下，所有打包和解压接口都会保护已存在的目标文件（若目标文件已存在则返回 `HX_OS_EXISTS`）。

## 2. 构建并运行完整程序

`HX_OS_ENABLE_ARCHIVE` 默认开启（`ON`），依赖 `FILE_UTILS`、`CONTAINERS` 以及预编译依赖 SDK 中的 `libarchive`、`OpenSSL`、`zlib`、`liblzma` 和 `zstd`。若在裁剪构建时设置 `-DHX_OS_ENABLE_ARCHIVE=OFF`，函数符号依然可链接，但 `hx_os_archive_available()` 返回 `0`，所有归档操作统一返回 `HX_OS_NOT_SUPPORTED`（各模块的原生依赖关系见[原生依赖目标说明](NATIVE_DEPENDENCIES.zh-CN.md)）。

先按[依赖构建指南](DEPENDENCIES.zh-CN.md)准备好 SDK，再将 SDK 的绝对安装前缀传给 `HX_OS_DEPENDENCY_PREFIX` 并编译 `hx_os_archive_files` 示例：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHX_OS_DEPENDENCY_PREFIX=/absolute/project/build-deps/prefix \
  -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --target hx_os_archive_files
```

完整源码位于 [archive_files.c](../examples/archive_files.c)。准备好包含 `device.json` 的 `records/` 目录后，依次执行以下四条命令即可体验打包、列目录、整包解压与单文件提取：

```sh
./build/bin/hx_os_archive_files pack records records.zip
./build/bin/hx_os_archive_files list records.zip
./build/bin/hx_os_archive_files unpack records.zip restored
./build/bin/hx_os_archive_files extract records.zip device.json selected.json
```

（在 Windows Visual Studio 的 Release 构建下，可执行文件路径为 `build\bin\Release\hx_os_archive_files.exe`。）所有路径与密码参数均采用 UTF-8 编码。由于示例默认开启防覆盖保护，重复运行前请先删除上一次生成的 `records.zip`、`restored` 和 `selected.json`（或指定新路径）。

在外部已安装工程中，C 项目只需通过 `find_package(hx_c_platform CONFIG REQUIRED)` 链接 `hx_c_platform::hx_c_platform`（C++ 项目可链接 `hx_c_platform::cpp`），公开头文件不会暴露任何第三方压缩库头文件。

## 3. 格式与密码

| 文件后缀 / 显式格式枚举 | 打包单文件 | 打包目录 | 密码加密支持 |
| --- | --- | --- | --- |
| `.zip` / `HX_OS_ARCHIVE_ZIP` | 支持 | 支持 | 支持（AES-256 ZIP） |
| `.tar` / `HX_OS_ARCHIVE_TAR` | 支持 | 支持 | 不支持 |
| `.tar.gz`, `.tgz` / `HX_OS_ARCHIVE_TAR_GZIP` | 支持 | 支持 | 不支持 |
| `.tar.xz`, `.txz` / `HX_OS_ARCHIVE_TAR_XZ` | 支持 | 支持 | 不支持 |
| `.tar.zst`, `.tar.zstd` / `HX_OS_ARCHIVE_TAR_ZSTD` | 支持 | 支持 | 不支持 |
| `.7z` / `HX_OS_ARCHIVE_7ZIP` | 支持 | 支持 | 需配合第 4 节的可选原生 7-Zip 后端 |
| `.gz` / `HX_OS_ARCHIVE_GZIP` | 仅单文件 | 不支持 | 不支持 |
| `.xz` / `HX_OS_ARCHIVE_XZ` | 仅单文件 | 不支持 | 不支持 |
| `.zst`, `.zstd` / `HX_OS_ARCHIVE_ZSTD` | 仅单文件 | 不支持 | 不支持 |

- **格式推断与单文件压缩流**：
  - 打包时若格式设为默认的 `HX_OS_ARCHIVE_AUTO`，库会根据输出文件名的后缀自动选择格式；读取归档时则会自动探测容器内容（即使加密的 `.7z` 被改名为 `.blob` 也能正确识别）。
  - 对于 `.gz`、`.xz`、`.zst` 这类不含目录容器的**单文件压缩流**，解压出的文件名取自压缩包文件名去掉最后一层后缀（若去掉后缀后为空则命名为 `data`），**绝不会盲目信任并使用 Gzip 头部内嵌的原始文件名字段作为落盘路径**。
- **默认编解码栈與压缩预设（`preset`）**：
  - 默认后端完全在进程内调用 `libarchive`、`zlib`、`liblzma`、`Zstd` 与 `OpenSSL`（绝不会在后台调用系统的 `tar`、`gzip` 或 `7z` 外部命令）。其中外层的 Gzip / XZ / Zstd 流通过本库适配层直接调用各自的原生流式解压器，确保在 TAR 逻辑结尾之后依然完整校验外层压缩流的尾部校验和。
  - `options.preset` 可选择 `FAST`、`BALANCED`（默认）或 `SMALL`。

### 创建与解压带密码的 AES-256 ZIP

使用 `_ex` 扩展入口并通过 `hx_os_archive_options` 传入口令即可创建或解压 AES-256 加密 ZIP：

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

解压时将相同的 `options` 传给 `hx_os_archive_unpack_ex()` 或 `hx_os_archive_extract_file_ex()`。

> [!NOTE]
> **关于 ZIP 密码编码互通与加密边界的注意事项**
> - **跨工具密码编码差异**：ZIP 标准并未强制统一非 ASCII 口令的字节编码——本库统一使用 UTF-8 字节，而 Windows 版 7-Zip GUI 工具会将口令先转为系统本地代码页（`CP_ACP`）。如果需要与外部 Windows 7-Zip 工具交换加密 ZIP，**请使用纯 ASCII 密码**（注：第 4 节的原生 `.7z` 格式固定采用 UTF-16LE 密码接口，不受此代码页差异影响）。
> - **ZIP AES 不加密文件名**：标准 AES-256 ZIP 仅加密文件内容，包内的文件名目录树依然是明文可见的。如果你需要连同文件名一起加密，请使用第 4 节的 `.7z` 后端并开启 `HX_OS_ARCHIVE_ENCRYPT_NAMES`，或者先打成普通包再用[密码学模块的 `hx_os_crypto_encrypt_file()`](CRYPTO.zh-CN.md#3-加密文件或任意字节) 整体加密。
> - **错误码与诊断信息**：并非所有底层解压后端都能将密码错误精确区分映射为 `HX_OS_AUTH_FAILED`（部分后端会返回 `HX_OS_IO_ERROR` 并在 `error` 和 `options.diagnostic` 中附上可读的 UTF-8 诊断详情）。

## 4. 7z 的密码与文件名加密

默认的 `libarchive` 后端支持读写普通（无密码）`.7z` 归档。如果你需要**读写带密码的 `.7z` 归档或加密 `.7z` 内部的文件名列表**，可以启用独立的可选 **原生 7-Zip 后端**：
1. 在构建主库时开启 `-DHX_OS_ENABLE_7ZIP=ON`（同时保持 `LIBRARY` 与 `PATHS` 模块开启）。这会额外编译一个轻量的自有一方 C++11 动态适配桥接模块（关闭异常，通过私有 C ABI 与 C99 核心通信，不会将原生 7-Zip 代码静态塞进核心库）。
2. 使用仓库提供的辅助构建脚本，从固定版本的子模块 `deps/7zip`（26.04）编译出原生 7-Zip 动态库（注意：受上游 Makefile 限制，构建目录路径中**不能包含空格**；Windows 下请在 Visual Studio 开发者命令行中执行）：

```sh
cmake -S tools/sevenzip -B build-7zip
cmake --build build-7zip --config Release
```

构建完成后，Windows 生成 `build-7zip/runtime/7z.dll`，Linux 生成 `build-7zip/runtime/7z.so`（Linux ARM 与 Android NDK 交叉编译命令参见[交叉构建说明](DEPENDENCIES.zh-CN.md#6-linux-arm-与-android-交叉构建)）。

在代码中，将编译出的原生动态库**绝对路径**赋给 `options.seven_zip_library`，并按需设置 `HX_OS_ARCHIVE_ENCRYPT_NAMES` 即可：

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;

options.password = password_from_your_application;
options.seven_zip_library = absolute_trusted_7zip_runtime_path;
options.flags = HX_OS_ARCHIVE_ENCRYPT_NAMES;

hx_os_status status = hx_os_archive_pack_ex(NULL,
	"records", "private.7z", &options, NULL, NULL);
```

- **安全加载规则**：为了防止 DLL 劫持，`seven_zip_library` 必须传入可信动态库的**绝对路径**（绝不通过系统 `PATH` 搜索）。自有的桥接适配模块默认从核心库同目录（静态核心则为可执行文件同目录）加载，也可通过 `options.seven_zip_adapter` 显式指定绝对路径。桥接层在创建 COM 对象前会严格核对 26.04 版本与接口布局，不兼容的外部 `p7zip` 或旧版 `.so` 会安全返回 `HX_OS_NOT_SUPPORTED`。
- **开源许可证提示**：可选的原生 7-Zip 运行库遵循独立的 LGPL / BSD / unRAR 许可条款（与本库默认宽松许可证组合分开，因此核心 `cmake --install` 不会自动复制 `7z.dll` / `7z.so`）。如果你在产品中分发该可选运行库，请按[第三方开源说明](../THIRD_PARTY_NOTICES.zh-CN.md)保留对应的上游许可证声明。

## 5. 上限、取消和部分结果

为了防范恶意构造的“压缩炸弹（Zip Bomb）”或超深目录树耗尽系统资源，每次归档操作默认执行以下安全配额（均可通过 `hx_os_archive_options` 按需调整）：

| 默认配额选项 | 默认上限 / 含义说明 |
| --- | --- |
| `max_archive_bytes` | `4 GiB`：输入的压缩包文件大小或打包生成的压缩包文件上限 |
| `max_content_bytes` | `4 GiB`：累计打包或解压出的文件内容总字节数上限（单条声明超出此值的记录也会被立即拒绝） |
| `max_decoded_bytes` | `8 GiB`：外层 Gzip / XZ / Zstd 流解码出的总字节数上限（含 TAR 头部、对齐填充与跳过的条目） |
| `max_entries` | `65,536`：单次操作最多允许检查的归档条目总数 |
| `max_depth` | `64`：归档内相对路径的最大目录嵌套层数 |
| `max_path_bytes` | `65,536`：UTF-8 路径字符串（含结尾 `\0`）的最大字节长度 |
| `max_memory_bytes` | `16 MiB`：本库自有一方工作缓冲区与元数据的内存分配预算上限 |
| `workers` | 默认为 `1` 个编解码工作线程（可请求 `1..32` 的并行度） |
| `deadline` | 默认不限时（`INFINITE`）；可传入 `hx_os_deadline_after_ms(...)` 设定单调截止时间 |

在调用 `_ex` 接口时，传入初始化的 `hx_os_archive_result` 结构体即可获取已完成进度（即便中途因超时或进度回调返回 `HX_OS_CANCELLED` 而中止，该结构体依然会如实报告在失败前已经成功落盘发布的文件数量 `published`）：

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

## 6. 文件系统与失败行为

- **打包是原子的，而多文件解压不是目录级事务**：
  - **打包（`pack`）**：先在目标压缩包同目录下写入临时文件并请求落盘同步，全部写完且校验无误后才原子发布为目标文件名（禁止将输出压缩包路径设在源目录内部）。
  - **解压（`unpack`）**：每个提取出的普通文件都会先写入目标父目录下的临时文件，待该条目解压并校验通过后再原子发布。但请注意，**解压整个目录并不是一个可回滚的数据库事务**——如果解压到第 10 个文件时遇到损坏或取消，前 9 个已成功发布的文件和已创建的子目录会保留在磁盘上（可通过 `progress.published` 查看已发布数量）。
- **路径净化与防穿越（Zip Slip 防护）**：
  - 任何包含绝对路径、父目录穿越（`..`）、Windows 盘符/NTFS 数据流语法（`:`）、Windows 保留设备名（如 `CON`、`NUL`）或非法 UTF-8 的包内条目都会直接报错中止（开头的 `./` 会被自动规范化剥离）。
  - 解压过程**绝不会在磁盘上恢复符号链接、硬链接、重解析点（Reparse Points）或特殊设备文件**；遍历目标目录树时如果发现已存在的路径分量是符号链接，也会立即拒绝写入，防止通过预先放置的软链接写穿到外部目录。
- **加密条目解压的私有权限与 POSIX Locale 隔离**：
  - 解压加密归档时，明文临时文件强制要求文件系统支持仅所有者访问权限（POSIX `0600` 核实，Windows 仅所有者受保护 DACL；不支持权限隔离的挂载盘会在写入明文前失败）。
  - 在 POSIX 系统上，为了确保 `libarchive` 正确转换 UTF-8 文件名元数据，库会在当前调用线程上临时切换线程级 UTF-8 Locale（`uselocale`，优先尝试 `C.UTF-8` 与 `en_US.UTF-8`），并在执行用户回调与自定义分配器期间自动恢复调用方原有的线程 Locale，**绝不修改进程全局的 `setlocale()`**。

## 7. C++11 直接调用

包含 `<hx/Archive.hpp>` 即可使用零异常的 `hx::Archive` 静态方法，返回值统一为 `hx::Result<hx_os_archive_result>`：

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

`hx::Archive::forEach` 接收 `noexcept` 回调（回调可返回 `void`、`hx_os_status` 或 `hx::Result<void>` 以提前中止遍历）。当操作失败但结果对象中已包含进度统计时，依然可以通过 `packed.value()` 查看已处理条目与已发布文件计数。

## 8. 运行压缩示例

```sh
build/bin/hx_os_archive_files pack input-folder records.zip
build/bin/hx_os_archive_files list records.zip
build/bin/hx_os_archive_files unpack records.zip restored-folder
```

运行前请先准备好非空的 `input-folder` 目录，并确保 `records.zip` 与 `restored-folder` 尚未存在（Windows 下可执行文件位于 `build/bin/Release/hx_os_archive_files.exe`）。各目标平台的运行库部署要求见[平台支持说明](PLATFORMS.zh-CN.md)。
