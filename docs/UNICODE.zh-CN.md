# Unicode 规范化与大小写折叠

[English](UNICODE.md) · [README](../README.zh-CN.md)

在处理跨平台文件名、用户搜索词或国际化标识符时，同一个视觉字符在 UTF-8 中可能存在多种编码形式（例如预组合字符 `é` 与 `e + 组合重音符`），不同语言的大小写转换规则也远不止 ASCII 的 `A-Z`（例如德语 `ß` 在大小写折叠后会变成两个字母 `ss`）。如果直接做字节级 `strcmp` 或按字节转小写，往往会导致漏匹配。本模块基于固定版本的 `utf8proc` 提供标准 Unicode 规范化（NFC／NFD／NFKC／NFKD）与语言环境无关的大小写折叠（Case Folding）。

在实际业务中，推荐**保留原始展示文本不变，仅对用于比较或索引的副本执行转换**。你可以先运行 [unicode_tools.c](../examples/unicode_tools.c) 体验这一过程：程序将 UTF-8 字符串 `"Straße"` 传入 `hx_os_unicode_casefold()`，把折叠结果写入 `hx_string` 并打印。确保构建时启用了 `HX_OS_ENABLE_UNICODE` 及对应的 `utf8proc` SDK，然后按[示例运行方法](../examples/README.zh-CN.md#run)编译并运行 `hx_os_unicode_tools`（无需命令行参数或输入文件）：

```text
strasse
```

可以看到，单个字符 `ß` 折叠后展开成了两个字母 `ss`，因此转换前后的字节长度与字符数都可能发生变化。若通过[安装包接入](USAGE.zh-CN.md#installed-project)使用本功能，请确保安装包同样保留了对应的 SDK 模块。

## 1. 选择转换

C 语言接口包含在 `<hx_os.h>` 中，核心函数有两个：

- `hx_os_unicode_normalize(input, form, out, error)`：按指定的规范化形式（`HX_OS_UNICODE_NFC`、`HX_OS_UNICODE_NFD`、`HX_OS_UNICODE_NFKC` 或 `HX_OS_UNICODE_NFKD`）转换输入文本。
- `hx_os_unicode_casefold(input, out, error)`：执行与语言环境（locale）无关的 Unicode 大小写折叠，并将结果规范化为 NFC 形式输出。

输出参数 `out` 必须是已初始化的 `hx_string`（例如 `HX_STRING_INIT`）。函数会使用 `out` 自身绑定的分配器申请内存；若转换失败，`out` 保持原有内容不变。输入视图 `input` 允许借用 `out` 当前持有的缓冲区（支持安全的自引用更新）。

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string result = HX_STRING_INIT;

	hx_os_status status = hx_os_unicode_casefold(
		hx_string_view_from_cstr("Straße"), &result, NULL);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&result));

	hx_string_destroy(&result);
	return status == HX_OS_OK ? 0 : 1;
}
```

在 C++11 中，包含 `<hx/Unicode.hpp>`（或 `<hx/Os.hpp>`）即可调用 `hx::Unicode::normalize()` 与 `hx::Unicode::casefold()`，两者均返回 `hx::Result<hx::String>`。完整用法见 [unicode_tools.c](../examples/unicode_tools.c)。

## 2. 含义与边界

不同规范化形式与大小写折叠的适用场景如下：

- **NFC（规范分解后再规范组合）**：将等价的组合序列合并为标准预组合字符，适合作为文本存储、网络传输和常规相等性比较的统一格式。
- **NFD（规范分解）**：将预组合字符拆分为基础字符与组合附加符号，适合需要单独检查或剥离重音符号的场景。
- **NFKC／NFKD（兼容分解与组合）**：除规范等价外，还会将全角字母数字、上标数字、连字（如 `ﬁ` → `fi`）等兼容字符统一替换为标准字符，适合全文搜索或宽松匹配。
- **大小写折叠（Case Folding）**：旨在消除大小写差异以供不区分大小写的匹配使用。它与系统的全局 locale 无关，但可能改变字符串的字节数和码点数；它既不是按语言习惯排序（Collation），也不处理土耳其语点号 `I`/`ı` 等特定语言专属的大小写规则。

> [!WARNING]
> **切勿盲目自动转换密码与精确业务 ID：**
> 本模块不会在底层自动改写文件路径、用户密码或业务主键。对于密码哈希、区分大小写的令牌（Token）或要求字节级精确匹配的业务 ID，应明确保留原始字节或仅按协议要求选用 NFC，切勿自动套用 `NFKC` 或 `casefold`。

**输入上限与模块裁剪：**
- **严格 UTF-8 与容量限制**：输入必须是合法的 UTF-8 字节序列（支持按显式长度包含内嵌 `\0`），单次输入上限为 **1 MiB**；转换过程中的暂存区与最终输出上限为 **16 MiB**。超出上限返回 `HX_OS_LIMIT`，非法 UTF-8 返回 `HX_OS_INVALID`。
- **无全局状态污染**：转换过程绝不修改进程 locale，也不替换 `utf8proc` 的全局分配器，所有临时缓冲区均通过输出 `hx_string` 绑定的内存上下文管理。
- **可选裁剪与原生访问**：CMake 选项 `HX_OS_ENABLE_UNICODE` 可单独关闭；关闭后调用上述函数会明确返回 `HX_OS_NOT_SUPPORTED`。如果业务需要直接调用 `utf8proc` 的底层字符属性查询接口，可通过[底层依赖目标](NATIVE_DEPENDENCIES.zh-CN.md)直接链接并调用原生 `utf8proc` API。
