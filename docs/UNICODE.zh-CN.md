# Unicode 规范化与大小写折叠

[English](UNICODE.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 需要按统一 Unicode 大小写规则匹配用户输入时，先转换比较用的副本，再比较；展示文字可以保留原样。

打开 [unicode_tools.c](../examples/unicode_tools.c)。提供 UTF-8 视图 Straße → unicode_casefold 写入 hx_string → 使用结果 → destroy。需要 UNICODE 和对应 utf8proc SDK。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_unicode_tools`。此例无命令行参数、无需输入文件。

**会得到什么：**输出 `strasse`。它展示 ß 可以变成两个字母 ss，长度不一定保持不变。不要自动把这一步应用于密码或要求精确区分的业务 ID。

下面的短片段用于解释接口；复制运行时以完整示例为准。使用[安装包接入](USAGE.zh-CN.md#installed-project)时，也必须保留对应 SDK 模块。

## 1. 选择转换

hx_os_unicode_normalize(input, form, out, error) 支持 NFC、NFD、NFKC、NFKD；hx_os_unicode_casefold(input, out, error) 做 Unicode 大小写折叠，并输出 NFC。out 是已初始化的 hx_string，使用自己的分配器；失败保留原值，input 可以借用 out。

```c
hx_string result = HX_STRING_INIT;

hx_os_status status = hx_os_unicode_casefold(
	hx_string_view_from_cstr("Straße"), &result, NULL);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&result));

hx_string_destroy(&result);
```

包含 hx_os.h、stdio.h，结果为 strasse。C++11 使用 hx::Unicode::normalize／casefold，返回 Result<String>。完整程序：[unicode_tools.c](../examples/unicode_tools.c)。

## 2. 含义与边界

NFC 合并规范等价字符，NFD 分解它们；NFKC／NFKD 还会转换兼容字符。大小写折叠与 locale 无关，可能改变文本长度；它不是语言排序，也不是土耳其语等语言的特定大小写规则。

输入是严格 UTF-8，可以按长度包含 NUL，最多 1 MiB；转换暂存和输出限制为 16 MiB。不会修改全局 locale，不会自动规范化路径、密码或业务 ID，也不启动后台工作。业务按字段显式选择转换方式。

UNICODE 使用固定版本的 utf8proc SDK，可以关闭；关闭后明确返回 NOT_SUPPORTED。实现用输出 context 管理暂存，不替换 utf8proc 全局分配器。需要原生能力时，可通过[底层依赖目标](NATIVE_DEPENDENCIES.zh-CN.md)直接调用 utf8proc。
