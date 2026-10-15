# 借用式文本工具

[English](TEXT_UTILITIES.md) · [字符串工具](COMMON_UTILITIES.zh-CN.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 处理分隔字段、端口文字和 file2/file10 这样的名称时，使用文本拆分、严格数字转换和自然比较。

打开 [text_utilities.c](../examples/text_utilities.c)。split_init／next 逐段借用输入 → parse_u64／i64 转成整数 → compare_natural 比较含数字的名称。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_text_utilities`。此例无命令行参数、无需输入文件。

**会得到什么：**能看到 alpha、beta、空段、gamma、空段；随后输出端口 443、偏移 -42，以及 file2 排在 file10 前。保存拆分结果供以后使用时，需要复制原文或保证原文仍在。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

## 1. 不复制原文的拆分

`<hx_text_utils.h>` 提供放在栈上的游标，每次返回一个指向现有输入的 `hx_string_view`。它不分配内存，也不修改输入。如果结果需要持有一份自己的原文副本，继续使用已有的 `hx_string_split()` / `hx::Strings::split()`。

```c
hx_os_text_split split;
hx_string_view part;
hx_os_status status = hx_os_text_split_init(&split,
	hx_string_view_from_cstr("alpha::beta::::gamma::"),
	hx_string_view_from_cstr("::"), 1, NULL);

if (status == HX_OS_OK) {
	while ((status = hx_os_text_split_next(&split, &part, NULL)) ==
		HX_OS_OK) {
		if (part.size)
			(void)fwrite(part.data, 1, part.size, stdout);
		putchar('\n');
	}
}

if (status != HX_OS_EOF)
	fprintf(stderr, "Split failed: %s\n", hx_os_status_name(status));
```

结果依次为 `alpha`、`beta`、空段、`gamma`、末尾空段。分隔符是完整的 `::` 字节序列，不是“任意一个冒号”。从左到右匹配，匹配之间不重叠；例如用 `aa` 拆分 `aaa`，得到空段和 `a`。

| 情况 | `keep_empty=1` | `keep_empty=0` |
| --- | --- | --- |
| 原文为空 | 返回一个空段 | 不返回任何段 |
| 连续分隔符 | 保留中间空段 | 跳过空段 |
| 末尾分隔符 | 保留最后的空段 | 跳过空段 |
| 分隔符为空 | `HX_OS_INVALID` | `HX_OS_INVALID` |

初始化时按给定长度完整校验原文及分隔符的 UTF-8 编码，仅执行一次；编码错误时保留游标原状态。后续 `next()` 不会反复扫描全文来校验编码。支持多字节 UTF-8 分隔符和内嵌 NUL；包含 NUL 时应显式提供长度。匹配器使用固定辅助空间查找完整分隔符，不处理正则表达式或语言 token。

`next()` 返回 `HX_OS_OK` 表示得到一段，空段也算成功；返回 `HX_OS_EOF` 表示已经结束。EOF 或失败不改变输出。接口不设置固定的分段数量上限，调用者决定在有界输入中前进多少次。初始化与迭代都是同步操作，耗时与所扫描的数据相关。

迭代期间原文及分隔符必须始终有效、保持不变。结果借用原文，不保证以 NUL 结尾。复制游标只复制当前位置，副本仍然借用同一份文本。游标和输出对象不能放在借用文本中，输出视图也不能与游标重叠。同一个游标需要串行访问；不同游标副本可以并发读取共享的只读原文。

## 2. 严格整数转换

`hx_os_text_parse_i64()`、`hx_os_text_parse_u64()` 解析完整的有界 token，不分配内存、不改变 locale。显式指定 `2`、`8`、`10` 或 `16` 进制；前导零不会自动切换进制。

```c
uint64_t port = 0;
hx_os_status status = hx_os_text_parse_u64(
	hx_string_view_from_cstr("443"), 10, &port, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Invalid port: %s\n", hx_os_status_name(status));
```

| 输入 | 行为 |
| --- | --- |
| 十进制 `+42` | 有符号、无符号解析都接受 |
| 十进制 `-42` | 只有有符号解析接受 |
| 无符号解析 `-0` | 拒绝 |
| 十六进制 `ff` / `FF` | 都得到 255 |
| 二进制 `101`；八进制 `377` | 分别得到 5、255 |
| `0xff`、两侧空白、`1_000`、`1.0`、`1e2`、`42ms` | 拒绝，不支持前缀、分隔符、小数或后缀 |
| 空文本、单独正负号、把末尾 NUL 计入长度 | 拒绝 |

成功时才写入完整结果；任何失败都保留输出原值。参数或进制不合法返回 `HX_OS_INVALID`，文本格式不符返回 `HX_OS_TYPE_MISMATCH`，数值溢出返回 `HX_OS_RANGE`。即使检测到数值溢出，仍会检查余下 token 的语法。使用整数运算准确覆盖 `INT64_MIN`…`INT64_MAX` 和 `0`…`UINT64_MAX`，不会先转浮点数再转回来而丢失精度。

这两个接口受 `HX_OS_ENABLE_TEXT_NUMBER` 控制，关闭后返回 `HX_OS_NOT_SUPPORTED`。需要时可查询已有的 `HX_OS_CAP_TEXT_NUMBER` 能力位。既有浮点转换保留其面向配置文件的语法；这次严格整数解析不会改变它。

## 3. 文件名等文本的自然排序

`hx_os_text_compare_natural(left, right)` 返回 `-1`、`0` 或 `1`，可以放入排序比较器。当两边当前位置都是 ASCII 数字时，比较整段数字的数值大小，不把它转换成固定宽度整数，因此再长的数字段也不会发生整数溢出。

```text
file0 < file00 < file2 < file2a < file02 < file002 < file10
```

数字段数值相同时，完整数字段较短的排在前面。这个规则先于后续文本生效，因此前导零的顺序固定、可预测。比较相等意味着整段字节完全相同，不只是数字的数值相同。

其他内容按无符号字节比较。支持 UTF-8 文本，但不提供 locale 排序、Unicode 规范化或大小写折叠。例如 `设备2`、`设备10` 的中文字节前缀相同，之后按 ASCII 数字段排序。非 ASCII 数字、正负号和小数分隔符没有特殊数值含义。比较器不校验编码；调用者必须提供有效字节范围，只有长度为零时才允许 NULL。整个比较不分配内存，也不改变进程状态。

## 4. C++11 与可运行示例

`<hx/TextUtilities.hpp>` 提供：

- `hx::SplitView::create(input, delimiter, keepEmpty)`、`next()`：通过 `Result` 返回借用式 `hx::StringView`；结束状态可以通过 `status()` 判断。
- `hx::TextUtilities::parseI64(input, base=10)`、`parseU64()`：分别返回 `Result<int64_t>`、`Result<uint64_t>`。
- `hx::TextUtilities::compareNatural(left, right)`：返回普通整数比较结果，要求输入视图有效。

包装层不增加分配，不使用异常。默认构造的 `SplitView` 已经处于结束状态。游标副本有各自独立的位置，但不会延长原文存储的生命周期。

开启 examples 和数值文本转换后，构建 `hx_os_text_utilities`，运行配置对应 `bin` 目录中的程序。完整 [C 示例](../examples/text_utilities.c) 输出各段长度、`Port: 443, offset: -42`、`file2 sorts before file10: yes`。拆分和自然比较属于始终可用的字符串核心，只有数值转换受上述功能开关控制。
