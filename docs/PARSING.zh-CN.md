# 运行时文本的解析辅助

[English](PARSING.md) · [README](../README.zh-CN.md)

## 1. 从词法单元到简单语法

Lexer 把 UTF-8 输入拆成标识符、数字、带引号字符串、标点，以及按选项保留的空白／注释。解析辅助负责检查和跳过这些单元，让应用不用反复手写比较和括号计数。它们处理运行时的配置字符串或文件内容，不给 C/C++ 源码添加新语法，也不要求编译前生成步骤。

例如下面的输入，title 必须读取，extensions 可以由应用选择忽略：

```text
title = "Camera";
extensions { nested [call("}")]; /* ignored */ }
```

字符串 `"}"` 不会结束 extensions。跳过整组时，会正确区分字符串、注释和不同的括号类型。

## 2. 运行完整 C 程序

[parse_script.c](../examples/parse_script.c) 读取标题、跳过扩展块，最后确认没有多余内容：

```sh
cmake --build build --config Release --target hx_os_parse_script
# Linux / macOS：
./build/bin/hx_os_parse_script
# Windows / Visual Studio：
./build/bin/Release/hx_os_parse_script.exe
```

预期输出为 `Title: Camera`。示例开启 `HX_OS_LEXER_SKIP_NEWLINES`，把换行当作可跳过空白。其它情况下，所有辅助函数都沿用创建 Lexer 时的空白／注释选项。

| 任务 | C 入口 | C++11 入口 |
| --- | --- | --- |
| 比较单个 token，不移动位置 | `hx_os_token_is` | 比较已有 token 的 text |
| 尝试消费一个可选文本 | `hx_os_lexer_accept` | `lexer.accept(";")` |
| 要求并消费指定文本 | `hx_os_lexer_expect` | `lexer.expect("title")` |
| 要求一种 token 类型 | `hx_os_lexer_expect_kind` | `lexer.expectKind(HX_OS_TOKEN_NUMBER)` |
| 读取并解码一个带引号字符串 | `hx_os_lexer_read_string` | `lexer.readString()` |
| 跳过完整括号组 | `hx_os_lexer_skip_group` | `lexer.skipGroup()` |
| 跳到指定 token 并消费它 | `hx_os_lexer_skip_until` | `lexer.skipUntil(";")` |

“指定文本”比较的是单个 token 的原始字节，标识符区分大小写。想获得去掉引号、已处理转义的字符串内容，应使用 `read_string`。当前 Lexer 每次返回一个标点字符，`==` 是两个 `=` token，不会自动合成运算符；这些辅助入口不会悄悄改变已有词法规则。

## 3. C++11 调用

包含 `<hx/Lexer.hpp>`。下列片段用于返回 `hx_os_status` 的函数；还需包含 `<cstdio>`，成功路径最后返回 `HX_OS_OK`：

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

`readString()` 返回独立拥有数据的 `String`，关闭 Lexer 后仍有效；`native()` 则借用 C 句柄，供高级调用使用。C token 输出借用 Lexer 的不可变输入，直到销毁 Lexer。C++ `next()` 继续返回自己拥有文本的 `Token`。

`accept()` 返回 `Result<bool>`：先检查 `ok()`，再通过 `value()` 判断有没有匹配。正常的“不匹配”是 `ok()==true、value()==false`；只判断 Result 的真假，判断的是有无错误，不是是否匹配。空的或已经移走的 C++ Lexer 在这些操作中返回 `HX_OS_CLOSED`。

## 4. 失败及位置推进

| 操作／结果 | 位置和输出 |
| --- | --- |
| `accept` 匹配 | 消费该 token，`matched=1` |
| `accept` 不匹配，包括 EOF | 返回 OK、`matched=0`，不移动 |
| `expect` 不匹配 | `HX_OS_SYNTAX`，不移动，不改 token 输出 |
| `expect`／`read_string` 意外到达 EOF | `HX_OS_TRUNCATED_INPUT` |
| `expect_kind(END)` 在 EOF | 成功返回 END，不再推进 |
| `read_string` 分配／解码失败 | 保留原输出字符串及位置 |
| `skip_group` 遇到错误闭括号或过深嵌套 | 返回 SYNTAX／LIMIT，可能已消费前缀，问题括号仍是下一个 token |
| `skip_group` 缺少闭括号 | 消费已有前缀后返回 TRUNCATED_INPUT |
| `skip_until` 找到指定 token | 消费到该 token，包含该 token 本身 |
| `skip_until` 找不到指定 token | 扫描到末尾后返回 EOF |

分组跳过支持 `()`、`[]`、`{}`，嵌套类型必须对应。默认深度上限为 64；`hx_os_lexer_skip_group_with_limit` 或 C++ `skipGroup(limit)` 可以指定 1～256。内部使用固定 256 字节括号栈，不递归、不分配内存。原有输入大小、token 大小和 token 数量上限继续生效。平面扫描 `skip_until` 不能代替完整括号组跳过，它可能停在嵌套组内部的目标 token。

expect 不匹配后，可以 peek 查看下一个 token 的位置而不消费它。分组跳过失败后，要从头再试，应 reset 或重新创建 Lexer；只提高深度上限不能恢复之前已经跳过的外层括号。不要假设每次成功检查 END 都会推进位置，从而写出不退出的重试循环。

除了向独立字符串解码，这些辅助操作都不分配内存，也不引入线程或锁。同一 Lexer 的操作要串行执行；分配器回调不得重入、修改或销毁参与操作的 Lexer／目标字符串。关闭 `HX_OS_ENABLE_LEXER` 后，依赖 Lexer 的操作明确不支持；原始 token 比较仍是无状态的字节比较。
