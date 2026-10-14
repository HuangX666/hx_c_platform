# UTF-8 正则表达式

[English](REGEX.md) · [README](../README.zh-CN.md)

## 1. 检查整个字符串

把下面的完整程序保存为 `main.c`，按[最小项目](USAGE.zh-CN.md#c-project)构建。它检查设备编号 `AB1234`：`[A-Z]{2}` 要求两个大写英文字母，`[0-9]{4}` 要求四个数字。正常输出 `Valid`；改为 `A1234` 会输出 `Invalid`。

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	int matched = 0;
	hx_os_status status = hx_os_regex_matches(
		"[A-Z]{2}[0-9]{4}", hx_string_view_from_cstr("AB1234"),
		&matched, NULL);

	if (status != HX_OS_OK) {
		fprintf(stderr, "%s\n", hx_os_status_name(status));
		return 1;
	}

	puts(matched ? "Valid" : "Invalid");
	return 0;
}
```

这个入口负责编译、匹配和释放表达式。正常不匹配返回 OK，并将 matched 设为 0；表达式写错或匹配未完成则返回明确错误。反复使用同一个表达式时，先编译一次。

C++11 对应 `hx::Regex::matches(pattern, text)`，检查 Result 即可。自有实现不使用 try／catch／throw，也不依赖 std::regex。

## 2. 提取字段和替换

完整的 [C 示例](../examples/regex.c) 从 `device=Door_01 request=42` 提取设备和请求编号，再替换成 `Door_01: #42`。[C++ 示例](../examples/regex_cpp.cpp) 使用命名捕获和反向引用查找重复单词。

```sh
cmake --build build --target hx_os_regex hx_os_regex_cpp
build/bin/hx_os_regex
build/bin/hx_os_regex_cpp
```

在库源码目录执行，并把 `build` 换成自己的已有构建目录。该目录需已按[依赖指南](DEPENDENCIES.zh-CN.md)配置 SDK，并开启 `HX_OS_BUILD_EXAMPLES=ON`。Windows 添加 `--config Release`，程序在 `bin/Release/`。C 示例输出：

```text
Device: Door_01
Request: 42
Door_01: #42
```

compile 创建不可变的表达式对象，可由多个线程共享调用。find 返回借用原始文本的字节位置，文本必须保持有效且不被修改。第 0 组表示整个匹配；没有参与匹配的组，其 offset 为 SIZE_MAX。capture_index 把组名转换成下标。所有调用结束后再销毁表达式。

C++ 便利调用：

```cpp
auto regex = hx::Regex::compile("(?<word>\\w+)\\s+\\k<word>");
if (!regex.hasValue())
	return 1;

auto match = regex.value().find("hello hello");
if (!match.hasValue())
	return 1;

auto word = match.value().group(1);
if (!word.hasValue())
	return 1;

std::printf("%.*s\n", (int)word.value().size(), word.value().data());
```

包含 `<hx/Regex.hpp>`、`<cstdio>`，将片段放进 main。group 返回借用文本的视图；replace 返回独立持有的 hx::String；split 在当前调用线程同步交付片段。

## 3. 支持的语法

匹配引擎由本项目用 C99 实现，采用有预算的回溯虚拟机，运行时递归／调用状态放在显式结构中。PCRE2 仅作为本地对照工具，不是生产运行依赖。

| 能力 | 示例 |
| --- | --- |
| 普通／引用文本、字符范围、否定／POSIX 字符类 | abc、\\Q[a]+\\E、[a-z]、[^0-9]、[[:alpha:]] |
| 重复、分支、贪婪／非贪婪 | *、+、?、{2,4}、{2,}、a\|ab、*?、+? |
| 捕获、非捕获、命名捕获 | (abc)、(?:abc)、(?<name>abc)、(?P<name>abc) |
| 数字／命名反向引用 | \\1、\\g{1}、\\k<name>、(?P=name) |
| 正／负前向断言，有界后向断言 | (?=abc)、(?!abc)、(?<=a{1,3})、(?<!abc) |
| 原子组、占有量词 | (?>a*)、a*+、a{2,4}+ |
| 整个表达式递归、组子程序 | (?R)、(?0)、(?1)、(?-1)、(?+1)、(?&name) |
| 捕获／递归条件、断言条件、定义组 | (?(1)a\|b)、(?(name)a\|b)、(?(R)a\|b)、(?(?=a)a\|b)、(?(DEFINE)...) |
| 流程控制与标记 | (*FAIL)、(*F)、(*ACCEPT)、(*COMMIT)、(*PRUNE)、(*THEN)、(*SKIP)、(*MARK:name)、(*SKIP:name) |
| 起止、词边界、重设匹配起点 | ^、$、\\A、\\z、\\Z、\\G、\\b、\\B、\\K |
| 全局／局部选项 | (?i)、(?im-s:...)、(?x)、(?U)、(?n) |
| Unicode 类别与字素簇 | \\p{L}、\\p{Nd}、\\P{Lu}、\\X；\\R 匹配换行序列 |

输入严格使用 UTF-8，按长度处理，可以包含 NUL；位置和长度的单位都是字节。默认换行规则为 LF；\\d／\\w／\\s 默认按 ASCII 判断，UCP 开启后使用 Unicode 属性。忽略大小写、UCP、Unicode 属性和字素簇使用所选 utf8proc。规范化需要显式调用，正则不自动改变文本。

忽略大小写采用 Unicode 默认简单折叠（C／S 记录），不按系统语言环境转小写，也不把一个字符扩成多个字符。例如 `(?i)[Z-a]` 能匹配 `A` 和 `z`，但 `(?i)i` 不匹配 `İ`。区间内字符的折叠结果在编译时收集，匹配时不再用两个端点的折叠结果代替整个区间。固定的 utf8proc 2.12 配合 [Unicode 17 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt) 中三项仅简单折叠的补充映射使用。

这里不声明与 PCRE2 完全兼容。Unicode script／script-extension 和 binary-property 表、分支重置组、重复组名、原生 callout、JIT、其他换行约定以及 PCRE2 的全部替换表达式语法，不在当前方言中。尚未支持的语法会明确拒绝，不会偷偷当成普通字符；无界或过长的后向断言也会拒绝。直接导入外部表达式前，先核对本表与公共头中的契约。

## 4. 上限与错误

默认编译上限：表达式 32 KiB、指令及字符类／区间数据合计 4 MiB、嵌套 64、计数重复 1000、后向断言 255 个码点、最多 64 个捕获组。options 可把计数重复提高至 65,535，后向断言提高至 1024。解析暂存、容量余量和分配器头额外计算。

编译过程另有固定的 4,194,304 个工作单位上限，覆盖语法树构造、忽略大小写的区间展开、长度分析和指令生成。超限返回 LIMIT，不返回半成品对象。捕获组长度会缓存，多次引用子程序不会让后向断言的长度分析指数增长。执行时的 max_steps 不改变这个编译上限。

默认执行上限：1000 万次 VM 步骤、保留的回溯存储 8 MiB、输入／输出文本 16 MiB、递归／作用域深度 64、最多 65,536 个结果。存储增长时可能暂时同时持有旧、新内存，当前捕获与调用快照还占用额外的有界空间。简单字符／字符类／点的重复使用紧凑状态，不为每个字符分配一帧。

```c
hx_os_regex_limits limits = HX_OS_REGEX_LIMITS_INIT;
limits.max_steps = 50000000;
limits.deadline = hx_os_deadline_after_ms(200);

hx_os_status status = hx_os_regex_find_with_limits(
	regex, text, 0, &limits, &match, &error);
```

普通调用使用默认值。一次 find／replace／split 共用整次预算，包括尝试不同起始位置。截止时间与取消按协作式检查，不承诺硬实时。提高预算后重试，会重新开始计算。

| 返回值 | 含义 |
| --- | --- |
| OK | 已完成；find 得到匹配，full_match 得到真／假 |
| NOT_FOUND | find 已完成且没有匹配；输出不变 |
| SYNTAX／NOT_SUPPORTED | 语法错误或方言／Unicode 能力不支持 |
| LIMIT | 步数、深度、输入、结果数或内存超限；不能视为不匹配 |
| TIMEOUT／CANCELLED | 在得出结论前停止 |
| NO_MEMORY | 分配失败 |

hx_os_error_message 提供英文原因和字节位置。find／full_match／replace 失败保留原输出；库不终止进程，也不强杀业务线程。

## 5. 替换与切分

替换支持数字形式的美元引用、美元花括号组名，以及两个美元字符表示字面美元。结果使用 out 的分配器，成功后才整体替换 out。没有参与匹配的组替换为空字符串；输入和替换模板可以借用旧 out，直到最终发布。

全局空匹配后前进一个 Unicode 码点。这是本库明确定义的便利行为，尚未实现 PCRE2 全部全局替换重试规则。split 保留空片段，不把分隔符的捕获组加入片段；失败时仍报告已成功交付数量。回调不能销毁表达式、修改输入或通过异常／longjmp 离开。

REGEX 可单独裁剪。基本 UTF-8 精确匹配不需要外部正则库，Unicode 增强模式需要 UNICODE。 平台边界见[平台支持](PLATFORMS.zh-CN.md)。


递归条件包含 `R`、`R0/Rn`、`R&name`，与 PCRE2 一样在对应位置优先解析已存在的捕获名。`(*ACCEPT)` 会结束当前活动捕获，并接受所在的子程序／断言或完整匹配，普通原子组不是局部接受边界。控制标签最多 255 个 UTF-8 字节、64 个不同标签；全局前缀指令必须在模式主体之前。断言内的 `\K` 与 PCRE2 默认规则一样拒绝。忽略大小写的 POSIX 类保留 ASCII 模式与 UCP 模式的差异。
