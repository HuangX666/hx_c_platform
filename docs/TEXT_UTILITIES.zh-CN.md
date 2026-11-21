# 借用式文本工具

[English](TEXT_UTILITIES.md) · [字符串工具](COMMON_UTILITIES.zh-CN.md) · [README](../README.zh-CN.md)

在解析命令行参数、切分冒号或逗号分隔的配置字段、校验端口号等数字文本，或者对 `file2` 与 `file10` 这类带编号的名称排序时，往往并不需要为每个子串分配堆内存。本模块提供零堆内存分配的**借用式文本拆分游标**、**严格 64 位整数解析**以及**自然顺序（Natural Sort）比较器**。

你可以先运行 [text_utilities.c](../examples/text_utilities.c) 查看这三项工具的配合使用：程序先用 `hx_os_text_split_init()` 与 `hx_os_text_split_next()` 逐段借用切分后的视图，再用 `hx_os_text_parse_u64()`／`hx_os_text_parse_i64()` 将文本转为整数，最后用 `hx_os_text_compare_natural()` 比较含数字的名称。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，单独构建并运行 `hx_os_text_utilities`（无需命令行参数或输入文件）：

```text
Piece: 'alpha' (5)
Piece: 'beta' (4)
Piece: '' (0)
Piece: 'gamma' (5)
Piece: '' (0)
Port: 443, offset: -42
file2 sorts before file10: yes
```

你也可以按[最小工程配置](USAGE.zh-CN.md#c-project)将该示例直接作为 `main.c` 编译。

## 1. 不复制原文的拆分

`<hx_text_utils.h>` 提供了直接放在栈上的拆分游标 `hx_os_text_split`，每次调用 `hx_os_text_split_next()` 都会返回一个指向原始输入缓冲区的 `hx_string_view` 切片。整个过程不分配任何堆内存，也不修改原始输入文本（若你需要让切分结果各自拥有一份独立的字符串拷贝，请使用 [字符串工具](COMMON_UTILITIES.zh-CN.md) 中的 `hx_string_split()` 或 `hx::Strings::split()`）。

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

上述代码依次输出 `alpha`、`beta`、空段、`gamma` 和末尾空段。请注意，分隔符匹配的是完整的 `"::"` 子串，而不是“任意单个冒号字符”；匹配从左到右不重叠推进（例如用 `"aa"` 拆分 `"aaa"` 会得到一个空段和 `"a"`）。

通过 `keep_empty` 参数可以控制是否保留空片段：

| 输入场景 | `keep_empty = 1`（保留空段） | `keep_empty = 0`（跳过空段） |
| --- | --- | --- |
| 原文为空字符串 `""` | 返回 1 个空段，随后返回 `HX_OS_EOF` | 不返回任何段，直接返回 `HX_OS_EOF` |
| 连续相邻分隔符（如 `a::::b`） | 保留中间的空段 | 自动跳过中间的空段 |
| 末尾以分隔符结束（如 `gamma::`） | 保留末尾的空段 | 自动跳过末尾的空段 |
| 分隔符本身为空字符串 `""` | 初始化直接返回 `HX_OS_INVALID` | 初始化直接返回 `HX_OS_INVALID` |

> [!IMPORTANT]
> **编码校验与借用生命周期契约：**
> - **初始化一次校验 UTF-8**：`hx_os_text_split_init()` 会按给定字节长度一次性校验原文与分隔符是否为合法 UTF-8（支持多字节 UTF-8 分隔符以及按显式长度传入的内嵌 `\0`）；若编码非法则直接返回错误且不改变游标状态。后续调用 `hx_os_text_split_next()` 时仅使用常数级辅助空间做精确子串扫描，不再重复扫描全文校验编码。
> - **迭代返回值**：`hx_os_text_split_next()` 每次成功产出一个片段（含长度为 `0` 的空段）均返回 `HX_OS_OK`；全部遍历完毕后返回 `HX_OS_EOF`（`HX_OS_EOF` 或错误均保持输出视图 `part` 不变）。
> - **保持原文有效**：在游标迭代及使用返回的 `part` 视图期间，原始文本与分隔符的底层内存必须保持有效且不被修改。返回的 `hx_string_view` 是原始内存的切片，**不保证以 `\0` 结尾**（切勿直接传给期望 C 字符串的 `printf("%s")`）。若需长期保存某个片段，请将其复制为独立的 `hx_string`。

## 2. 严格整数转换

C 标准库的 `strtol`／`strtoull` 会默认忽略前导空白、把前导 `0` 当作八进制，且需要配合 `errno` 和尾指针才能判断是否完整解析。`hx_os_text_parse_i64()` 与 `hx_os_text_parse_u64()` 则专门面向配置与协议字段：它们解析完整的有界视图，不分配堆内存，不依赖或修改进程 locale，并要求调用方显式指定进制（`2`、`8`、`10` 或 `16`，前导 `0` 绝不会自动切换进制）。

```c
uint64_t port = 0;
hx_os_status status = hx_os_text_parse_u64(
	hx_string_view_from_cstr("443"), 10, &port, NULL);

if (status != HX_OS_OK)
	fprintf(stderr, "Invalid port: %s\n", hx_os_status_name(status));
```

| 输入文本示例 | 解析行为与结果 |
| --- | --- |
| 十进制 `"+42"` | 有符号（`i64`）与无符号（`u64`）解析均接受，得到 `42` |
| 十进制 `"-42"` | 仅 `hx_os_text_parse_i64()` 接受；无符号解析直接拒绝 |
| 无符号解析 `"-0"` | 明确拒绝（返回 `HX_OS_TYPE_MISMATCH`） |
| 十六进制 `"ff"` 或 `"FF"`（`base = 16`） | 大小写均接受，得到 `255` |
| 二进制 `"101"`（`base = 2`）；八进制 `"377"`（`base = 8`） | 分别得到 `5` 和 `255` |
| `"0xff"`、首尾带空格、`"1_000"`、`"1.0"`、`"1e2"`、`"42ms"` | 全部拒绝（不支持进制前缀、空白、千位分隔符、小数或单位后缀） |
| 空字符串 `""`、单独的正负号 `"+"`／`"-"`、视图长度误含末尾 `\0` | 全部拒绝 |

- **错误码区分与全范围精度**：仅当整个视图完全合法且未溢出时才写入输出变量；任何失败均保持输出变量原值不变。参数或进制参数非法返回 `HX_OS_INVALID`，字符格式不合法返回 `HX_OS_TYPE_MISMATCH`，数值超出目标类型范围返回 `HX_OS_RANGE`（即使中途已检测到数值溢出，解析器仍会继续校验剩余字符的语法以返回准确的错误类别）。底层全程使用纯整数运算，精确覆盖 `INT64_MIN`…`INT64_MAX` 与 `0`…`UINT64_MAX`，不会经过浮点数中转而损失精度。
- **构建开关**：这两个函数受 CMake 选项 `HX_OS_ENABLE_TEXT_NUMBER` 控制（默认开启）；关闭后调用会返回 `HX_OS_NOT_SUPPORTED`，运行时可通过能力位 `HX_OS_CAP_TEXT_NUMBER` 查询。

## 3. 文件名等文本的自然排序

在对文件名、设备名或版本号列表排序时，普通字典序会把 `"file10"` 排在 `"file2"` 前面。`hx_os_text_compare_natural(left, right)` 返回 `-1`、`0` 或 `1`，可直接用于排序比较器：当左右两侧在当前位置同时遇到连续的 ASCII 数字（`0-9`）时，它会按**整段数字的数值大小**进行比较，且内部不将其转换为固定宽度整数，因此无论数字段有多长都不会发生整数溢出。

```text
file0 < file00 < file2 < file2a < file02 < file002 < file10
```

- **前导零决胜规则（Tie-breaker）**：当两段数字的数值大小相等时（例如 `"0"` 与 `"00"`，或 `"2"` 与 `"02"`），**数字字符总长度较短**的一方排在前面，且该规则优先于后续非数字字符的比较。因此，`hx_os_text_compare_natural()` 返回 `0` 意味着两个视图的**完整字节序列完全一致**，而不仅仅是数值相等。
- **非数字字节比较**：非 ASCII 数字部分按无符号字节值逐字节比较。因此具有相同中文 UTF-8 前缀的 `"设备2"` 与 `"设备10"` 会自然地按 `2 < 10` 排序。注意：该函数不执行 locale 排序、Unicode 规范化或大小写折叠，全角数字、正负号和小数点也仅作普通字节处理。调用方需保证传入的视图指针有效（仅当 `size == 0` 时允许 `data == NULL`）。

## 4. C++11 与可运行示例

在 C++11 中，包含 `<hx/TextUtilities.hpp>` 即可使用零额外分配、无异常的封装接口：

- `hx::SplitView::create(input, delimiter, keepEmpty)` 与 `next()`：通过 `hx::Result<hx::StringView>` 逐段返回借用切片；遍历结束可通过 `status()` 检查是否为 `HX_OS_EOF`。默认构造的 `SplitView` 直接处于已结束状态。
- `hx::TextUtilities::parseI64(input, base = 10)` 与 `parseU64(input, base = 10)`：分别返回 `hx::Result<int64_t>` 与 `hx::Result<uint64_t>`。
- `hx::TextUtilities::compareNatural(left, right)`：返回 `-1`、`0` 或 `1`。

文本拆分与自然排序属于始终编译的核心字符串功能，仅严格整数转换受 `HX_OS_ENABLE_TEXT_NUMBER` 开关控制。完整示例请参考 [C 示例](../examples/text_utilities.c)。
