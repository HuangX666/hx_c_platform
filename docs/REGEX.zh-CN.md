# UTF-8 正则表达式

[English](REGEX.md) · [README](../README.zh-CN.md)

当需要校验半结构化文本（如设备编号、日志行、自定义键值记录）、按命名捕获组提取字段或做模板替换时，手写字符扫描容易遗漏边界，而直接引入不可控的回溯正则又可能面临灾难性回溯（ReDoS）风险。本模块提供纯 C99 自研的 UTF-8 正则表达式引擎，内置编译与执行步数／内存／超时预算保护，无需在生产环境中链接外部正则库（注：对于标准 JSON 或 YAML 等结构化文档，仍建议直接使用专用解析器）。

你可以先通过 [regex.c](../examples/regex.c) 体验完整用法：程序先编译带有命名捕获组的模式，从 `"device=Door_01 request=42"` 中查找并提取字段范围，再将整条记录替换为展示文本 `"Door_01: #42"`，最后销毁正则对象。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，构建并运行 `hx_os_regex`（无需命令行参数或输入文件）：

```text
Device: Door_01
Request: 42
Door_01: #42
```

在高频路径上，建议编译一次正则对象并在多次匹配间复用；同时请注意区分“正常未匹配”与“超出步数／内存预算”。你也可以按[最小工程配置](USAGE.zh-CN.md#c-project)将完整示例作为 `main.c` 编译运行。

## 1. 检查整个字符串

如果你只需要一次性判断整段文本是否完全符合某个正则规则，可以直接调用 `hx_os_regex_matches()`。将下面这段完整代码保存为 `main.c`，并按[最小项目](USAGE.zh-CN.md#c-project)构建。它校验设备编号 `"AB1234"` 是否符合“两个大写 ASCII 字母 + 四个数字”（`[A-Z]{2}[0-9]{4}`），正常运行时输出 `Valid`，若将输入改为 `"A1234"` 则输出 `Invalid`：

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

- `hx_os_regex_matches()` 在内部自动完成编译、整串匹配和释放。
- **区分未匹配与错误**：当正则表达式合法且执行顺利完成时，不论文本是否匹配，函数都返回 `HX_OS_OK`，并通过 `matched` 输出 `1`（匹配）或 `0`（不匹配）。只有当正则语法有误（`HX_OS_SYNTAX`）或超出执行预算（`HX_OS_LIMIT`）时，才会返回非 `HX_OS_OK` 状态。
- 若同一条规则需要反复匹配多条文本，请使用下一节的 `hx_os_regex_compile()` 预先编译一次。
- C++11 对应接口为 `hx::Regex::matches(pattern, text)`，返回 `hx::Result<bool>`，全程不使用 `try`／`catch`／`throw`，也不依赖标准库的 `std::regex`。

## 2. 提取字段和替换

完整的 [C 示例](../examples/regex.c) 演示了如何从 `"device=Door_01 request=42"` 中提取 `device` 与 `request` 字段并替换为 `"Door_01: #42"`；[C++ 示例](../examples/regex_cpp.cpp) 则演示了使用命名捕获组与反向引用查找连续重复的单词。

在已按[依赖指南](DEPENDENCIES.zh-CN.md)配置好 SDK 且开启 `HX_OS_BUILD_EXAMPLES=ON` 的构建目录中，可直接编译并运行这两个目标（Windows MSVC 请附加 `--config Release`，可执行文件位于 `bin/Release/`）：

```sh
cmake --build build --target hx_os_regex hx_os_regex_cpp
build/bin/hx_os_regex
build/bin/hx_os_regex_cpp
```

C 示例的输出如下：

```text
Device: Door_01
Request: 42
Door_01: #42
```

**生命周期与捕获组规则：**
- **线程安全复用**：`hx_os_regex_compile()` 创建的是不可变（Immutable）正则对象，可在多个线程之间安全共享并并发执行匹配；所有调用方使用完毕后再调用 `hx_os_regex_destroy()` 销毁。
- **借用原始文本的字节范围**：`hx_os_regex_find()` 返回的匹配结果记录的是原始输入文本中的字节偏移与长度（Byte Span），因此在读取捕获内容期间，原始文本的内存必须保持有效且不被修改。
- **捕获组编号**：第 `0` 组始终代表整个匹配片段；未参与本次匹配的互选分支捕获组，其 `offset` 为 `SIZE_MAX`。
- **按名称查询下标**：`hx_os_regex_capture_index()` 可将非空捕获组名转换为数字下标。若传入空字符串 `""` 或 `NULL`，返回 `HX_OS_INVALID`；若名称不存在，返回 `HX_OS_NOT_FOUND`（两者均不修改输出下标变量）。匿名捕获组请直接按数字下标访问。

在 C++11 中，包含 `<hx/Regex.hpp>` 即可使用面向对象封装：

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

其中 `match.value().group(1)` 返回借用输入文本的 `hx::StringView`；`replace()` 返回独立持有内存的 `hx::Result<hx::String>`；`split()` 则在当前调用线程内同步回调交付切分后的各个文本片段。

## 3. 支持的语法

本模块的匹配引擎由纯 C99 编写，采用带显式预算控制的回溯虚拟机（VM），运行时的递归与子程序调用状态均保存在显式分配的数据结构中，不会撑爆 C 调用栈（注：仓库构建测试中的 PCRE2 仅用作本地差分对照基准，并非生产运行时的依赖库）。

| 能力类别 | 语法示例 |
| --- | --- |
| 普通文本、字面量转义、字符范围、否定与 POSIX 字符类 | `abc`、`\Q[a]+\E`、`[a-z]`、`[^0-9]`、`[[:alpha:]]` |
| 量词重复、多分支选择、贪婪与非贪婪模式 | `*`、`+`、`?`、`{2,4}`、`{2,}`、`a\|ab`、`*?`、`+?` |
| 普通捕获组、非捕获组、命名捕获组 | `(abc)`、`(?:abc)`、`(?<name>abc)`、`(?P<name>abc)` |
| 数字反向引用与命名反向引用 | `\1`、`\g{1}`、`\k<name>`、`(?P=name)` |
| 正向／负向前瞻断言，有界正向／负向后顾断言 | `(?=abc)`、`(?<!abc)`、`(?<=a{1,3})`、`(?!abc)` |
| 原子组与占有量词（防回溯） | `(?>a*)`、`a*+`、`a{2,4}+` |
| 整模式递归与捕获组子程序调用 | `(?R)`、`(?0)`、`(?1)`、`(?-1)`、`(?+1)`、`(?&name)` |
| 捕获条件、递归条件、断言条件与 `DEFINE` 子程序块 | `(?(1)a\|b)`、`(?(name)a\|b)`、`(?(R)a\|b)`、`(?(?=a)a\|b)`、`(?(DEFINE)...)` |
| 回溯流程控制动词与标记 | `(*FAIL)`、`(*F)`、`(*ACCEPT)`、`(*COMMIT)`、`(*PRUNE)`、`(*THEN)`、`(*SKIP)`、`(*MARK:name)`、`(*SKIP:name)` |
| 行／文本锚点、词边界、重置匹配起点 | `^`、`$`、`\A`、`\z`、`\Z`、`\G`、`\b`、`\B`、`\K` |
| 全局或局部内联修饰符 | `(?i)`、`(?im-s:...)`、`(?x)`、`(?U)`、`(?n)` |
| Unicode 通用类别、字素簇与通用换行 | `\p{L}`、`\p{Nd}`、`\P{Lu}`、`\X`；`\R` 匹配换行序列 |

占有量词在 `(?U)` 下仍然贪婪。相对引用的距离必须非零。

> [!NOTE]
> **UTF-8、大小写折叠与方言细节：**
> - **字节偏移与严格 UTF-8**：输入必须是合法 UTF-8（支持按显式长度包含内嵌 `\0`），所有偏移与长度均以**字节**为单位。默认换行符为 LF（`\n`）；`\d`、`\w`、`\s` 默认按 ASCII 范围匹配，开启 `UCP` 选项后切换为按 Unicode 属性匹配。忽略大小写（`CASELESS`）、`UCP`、`\p{...}` 与 `\X` 依赖内置的 `utf8proc` 模块。正则引擎不会自动对输入做 NFC/NFD 规范化，若需要请先显式调用 [Unicode 规范化](UNICODE.zh-CN.md)。
> - **Unicode 简单大小写折叠**：忽略大小写模式（`(?i)`）采用 Unicode 默认简单大小写折叠（Simple Case Folding，即 `C` 与 `S` 映射记录），与系统 locale 无关，且不会将单字符展开为多字符（例如 `(?i)[Z-a]` 可匹配 `'A'` 与 `'z'`，但 `(?i)i` 不会匹配土耳其语 `'İ'`）。字符区间 `[...]` 的大小写折叠成员在编译期完整展开，不会只折叠区间首尾两个端点。引擎在固定版本 `utf8proc 2.12` 的基础上，补充了 [Unicode 17 CaseFolding.txt](https://www.unicode.org/Public/17.0.0/ucd/CaseFolding.txt) 中新增的三项仅简单折叠映射。
> - **高级控制流细节**：递归条件支持 `R`、`R0`/`Rn` 及 `R&name`（当同名捕获组已存在时，与 PCRE2 一致优先解析为捕获组条件）。`(*ACCEPT)` 会闭合当前活动的捕获组并立即接受当前子程序、断言或整次匹配（普通原子组 `(?>...)` 不构成局部接受边界）。控制标记名称最长 255 个 UTF-8 字节，单个模式最多包含 64 个不同标记；全局前缀指令必须位于模式主体最前端。断言内部禁止使用 `\K`；忽略大小写下的 POSIX 字符类保留 ASCII 模式与 UCP 模式的语义区分。
> - **不支持的 PCRE2 特性会显式报错**：本引擎并非 PCRE2 的完整克隆。诸如 Unicode 脚本表（Script / Script Extensions）、二进制属性表、分支重置组 `(?|...)`、同名重复捕获组、原生 Callout、JIT 编译、其他换行符约定以及 PCRE2 扩展替换语法均不在支持范围内。遇到不支持的语法、无界后顾断言或超长后顾断言时，编译器会明确返回错误，绝不会静默退化为普通字符匹配。

## 4. 上限与错误

为防止恶意或复杂正则表达式耗尽 CPU 与内存，编译阶段与执行阶段均设有明确的资源预算：

- **默认编译上限**：模式字符串最大 `32 KiB`、生成的字节码指令加字符类／区间数据合计最大 `4 MiB`、括号嵌套深度最多 `64` 层、固定次数重复上限 `1000` 次、后顾断言（Lookbehind）最大固定宽度 `255` 个码点、捕获组最多 `64` 个。通过编译选项（Options）可将计数重复上限调高至 `65,535`，后顾断言宽度调高至 `1024`。
- **编译工作量上限**：编译期设有固定的 `4,194,304` 个工作单位上限，涵盖抽象语法树（AST）构造、忽略大小写区间展开、定长分析与指令生成（子程序与捕获组宽度会被缓存，避免多次引用子程序时产生指数级分析开销）。若超出上限，编译直接返回 `HX_OS_LIMIT`，不会产生半成品对象。若分组引用数字溢出（含相对编号计算溢出）则返回 `HX_OS_RANGE`；引用不存在的分组则返回 `HX_OS_SYNTAX`。
- **默认执行上限**：单次操作最多执行 `10,000,000`（1000 万）步 VM 指令、保留的回溯栈存储上限 `8 MiB`、输入与结果文本上限 `16 MiB`、运行时递归／作用域深度上限 `64` 层、单次操作最多交付 `65,536` 个匹配或切分结果。简单的单字符、字符类或点号重复使用紧凑计数状态，不会为每个匹配字符压入独立栈帧。

如果需要在特定调用中放宽或收紧执行预算，或者设置超时截止时间（Deadline），可使用 `*_with_limits` 变体：

```c
hx_os_regex_limits limits = HX_OS_REGEX_LIMITS_INIT;
limits.max_steps = 50000000;
limits.deadline = hx_os_deadline_after_ms(200);

hx_os_status status = hx_os_regex_find_with_limits(
	regex, text, 0, &limits, &match, &error);
```

一次 `find`、`replace` 或 `split` 调用共享传入的整份预算（包括在不同起始偏移上的尝试）。截止时间与取消令牌按协作式步进检查，不作硬实时保证；预算耗尽后若调高预算重试，会从头开始一次新的匹配计算。

| 返回状态码 | 含义与处理方式 |
| --- | --- |
| `HX_OS_OK` | 执行完成：`find` 已找到匹配，或 `matches`／`full_match` 已得出明确的真／假结论 |
| `HX_OS_NOT_FOUND` | `find` 正常遍历完毕但未找到匹配；输出结构保持不变 |
| `HX_OS_SYNTAX` / `HX_OS_NOT_SUPPORTED` | 正则语法错误，或使用了当前方言／未启用 Unicode 模块不支持的特性 |
| `HX_OS_RANGE` | 分组编号或量词数字溢出（含相对分组编号计算溢出） |
| `HX_OS_LIMIT` | 编译工作量、VM 步数、递归深度、文本长度、结果数量或内存预算耗尽（**注意：不能将其视作“未匹配”**） |
| `HX_OS_TIMEOUT` / `HX_OS_CANCELLED` | 在得出匹配结论前已到达截止时间或收到取消信号 |
| `HX_OS_NO_MEMORY` | 堆内存分配失败 |

调用失败时，可通过 `hx_os_error_message(&error)` 获取带字节偏移位置的英文诊断信息。`find`、`full_match` 和 `replace` 在失败时均保证原有输出对象不被破坏，且库绝不会在内部调用 `abort()` 或强行终止业务线程。

## 5. 替换与切分

- **替换模板语法（`replace`）**：支持 `$0`／`$1` 等数字捕获组引用、`${name}` 或 `${1}` 花括号引用，以及用 `$$` 表示字面量 `$`。未参与匹配的捕获组会被替换为空字符串。替换结果使用目标 `hx_string`（`out`）绑定的分配器在临时缓冲区中构造，全部成功后才一次性替换 `out`（输入文本与替换模板本身允许借用 `out` 原有的内存）。
- **全局空匹配推进**：全量替换和分割时，只有匹配完全没有消耗输入，才会额外前进一个 Unicode 码点。`a\K` 会消耗 `a`，再返回其后的空区间，因此对 `aa` 全量替换为 `X` 会得到 `aXaX`。
- **文本切分（`split`）**：保留相邻分隔符之间的空片段，且不会将分隔符模式内部的捕获组混入切分结果。若中途因预算耗尽或回调返回非 `HX_OS_OK` 而停止，函数仍会通过输出参数报告此前已成功交付的片段数量。切分回调函数内严禁销毁当前正则对象、修改原始输入文本，也不可抛出 C++ 异常或调用 `longjmp`。
- **模块裁剪**：CMake 选项 `HX_OS_ENABLE_REGEX` 可单独关闭。基础的 UTF-8 字节与 ASCII 类匹配不依赖外部库；涉及 Unicode 属性、字素簇与忽略大小写折叠的特性则需要同时启用 `HX_OS_ENABLE_UNICODE`。各平台的构建支持边界参见[平台支持](PLATFORMS.zh-CN.md)。
