# 文件与目录压缩

[English](ARCHIVE.md) · [README](../README.zh-CN.md) · [依赖构建](DEPENDENCIES.zh-CN.md)

## 1. 先选择你要做的事

包含 `<hx_os.h>`，也可以只包含 `<hx_os_archive.h>`。常用操作不需要全局初始化，不需要自己管理压缩对象、组织读写循环或配置底层编解码器。

| 想做什么 | C 入口 | C++11 入口 |
| --- | --- | --- |
| 把一个文件或目录打成压缩包 | `hx_os_archive_pack` | `hx::Archive::pack` |
| 解压到一个目录 | `hx_os_archive_unpack` | `hx::Archive::unpack` |
| 只取出指定的普通文件 | `hx_os_archive_extract_file` | `hx::Archive::extractFile` |
| 查看包内条目，不解压 | `hx_os_archive_visit` | `hx::Archive::forEach` |

例如，`records/` 中有 `device.json` 和 `images/photo.png`，压缩包内就是这两个相对路径，不会额外套一层 `records/`。打包单个文件时使用它的文件名。空目录会保留；打包时遇到链接或特殊文件会明确失败。

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

默认保留已有目标文件。压缩方式不支持、名字不安全或文件已经存在时，函数返回明确状态，不会静默跳过。

## 2. 构建并运行完整程序

ARCHIVE 默认开启，需要 FILE_UTILS、CONTAINERS，以及准备好的 libarchive、OpenSSL 和压缩依赖 SDK。设置 `HX_OS_ENABLE_ARCHIVE=OFF` 可以裁剪：函数仍可链接，但操作返回 `HX_OS_NOT_SUPPORTED`，`hx_os_archive_available()` 返回 0。完全不需要 OpenSSL 时，同时关闭 ARCHIVE 与 CRYPTO。

先按[依赖构建说明](DEPENDENCIES.zh-CN.md)准备 SDK，再用它的绝对 prefix 路径配置本库：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DHX_OS_DEPENDENCY_PREFIX=/absolute/project/build-deps/prefix \
  -DHX_OS_BUILD_EXAMPLES=ON
cmake --build build --config Release --parallel 4
```

完整 C 程序在 [archive_files.c](../examples/archive_files.c)。它直接使用这些公共入口，失败时显示复制后的诊断，最后报告已经发布了多少文件。示例复用已有参数辅助层，将 Windows 命令行参数转换为 UTF-8。

```sh
./build/bin/hx_os_archive_files pack records records.zip
./build/bin/hx_os_archive_files list records.zip
./build/bin/hx_os_archive_files unpack records.zip restored
./build/bin/hx_os_archive_files extract records.zip device.json selected.json
```

Visual Studio 的 Release 构建使用 `build\bin\Release\hx_os_archive_files.exe`。输入目录 `records` 需要事先放好文件；解压目录 `restored` 不存在时会创建。路径和密码统一为 UTF-8。示例默认不覆盖已有文件，重复运行时需要选择新的输出位置或先自行处理旧文件。

安装后的纯 C 项目通过 `find_package(hx_c_platform CONFIG REQUIRED)` 链接 `hx_c_platform::hx_c_platform`；C++ 项目可以链接 `hx_c_platform::cpp`。公共头不暴露第三方压缩头文件。

## 3. 格式与密码

| 输出后缀／显式格式 | 文件 | 目录 | 密码 |
| --- | --- | --- | --- |
| `.zip`／`HX_OS_ARCHIVE_ZIP` | 支持 | 支持 | AES-256 ZIP |
| `.tar`／`HX_OS_ARCHIVE_TAR` | 支持 | 支持 | 不支持 |
| `.tar.gz`、`.tgz`／`HX_OS_ARCHIVE_TAR_GZIP` | 支持 | 支持 | 不支持 |
| `.tar.xz`、`.txz`／`HX_OS_ARCHIVE_TAR_XZ` | 支持 | 支持 | 不支持 |
| `.tar.zst`、`.tar.zstd`／`HX_OS_ARCHIVE_TAR_ZSTD` | 支持 | 支持 | 不支持 |
| `.7z`／`HX_OS_ARCHIVE_7ZIP` | 支持 | 支持 | 通过可选原生 7-Zip 后端 |
| `.gz`／`HX_OS_ARCHIVE_GZIP` | 支持 | 不支持 | 不支持 |
| `.xz`／`HX_OS_ARCHIVE_XZ` | 支持 | 不支持 | 不支持 |
| `.zst`、`.zstd`／`HX_OS_ARCHIVE_ZSTD` | 支持 | 不支持 | 不支持 |

打包时 `AUTO` 根据输出后缀选择格式。读取时识别容器内容；加密 7z 改名成 `.blob` 也可以识别。单独的压缩后缀表示解出一个文件，显式指定格式可消除歧义。单文件压缩流的输出文件名为压缩包文件名去掉最后一个扩展名，没有可用名字时使用 `data`。不会把 Gzip 头中附带的文件名直接作为输出路径。支持空文件与连接在一起的多个 Gzip member。

默认后端由 libarchive、zlib、liblzma、Zstd 和 OpenSSL 组成。外层 Gzip/XZ/Zstd 流通过适配层中的原生流式解压器读取，确保 TAR 已到逻辑结尾时仍完成外层流的校验。不调用外部 `tar`、`gzip` 或 `7z` 命令作为回退。ZIP 使用 Deflate，普通 7z 使用 libarchive 选择的编解码器。`preset` 可以选择 FAST、BALANCED（默认）或 SMALL；它们不是各格式完全相同的数字压缩等级。

密码 ZIP 只需增加选项：

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

`HX_OS_ERROR_ARCHIVE` 的 `native_code` 保留原生错误码；`cause_code` 按 `int64_t` 解读为负数时是 libarchive 返回值，此时 `native_code` 为其 errno；GZIP／XZ／ZSTD 格式常量标识外层解码器错误，零值标识原生 7-Zip 的 HRESULT／操作码。可选诊断字符串提供复制后的可读说明。原生文字只保留有效的 UTF-8 前缀；如果没有可保留的内容，使用包含最终状态的 `Archive operation failed: <status>` 兜底。如果诊断字符串自身的分配器失败，复制诊断只能尽力完成，仍以返回状态和原生错误为准。

解压时把同样的选项传给 `unpack_ex` 或 `extract_file_ex`。错误密码会失败，复制后的原生诊断用于解释细节。并非所有后端的密码错误都能精确映射为 `AUTH_FAILED`；有些返回 `IO_ERROR` 并保留归档错误详情。ZIP AES 不隐藏文件名。 不同工具对 ZIP 密码字节的编码约定并不统一：本库使用 UTF-8，Windows 7-Zip 会经过 CP_ACP 转换。与该工具交换密码 ZIP 时使用 ASCII 密码；不能保证非 ASCII 密码 ZIP 在该外部程序中互通。原生 7z 的 UTF-16 密码接口不受此差异影响。CRC 用来检测损坏，不能认证发送者。加密 7z 也不等同于独立[加密模块](CRYPTO.zh-CN.md)提供的应用层认证信封契约。

示例从环境变量 `HX_ARCHIVE_PASSWORD` 读取密码，不把密码放进命令行参数。实际应用可以直接传入内存中的密码。环境变量只是演示输入方式，不是密码保险箱。

## 4. 7z 的密码与文件名加密

这是单独可选的后端，不替换默认后端，也不把原生 7-Zip 实现静态合进核心。开启 `HX_OS_ENABLE_7ZIP=ON`，并保留 LIBRARY、PATHS 模块。构建会增加一个自有 C++11 动态适配模块，关闭异常；C99 核心通过私有 C ABI 与它通信。第三方 7-Zip 内部可以使用异常，所调用的原生 COM 接口在返回前处理原生错误。

固定版本的源码位于独立子模块 `deps/7zip`（26.04）。可以在不修改源码、不向源码目录生成产物的情况下构建宿主动态库：

```sh
cmake -S tools/sevenzip -B build-7zip
cmake --build build-7zip --config Release
```

Windows 需要使用 Visual Studio 开发者命令行。产物分别是 `build-7zip/runtime/7z.dll` 和 Linux 上的 `build-7zip/runtime/7z.so`。受上游 makefile 限制，此辅助构建目录不能包含空格。现已支持在 POSIX 宿主／WSL 中使用目标工具链交叉构建 Linux ARM 和 Android；Android 选择对应 NDK ABI/API，在运行库内部静态包含 C++ 运行时，线程符号来自 Bionic libc。操作见[交叉构建](DEPENDENCIES.zh-CN.md#6-linux-arm-与-android-交叉构建)。Apple 验证暂缓。显式选择 FetchContent 时获取相同固定提交。

```c
hx_os_archive_options options = HX_OS_ARCHIVE_OPTIONS_INIT;

options.password = password_from_your_application;
options.seven_zip_library = absolute_trusted_7zip_runtime_path;
options.flags = HX_OS_ARCHIVE_ENCRYPT_NAMES;

hx_os_status status = hx_os_archive_pack_ex(NULL,
	"records", "private.7z", &options, NULL, NULL);
```

只需要内容加密时不设置 ENCRYPT_NAMES。`seven_zip_library` 必须是可信原生动态库的绝对路径。`seven_zip_adapter` 可以显式指定自有适配模块路径；不指定时，从核心库旁边加载，静态核心则从可执行文件旁边加载。两个顶层模块均不通过 PATH 查找，其依赖动态库仍遵守系统加载器规则。安装时适配模块位于 Windows 的 `bin`、POSIX 的 `lib`；静态链接的 POSIX 程序应显式指定路径，或者把适配模块部署在可执行文件旁。

`hx_os_archive_7zip_available()` 只说明桥接代码是否构建，不代表原生 DLL 已经安装。一次同步操作会先释放流与原生 COM 对象，再卸载自身持有的模块引用。私有卸载入口用短锁建立不同调用之间的析构顺序；锁不覆盖编解码、业务回调、注入分配器或诊断分配。运行时不要并发替换或删除这些模块文件。适配器在创建 COM 对象之前检查模块版本和接口布局；与固定的 26.04 不匹配时返回 NOT_SUPPORTED，不能把 p7zip 或 23 之前的 Unix 接口当成相同 ABI。

原生可选后端有独立的 LGPL/BSD/unRAR 条件，与默认宽松许可组合分开。核心安装不会自动复制原生 7-Zip 运行库。分发时保留实际组件的完整上游许可证并履行对应义务；动态加载不会消除这些条件。见[第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)。

## 5. 上限、取消和部分结果

| 默认选项 | 数值／含义 |
| --- | --- |
| `max_archive_bytes` | 压缩包输入文件或输出文件最多 4 GiB |
| `max_content_bytes` | 打包／解压处理的文件内容最多 4 GiB，也拒绝单条声明大小超过该值的记录 |
| `max_decoded_bytes` | 外层 Gzip/XZ/Zstd 解码流最多 8 GiB，包含 TAR 头、填充及跳过的内容 |
| `max_entries` | 最多检查 65,536 个条目 |
| `max_depth` | 最多 64 层路径分量 |
| `max_path_bytes` | UTF-8 路径含结束符最多 65,536 字节 |
| `max_memory_bytes` | 自有工作内存及元数据最多 16 MiB，包含分配记账开销 |
| `workers` | 默认一个编解码工作线程；可请求 1–32 的并行度，原生库还可能使用固定辅助线程 |
| `deadline` | 默认无限；有限值使用本库已有的单调绝对截止时间 |

原生编解码器的字典、solid 块、原生接口持有的字符串、运行库对象和文件系统缓存不属于自有分配器预算。输出上限不是硬内存或 CPU 沙箱。确实需要强隔离的输入应由应用放进自己管理的辅助进程中处理。不承诺硬截止时间，也不强行打断原生代码或文件系统调用。 原生普通 7z 编码还可能使用临时暂存文件，位于自有内存预算之外；源内容上限约束已处理输入，输出上限在数据到达本库输出流时执行。

选项、密码、路径字符串借用到函数返回。进度回调可以返回 `HX_OS_CANCELLED` 取消。默认 libarchive 后端在调用线程执行回调，原生 7-Zip 可能从编解码线程报告进度。进度回调不会重叠，不持有库锁调用，允许合并中间通知；快照只在本次回调期间有效。不能跨 C 边界抛异常或 `longjmp`，交给 UI 线程的消息由应用安排。

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

`entries/files/directories` 是已经检查的记录数；`content_bytes` 是已经处理的源文件或解出内容，单纯列目录时为零。`archive_bytes` 是到达过的最大压缩包文件偏移，不会因为 seek 而重复累计。`published` 表示已经发布的文件数：整个打包文件算一个，解压可能已经完成多个。失败也可能有非零进度。同步函数返回后，不再保留后台工作者或调用者缓冲区。

## 6. 文件系统与失败行为

打包时在输出文件旁创建临时文件，写完并请求文件同步后原子发布。输出父目录必须已存在。默认遇到旧文件返回 EXISTS；显式设置 `HX_OS_ARCHIVE_OVERWRITE` 才允许替换。父目录同步沿用 `file_save_atomic` 的尽力语义，成功不代表能够抵抗任何断电场景。输出压缩包位于源目录内部时会拒绝操作。

解压会按需创建目录，每个普通文件先写到相应父目录下的临时文件，条目检查完成后才发布。`HX_OS_ARCHIVE_SYNC_FILES` 请求文件同步，POSIX 还请求父目录同步；后续同步失败时，已经发布的事实会保留在结果中。Windows 只承诺文件同步请求，不伪装成相同的目录持久化保证。

解压不是整个目录的事务。后面的条目、回调或外层压缩尾部失败时，之前已经完成的文件和新建目录会保留。当前尚未发布的临时文件会尽量删除，清理失败可能留下可恢复的临时文件。因此，不能把整个函数失败理解成目标目录毫无变化。查看目录或跳过条目也不等于验证其内容 CRC 或认证数据。

绝对路径、父目录穿越、盘符／数据流语法、Windows 设备名和非法 UTF-8 会失败；开头的 `./` 会规范化。包内路径按支持平台可共用的规则处理，因此部分 POSIX 合法名字会被明确拒绝。不会恢复符号链接、硬链接、重解析点及特殊文件。只有显式 OVERWRITE 才替换已有普通文件；同名条目默认失败，整体解压开启覆盖时按包内顺序处理。原生 7z 的单文件提取会拒绝重复同名造成的歧义。

遍历使用原生目录句柄／描述符，不改变进程工作目录；目标目录下已经存在的链接会被拒绝。调用者需要保持输入输出目录树稳定，包括不并发修改 Windows 目录的重解析元数据。这不是对任意并发命名空间变化的隔离，也不对输入文件创建快照。

加密文件的明文临时文件要求文件系统可以限制访问。POSIX 检查所有者及权限位，并清除 Darwin 上继承的 ACL；Windows 使用受保护的仅所有者 DACL。不支持这些保护的文件系统会失败，包括不能兑现所需权限位的 WSL Windows 挂载目录。自有临时数据缓冲区在释放时清零，但不承诺擦除所有第三方缓冲、缓存或存储介质内容。默认后端会保留调用前已有的 OpenSSL 错误条目，错误标记不在其公开保存／恢复 API 的保证内。静态 OpenSSL 继续遵守显式线程清理和卸载约定。

POSIX 的 libarchive 元数据转换临时使用当前线程的 UTF-8 locale，依次尝试 `C.UTF-8`、`en_US.UTF-8`；进度／遍历回调和注入分配器执行时恢复调用者原有 locale。并发操作共用一个不可变 locale，每次调用只在取得／释放引用时进入两段短锁，不在编解码或回调期间持锁，最后一个操作结束即释放。不改进程全局 locale。目标环境两个名字都不可用时返回 NOT_SUPPORTED。Windows 原生宽字符串和 7-Zip 接口中的 UTF-16 字符串均由适配层转换。

## 7. C++11 直接调用

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

C++ 层调用相同的 C 实现，返回 `Result<hx_os_archive_result>`。结果携带值时，失败也能查看部分完成计数。`forEach` 接受 `noexcept` 回调，可返回 void、状态码或 `Result<void>`；没有异常转发、隐藏后台队列或另一套 C++ 编解码实现。

## 8. 运行压缩示例

```sh
build/bin/hx_os_archive_files pack input-folder records.zip
build/bin/hx_os_archive_files list records.zip
build/bin/hx_os_archive_files unpack records.zip restored-folder
```

先准备 input-folder，输出归档和 restored-folder 应不存在。Windows 程序位于 build/bin/Release，带 .exe 后缀。详细参数见前文；示例可独立运行，部署要求见[平台支持](PLATFORMS.zh-CN.md)。
