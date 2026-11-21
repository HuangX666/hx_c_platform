# 原生宽字符串与 UTF-8 路径的标准文件操作

[English](TEXT_INTEROP.md) · [README](../README.zh-CN.md)

在跨平台 C/C++ 开发中，应用内部统一使用 UTF-8 最为省心，但总会遇到两类平台边界问题：一是第三方库强制要求传入标准 C 的 `FILE *`，而在 Windows 上直接把含中文的 UTF-8 路径传给 `fopen()` 会按本地 ANSI 代码页解析导致乱码或打开失败；二是操作系统原生 API 返回了 `wchar_t *` 宽字符串，需要安全地与 `hx_string` 互转。本模块专门提供这两类互操作辅助函数以及与区域设置无关的 ASCII 忽略大小写比较。

> [!TIP]
> 如果你只是想直接读写整个 UTF-8 文本文件，无需手动绕道 `FILE *` 或 `wchar_t`，直接使用 `hx_os_file_read_text()`／`hx::Files::readText()` 等文件便利接口即可。

下面第 1 节与第 2 节各提供了一份完整的 C99 程序，你可以任选一份保存为 `main.c`，并按[最小 C 工程](USAGE.zh-CN.md#c-project)直接编译运行：
- **第 1 份程序**：在当前工作目录下创建或覆盖名为 `message-中文.txt` 的文件，写入 `Hello, 中文!` 加换行符，成功时进程退出码为 `0`。
- **第 2 份程序**：将原生 `wchar_t` 宽字符串 `L"中文😀"` 转换为 UTF-8 并打印到终端（在支持 UTF-8 的终端上会输出 `中文😀`）。

## 1. 用 UTF-8 文件名打开标准 FILE

当第三方库接口要求传入 `FILE *` 时，使用 `<hx_os_stdio.h>` 中的 `hx_os_fopen_utf8()` 可以在所有平台上用 UTF-8 文件名打开标准文件流，最后用 `hx_os_fclose()` 关闭。注意：`hx_os_fopen_utf8()` 仅负责把**文件名路径**转换为宿主系统要求的形式（如 Windows UTF-16 及长路径格式），不会对文件内部读写的数据字节做编码转换。

```c
#include <hx_os_stdio.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	const char *message = "Hello, 中文!\n";
	hx_os_error error;
	FILE *file = hx_os_fopen_utf8("message-中文.txt", "wb", &error);
	if (!file) {
		fprintf(stderr, "%s\n", hx_os_status_name(error.status));
		return 1;
	}

	size_t written = fwrite(message, 1, strlen(message), file);
	hx_os_status closed = hx_os_fclose(&file, &error);

	return written == strlen(message) && closed == HX_OS_OK ? 0 : 1;
}
```

- **受支持的模式字符串**：必须以 `r`、`w` 或 `a` 开头，后接可选的 `b` 和 `+`（两者顺序不限，即支持 `rb`、`wb`、`ab`、`r+b`、`rb+` 等标准写法）。若包含重复标志或特定编译器的非标准 CRT 模式扩展，函数会在调用底层 CRT 前直接拒绝并置状态为 `HX_OS_INVALID`。在 Windows 上建议显式加上 `b` 以保持原始换行字节不被转换。
- **关闭与 CRT 边界契约**：`hx_os_fclose(&file, &error)` 会通过打开该文件的运行库关闭流（同时冲刷 CRT 用户态缓冲，但不等同于调用 `fsync` 落盘持久化），且无论关闭是否成功都会将 `file` 指针置为 `NULL`。

> [!WARNING]
> **Windows 动态库（DLL）的 CRT 一致性要求：**
> 当你的代码拿到 `FILE *` 并直接调用 `fread()`、`fwrite()` 等标准库函数时，调用方必须与 `hx_c_platform` 共享同一个 C 运行库（CRT）实例。特别是在 Windows 上，如果两个模块分别静态链接了各自独立的 CRT（`/MT`），切勿跨模块边界直接传递 `FILE *`；跨独立 CRT 边界时请改用不透明句柄 `hx_os_file` 系列接口。

## 2. 把原生宽字符串直接写入 hx_string

当系统原生接口返回 `wchar_t *` 时，可以使用 `hx_string_assign_wide()` 将其严格转换为 UTF-8 并存入 `hx_string`：

```c
#include <hx_string.h>
#include <wchar.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;
	const wchar_t *native = L"中文😀";

	hx_os_status status = hx_string_assign_wide(
		&text, native, wcslen(native), NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));

	hx_string_destroy(&text);

	return status == HX_OS_OK ? 0 : 1;
}
```

- **注意 `units` 参数的单位**：`units` 表示 **`wchar_t` 元素的个数**，既不是字节数，也不是 Unicode 码点数。在 Windows 上，`wchar_t` 为 16 位 UTF-16 编码单元，像 `😀` 这样的增补平面字符会占用 **2 个** `wchar_t` 单元；而在受支持的 POSIX 平台上，`wchar_t` 为 32 位 UTF-32 单元。
- **严格校验与失败保护**：转换过程完全独立于进程的全局 locale。遇到孤立代理项（Unpaired Surrogates）或越界码点时会明确返回 `HX_OS_INVALID`，并保持目标 `hx_string` 原有内容不变。
- **底层缓冲区互转接口**：`hx_os_text_to_wide()` 与 `hx_os_text_from_wide()` 接受显式元素长度，并原样保留内嵌的 `\0` 字符。其 `capacity` 与输出的 `required` 长度均以**目标元素类型**（`wchar_t` 或 `char`）为单位，且**包含末尾终止符**。传入 `output = NULL, capacity = 0` 可预先测量所需元素个数（此时返回 `HX_OS_OUTPUT_FULL` 并填写 `required`）。输入、输出与 `required` 变量所在的内存区域不得重叠。

## 3. 不使用异常的 C++11 便利入口

在 C++11 中，包含 `<hx/Text.hpp>` 即可使用 `hx::Text::fromWide()` 与 `hx::Text::toWide()`，两者均返回 `hx::Result` 且不依赖 C++ 异常：

```cpp
hx::Result<hx::WideString> make_native_title() noexcept
{
	auto text = hx::Text::fromWide(L"中文😀");
	if (!text)
		return hx::Failure(text.error());

	return hx::Text::toWide(text.value());
}
```

- 先检查 `if (result)`，再在结果对象存活期间访问 `result.value().c_str()`。
- `hx::WideString::size()` 返回不含末尾终止符的原生 `wchar_t` 单元数。
- 若宽字符串包含内嵌 `L'\0'`，可使用 `hx::Text::fromWideUnits(pointer, units, context)` 按显式长度转换；`hx::Text::toWide()` 也支持传入自定义的 `hx_context` 内存上下文。

## 4. ASCII 忽略大小写比较和查找

当协议字段、HTTP 头或配置键名只需要按 ASCII 规则（`A-Z` 映射为 `a-z`）忽略大小写比较或搜索时，可以使用：

- **C 接口**：`hx_string_compare_ascii_nocase()`（返回 `-1`、`0` 或 `1`）与 `hx_string_find_ascii_nocase()`（返回匹配的字节偏移，未找到返回 `HX_STRING_NOT_FOUND`）。
- **C++11 接口**：`hx::String::compareAsciiNoCase()` 与 `hx::String::findAsciiNoCase()`。

这两个操作零堆内存分配、保留内嵌 `\0`、且不受系统 locale 影响。它们仅折叠 ASCII 范围内的 `A-Z` 与 `a-z`，对所有非 ASCII 的 UTF-8 字节保持精确比较（因此不会把德语 `ß` 与 `ss` 视为相等，也不做 Unicode 规范化；若需要完整 Unicode 大小写折叠，请使用 [Unicode 模块](UNICODE.zh-CN.md)）。查找接口遵循统一的 `find` 契约：若搜索词本身不是合法 UTF-8，或者传入的起始偏移落在了多字节 UTF-8 字符的中间字节上，将直接返回 `HX_STRING_NOT_FOUND`。
