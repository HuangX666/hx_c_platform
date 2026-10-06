# 原生宽字符串与 UTF-8 路径的标准文件操作

[English](TEXT_INTEROP.md) · [README](../README.zh-CN.md)

当已有原生接口需要 `wchar_t`，或者第三方接口需要标准 `FILE *` 时，使用这些辅助函数。普通整文件文本读取仍使用 `hx_os_file_read_text()` / `hx::Files::readText()`。

## 1. 用 UTF-8 文件名打开标准 FILE

下面是完整 C99 程序，写入一段 UTF-8 文本，不转换换行符。路径和模式仅在打开期间借用。打开函数转换的是文件名，不会转换文件内容。

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

模式以 `r`、`w` 或 `a` 开头，后面可选 `b` 和 `+`，两者顺序不限，例如 `rb`、`wb`、`ab`、`r+b` 或 `rb+`。重复标记和运行库专用扩展在调用 CRT 前返回 INVALID。Windows 上使用 `b` 保持原始字节。这些同步操作可能阻塞；`fclose` 还会冲刷 CRT 的用户态缓冲，但这不等于持久化保证。

`hx_os_fclose()` 通过打开文件的运行库关闭流，关闭失败也清空指针。调用者直接使用 `fread`、`fwrite` 等标准接口时，双方需要使用兼容的共享 CRT。尤其在 Windows 上，分别静态链接各自 CRT 的 DLL 不能安全地交换这些流；这种边界应使用不透明的 `hx_os_file` 接口。Windows 路径复用现有文件接口的 UTF-8 校验及长路径转换。

## 2. 把原生宽字符串直接写入 hx_string

下面是完整的 C99 程序，创建、使用并释放输出字符串：

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

`units` 表示 `wchar_t` 元素数量，不是字节数，也不是 Unicode 码点数。Windows 上补充平面字符占两个 UTF-16 单元；已适配的 POSIX 目标使用 UTF-32 原生宽字符单元。转换严格且不依赖进程区域设置，孤立代理项和越界码点会报错，失败保留原有 `hx_string`。

缓冲区入口 `hx_os_text_to_wide()`、`hx_os_text_from_wide()` 接受显式长度并保留嵌入的 NUL。容量与所需大小均以目标元素为单位，包含末尾 NUL。输出为 NULL 且容量为零时测量长度并返回 OUTPUT_FULL。输入无效或容量不足时不修改输出缓冲区。输入、输出以及保存所需大小的位置不得重叠。

## 3. 不使用异常的 C++11 便利入口

包含 `<hx/Text.hpp>`。下面的函数先把原生字符串转换成 UTF-8，再转换回来；返回的宽字符串自己持有内存：

```cpp
hx::Result<hx::WideString> make_native_title() noexcept
{
	auto text = hx::Text::fromWide(L"中文😀");
	if (!text)
		return hx::Failure(text.error());

	return hx::Text::toWide(text.value());
}
```

检查结果后，在结果对象仍然存活时使用 `result.value().c_str()`。`WideString::size()` 不含结束符，以原生宽字符单元计数。`fromWideUnits(pointer, units, context)` 通过显式长度保留嵌入的 NUL；`toWide()` 也可指定持有结果的 context。自有接口均使用 `Result`，支持关闭异常的编译配置。

## 4. ASCII 忽略大小写比较和查找

`hx_string_compare_ascii_nocase()` 返回 -1、0 或 1；`hx_string_find_ascii_nocase()` 返回字节偏移或 `HX_STRING_NOT_FOUND`。C++ `String` 对应方法是 `compareAsciiNoCase()` 和 `findAsciiNoCase()`。它们不分配内存，保留嵌入的 NUL，也不依赖进程区域设置。仅把 A-Z 按 a-z 比较，其他 UTF-8 字符保持精确匹配，因此不会把 `ß` 与 `ss` 视为相同，也不做 Unicode 规范化。查找沿用现有 find 契约：无效查找串或落在 UTF-8 码点内部的起始位置返回 NOT_FOUND。
