# 运行时文本的解析辅助

[English](PARSING.md) · [README](../README.zh-CN.md)

当应用需要解析自定义的轻量配置脚本、DSL 规则或命令文本——其中既有必须提取的键值对，又有可以安全跳过的嵌套扩展块（如 `{ ... }`）——如果每次都手写字符循环、引号转义与括号配对计数，代码很容易变得冗长且脆弱。本模块在 UTF-8 词法分析器（Lexer）之上提供了一组轻量级的语法消费与括号组跳过辅助函数。

你可以先运行 [parse_script.c](../examples/parse_script.c) 查看完整效果：程序创建词法分析器后，调用 `read_title()` 依次核对 `title`、等号 `=`、带引号字符串与分号 `;`，接着调用 `hx_os_lexer_skip_group()` 一次性跳过整个 `extensions { ... }` 嵌套块，最后确认到达 `HX_OS_TOKEN_END` 并销毁分析器。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，单独构建并运行 `hx_os_parse_script`（无需命令行参数或输入文件）：

```text
Title: Camera
```

将示例中的输入字符串替换为你自己的规则文本，即可按相同模式编写各字段的读取函数；你也可以按[最小工程配置](USAGE.zh-CN.md#c-project)将该示例源码作为 `main.c` 编译。如果文本在正式解析前还需要展开 `#define` 常量、处理 `#if` 条件分支或 `#include` 包含文件，请配合[运行时预处理](PREPROCESSING.zh-CN.md)使用。

## 1. 从词法单元到简单语法

底层 Lexer 负责将运行时的 UTF-8 文本切分为标识符（Identifier）、数字（Number）、带引号字符串（String）、标点符号（Punctuation）以及按选项保留的空白与注释。解析辅助函数则在此基础上封装了“试探消费（Accept）”、“强制匹配（Expect）”、“解码字符串（Read String）”和“跳过平衡括号组（Skip Group）”等常见递归下降解析动作（注意：它们仅在运行期解析数据文本，不会改变 C/C++ 源码语法，也不需要预编译代码生成器）。

例如面对下面这段自定义配置，应用需要提取 `title` 字段，并安全忽略整个 `extensions` 块：

```text
title = "Camera";
extensions { nested [call("}")]; /* ignored */ }
```

调用 `hx_os_lexer_skip_group()` 跳过 `{ ... }` 时，词法分析器会自动识别字符串字面量与注释——因此字符串内部的 `"}"` 或注释里的符号绝不会提前闭合外层的 `extensions` 花括号，同时内嵌的 `[...]` 与 `(...)` 也会被严格检查配对。

## 2. 运行完整 C 程序

[parse_script.c](../examples/parse_script.c) 完整演示了读取 `title`、跳过 `extensions` 块并验证末尾无多余 Token 的过程：

```sh
cmake --build build --config Release --target hx_os_parse_script
# Linux / macOS：
./build/bin/hx_os_parse_script
# Windows / Visual Studio：
./build/bin/Release/hx_os_parse_script.exe
```

运行输出为 `Title: Camera`。该示例在创建 Lexer 时开启了 `HX_OS_LEXER_SKIP_NEWLINES`，使换行符像普通空格一样被自动跳过；所有解析辅助函数均直接沿用创建 Lexer 时指定的空白与注释过滤选项。

| 常见解析动作 | C 接口 | C++11 接口（`hx::Lexer`） |
| --- | --- | --- |
| 比较单个 Token 文本（不推进游标） | `hx_os_token_is` | 直接比较已获取 `Token` 的 `.text()` |
| 试探并消费可选的指定 Token | `hx_os_lexer_accept` | `lexer.accept(";")` |
| 要求并消费指定的 Token 文本 | `hx_os_lexer_expect` | `lexer.expect("title")` |
| 要求并消费指定的 Token 类型 | `hx_os_lexer_expect_kind` | `lexer.expectKind(HX_OS_TOKEN_NUMBER)` |
| 读取并解码一个带引号的字符串 | `hx_os_lexer_read_string` | `lexer.readString()` |
| 跳过一整组配对的括号块 `()`／`[]`／`{}` | `hx_os_lexer_skip_group` | `lexer.skipGroup()` |
| 顺序跳到指定 Token 并消费它 | `hx_os_lexer_skip_until` | `lexer.skipUntil(";")` |

> [!NOTE]
> **词法匹配细节：**
> - **原始字节精确匹配**：`accept`、`expect` 与 `skip_until` 比较的是单个 Token 的原始字面字节，且标识符严格区分大小写。
> - **获取去引号字符串**：若要获取去掉外层引号并完成转义解码后的字符串内容，请调用 `hx_os_lexer_read_string()`，而不是直接读取原始 Token 切片。
> - **单字符标点规则**：当前 Lexer 每次将单个标点字符切分为一个独立的标点 Token（例如 `==` 会被切分为两个连续的 `=` Token，不会自动合并成双字符运算符）。

## 3. C++11 调用

在 C++11 中，包含 `<hx/Lexer.hpp>` 即可使用面向对象封装。下面这段代码可放入返回 `hx_os_status` 的函数中（成功路径末尾返回 `HX_OS_OK`）：

```cpp
auto created = hx::Lexer::create("title = 'Camera'; { [1, 2] }");
if (!created)
	return created.status();

auto &lexer = created.value();
auto status = lexer.expect("title");
if (!status)
	return status.status();

status = lexer.expect("=");
if (!status)
	return status.status();

auto title = lexer.readString();
if (!title)
	return title.status();

status = lexer.skipUntil(";");
if (!status)
	return status.status();

status = lexer.skipGroup();
if (!status)
	return status.status();

std::puts(title.value().c_str());
```

- **生命周期独立**：`lexer.readString()` 返回独立持有内存的 `hx::Result<hx::String>`，即使后续关闭或销毁了 `lexer`，该字符串依然有效。而在 C 接口中，普通 Token 输出切片借用自 Lexer 的不可变输入缓冲区，需在销毁 Lexer 前使用。
- **如何正确检查 `lexer.accept()`**：`accept()` 返回 `hx::Result<bool>`。请先检查 `result.ok()` 确认词法扫描本身没有出错，再检查 `result.value()` 判断是否匹配到了目标符号——正常的“当前符号不匹配”会返回 `ok() == true` 且 `value() == false`（若只写 `if (result)`，判断的只是“没有发生词法错误”，而非“匹配成功”）。对已移走（Moved-from）或未初始化的 C++ `Lexer` 调用方法会返回 `HX_OS_CLOSED`。

## 4. 失败及位置推进

不同辅助函数在匹配成功或失败时的游标推进契约如下：

| 操作与结果场景 | 游标位置与输出状态 |
| --- | --- |
| `accept` 匹配成功 | 消费该 Token，输出 `matched = 1`，返回 `HX_OS_OK` |
| `accept` 不匹配（包括已处于 `EOF`） | **不移动游标**，输出 `matched = 0`，正常返回 `HX_OS_OK` |
| `expect` 遇到不匹配的 Token | **不移动游标**，不修改输出 Token，返回 `HX_OS_SYNTAX` |
| `expect` 或 `read_string` 意外遇到 `EOF` | 返回 `HX_OS_TRUNCATED_INPUT` |
| `expect_kind(HX_OS_TOKEN_END)` 在 `EOF` 处调用 | 成功返回 `HX_OS_OK` 与 `END` Token（游标保持在末尾不再推进） |
| `read_string` 内存分配或转义解码失败 | 保持输出字符串原内容与游标位置不变 |
| `skip_group` 遇到错误的闭括号或超出嵌套深度 | 返回 `HX_OS_SYNTAX` 或 `HX_OS_LIMIT`（外层已消费的前缀不再回退，出错的括号保留为下一个待读 Token） |
| `skip_group` 在闭合前到达 `EOF` | 消费完已有前缀后返回 `HX_OS_TRUNCATED_INPUT` |
| `skip_until` 找到目标 Token | 一直消费到该目标 Token（**包含**该目标 Token 本身），返回 `HX_OS_OK` |
| `skip_until` 扫描至末尾仍未找到目标 Token | 游标停在输入末尾，返回 `HX_OS_EOF` |

> [!IMPORTANT]
> **括号组跳过（`skip_group`）与平面跳过（`skip_until`）的区别：**
> - `hx_os_lexer_skip_group()` 支持 `()`、`[]` 和 `{}` 三种括号并严格校验嵌套配对。默认最大嵌套深度为 **64 层**；可通过 `hx_os_lexer_skip_group_with_limit()` 或 C++ `skipGroup(limit)` 在 **1—256 层**之间调整。内部使用固定的 256 字节栈上括号缓冲，零堆内存分配且不使用 C 函数递归。
> - `hx_os_lexer_skip_until()` 只是平面顺序扫描，**不会**识别括号层级——如果嵌套花括号内部恰好也出现了分号 `;`，`skip_until(";")` 就会停在内部的分号上。因此跳过括号块时请务必使用 `skip_group`。
> - 当 `expect` 报 `HX_OS_SYNTAX` 时，由于游标未移动，你可以直接调用 `peek` 查看当前实际是什么 Token；而当 `skip_group` 中途失败后，若想从头重新解析，请调用 `reset` 或重建 Lexer（切勿写出假设“每次检查 `END` 都会向前推进游标”的死循环）。

除 `read_string` 需要向输出 `hx_string` 写入解码文本外，其余解析辅助操作均不分配堆内存。若在构建时关闭了 `HX_OS_ENABLE_LEXER`，依赖 Lexer 的操作会返回 `HX_OS_NOT_SUPPORTED`，而纯内存切片比较 `hx_os_token_is()` 仍可正常使用。
