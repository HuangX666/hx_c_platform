# 运行时常量、条件与文件包含

[English](PREPROCESSING.md) · [README](../README.zh-CN.md)

当复杂的业务配置文件需要复用公共端口或路径常量（`#define`）、根据运行环境或开关裁剪配置片段（`#if`／`#ifdef`），或者将大型配置拆分为多个受控的相对路径子文件（`#include`）时，可以在把文本交给 JSON 或自定义解析器之前，先执行一次**有界的运行时文本预处理**。

你可以先运行 [preprocess_config.c](../examples/preprocess_config.c) 查看完整效果：程序准备了一段带有 `#define` 与 `#if` 分支的配置模板，调用 `hx_os_preprocess_text_ex()` 在内存中完成宏展开与条件裁剪，检查返回状态与诊断对象后打印最终生成的合法 JSON 文本。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，单独构建并运行 `hx_os_preprocess_config`（无需命令行参数或外部输入文件）：

```json
{
  "title": "Camera",
  "port": 8080,
  "enabled": true
}
```

在实际工程中，预处理只负责产出展开后的纯文本，随后你应将该文本继续传给 [配置解析器](CONFIGURATION.zh-CN.md) 或自己的语法解析器以读取强类型字段。你也可以按[最小工程配置](USAGE.zh-CN.md#c-project)将完整示例作为 `main.c` 编译。

## 1. 从一次调用开始

运行时预处理器处理的是应用运行期间加载的 UTF-8 数据文本（例如 `.hxconf` 配置文件），它不改变 C/C++ 编译期的源码语法，不调用系统编译器，也不增加构建期代码生成步骤。

```c
#include <hx_os.h>
#include <stdio.h>

int main(void)
{
	hx_string text = HX_STRING_INIT;
	hx_os_error error;

	hx_os_status status =
		hx_os_preprocess_file("device.hxconf", &text, &error);
	if (status == HX_OS_OK)
		puts(hx_string_c_str(&text));

	hx_string_destroy(&text);
	return status == HX_OS_OK ? 0 : 1;
}
```

- **文件入口 vs. 文本入口**：直接处理磁盘文件使用 `hx_os_preprocess_file()`；直接处理内存中的 C 字符串使用 `hx_os_preprocess_text(input, &text, &error)`；若需传入显式字节长度、自定义源名称、资源上限选项（Options）或获取详细行列号诊断信息（Diagnostic），则使用 `hx_os_preprocess_text_ex()`／`hx_os_preprocess_file_ex()`。
- **失败安全**：输出统一写入调用方已初始化的 `hx_string`，使用该字符串绑定的内存上下文；任何预处理错误都保证目标字符串保持原有内容不变。
- **体验包含文件（`#include`）**：仓库中的 [device.hxconf](../examples/data/preprocess/device.hxconf) 与 [common.hxconf](../examples/data/preprocess/common.hxconf) 演示了跨文件包含。将这两个文件复制到同一目录下，把 `device.hxconf` 的路径传给 `hx_os_preprocess_file()` 即可看到合并展开后的 JSON，随后可直接喂给[配置解析器](CONFIGURATION.zh-CN.md)。

## 2. 语言规则

预处理模板使用类 C 风格的 `#` 行首指令：

```text
#define PORT 8080
#define TITLE "Camera"
#if defined(PORT) && PORT >= 1024
{ "name": TITLE, "port": PORT }
#else
{ "name": TITLE, "port": 8080 }
#endif
```

| 支持的指令 | 行为说明 |
| --- | --- |
| `#define NAME replacement` | 定义对象宏（无参数宏）；若同名宏重复定义，以最后一次成功定义为准 |
| `#define NAME` | 定义替换内容为空的宏（在正文中展开为空，但 `#ifdef NAME` 与 `defined(NAME)` 为真） |
| `#undef NAME` | 移除宏定义（若该名称原本未定义也正常成功） |
| `#if expression` | 计算条件表达式，开启条件分支 |
| `#ifdef NAME`、`#ifndef NAME` | 判断指定宏名称当前是否已定义 |
| `#elif expression` | 当前面所有分支均未命中时，计算并尝试命中当前分支 |
| `#else`、`#endif` | 命中兜底分支（每层最多一次），或闭合当前层的条件块 |
| `#include "relative.hxconf"` | 通过文件解析器（Resolver）加载并递归预处理另一个配置来源 |

**词法与条件求值规则：**
- **指令位置与标识符**：`#` 必须是当前物理行上第一个非空白、非注释的 Token。宏名称严格区分大小写，必须是长度不超过 `127` 字节的合法 UTF-8 标识符。**字符串字面量内部与注释内部绝不会展开宏**；普通配置正文中未定义的标识符保持原样输出。每个源文件（含被 `#include` 的子文件）必须闭合自己的 `#if ... #endif`，不能在子文件中闭合父文件的条件块。宏替换文本中的注释在展开时作为空白处理，定义末尾的行注释不会吞掉使用处的后续条件。临时条件文本使用展开预算，`max_output_bytes` 只约束最终输出。
- **条件表达式语法**：支持有符号 64 位整数（十进制／十六进制）、`true`／`false` 字面量、`defined(NAME)` 或 `defined NAME`、一元运算符 `+`／`-`／`!`、大小与相等比较（`<`、`<=`、`>`、`>=`、`==`、`!=`）、逻辑与 `&&`、逻辑或 `||` 以及括号 `(...)`。运算符优先级从高到低依次为：一元运算 → 关系比较 → 相等比较 → `&&` → `||`。
- **宏展开与未定义标识符**：`#if` 表达式会先对除 `defined(...)` 目标外的标识符做文本宏展开，再按上述优先级求值（不会在背地里替宏替换内容隐式套括号）。展开后仍未定义的标识符在 `#if` 条件中按整数 `0` 处理；保留字 `defined`、`true`、`false` 禁止被 `#define` 或 `#undef`。表达式括号嵌套上限为 `64` 层，且即使逻辑短路已确定结果，剩余操作数的语法仍会被完整校验。

> [!NOTE]
> **明确不支持的 C 预处理器特性：**
> 为保持配置预处理的确定性与安全性，**不支持**带参数的函数宏、字符串化 `#`、记号粘贴 `##`、反斜杠行尾续行、二元算术与位运算（`+ - * / & | ^ << >>`）、整数类型后缀（如 `ULL`）、字符常量算术、系统尖括号包含 `<...>`，也不会自动读取宿主进程的环境变量。遇到不支持的指令或运算符会明确报错；未命中的条件分支内部虽不执行 `#include` 或求值嵌套条件，但仍要求是词法合法的 UTF-8 文本。

## 3. 明确的文件访问

预处理器对文件系统访问采取严格的显式授权策略：
- **内存文本入口默认禁止读文件**：调用 `hx_os_preprocess_text()` 时默认没有绑定文件加载器（Resolver），一旦命中活动分支中的 `#include`，会直接返回 `HX_OS_NOT_SUPPORTED`（除非你在 `options` 中显式提供了自定义 Resolver）。
- **文件入口内置相对路径加载器**：调用 `hx_os_preprocess_file()` 时，内置加载器以**当前发出 `#include` 的文件所在目录**为基准解析相对路径（内部自动处理 Windows UTF-8 路径转换）。
- **内置加载器的路径限制**：严格拒绝绝对路径、带冒号 `:` 的盘符／协议语法以及任何 `..` 父目录跳转分量，同时接受 `/` 与 `\` 分隔符并跟随文件系统符号链接。注意：这是一种面向可信配置文件的有界路径约束，并非操作系统级安全沙箱。若你想完全禁用包含，可在 `options.flags` 中设置 `HX_OS_PREPROCESS_NO_INCLUDES`。
- **自定义 Resolver 回调**：如果你的配置存储在内存资源表、压缩包或虚拟文件系统中，可以通过 `options` 注册自定义 Resolver。回调接收父来源标识、请求包含的名称以及剩余字节配额，将内容写入已初始化的 `hx_string` 并返回唯一、非空且不超过 `4,096` 字节的来源标识（用于检测循环 `#include`）。

## 4. C++11 与诊断

在 C++11 中，包含 `<hx/Preprocessor.hpp>` 即可调用 `hx::Preprocessor::file()` 或 `hx::Preprocessor::text()`，并通过可选的 `hx::PreprocessDiagnostic` 获取精确到行列号的失败原因：

```cpp
hx::PreprocessDiagnostic why;
auto text = hx::Preprocessor::file("device.hxconf", NULL, &why);
if (!text) {
	std::fprintf(stderr, "%s:%zu:%zu: %s (%s)\n",
		why.sourceName(), why.line(), why.column(),
		why.description(), hx_os_status_name(text.status()));

	return text.status();
}

std::puts(text.value().c_str());
```

- 在 C 语言中，使用 `HX_OS_PREPROCESS_DIAGNOSTIC_INIT` 初始化 `hx_os_preprocess_diagnostic`，调用结束后用 `hx_os_preprocess_diagnostic_destroy()` 释放。当预处理失败时，诊断对象会记录出错的来源文件名、从 `1` 开始的行列号以及静态英文原因描述 `description`。
- **空白与换行说明**：预处理器在展开宏和拼接 `#include` 文件时，可能会插入必要的空格与空行以防止相邻 Token 意外粘连。如果下游使用自定义 Lexer 解析且将换行视作普通空白，请为下游 Lexer 开启 `HX_OS_LEXER_SKIP_NEWLINES`；标准 JSON／YAML／TOML 配置解析器则会自动忽略这些空白。

## 5. 上限和所有权

为防止无限宏递归、炸弹式宏展开（Billion Laughs）或过深的文件包含，预处理器强制执行以下资源预算（可通过 `HX_OS_PREPROCESS_OPTIONS_INIT` 初始化后按需下调或上调，任何上限设为 `0` 均视为非法）：

| 预算选项字段 | 默认上限 | 允许配置的最大上限 |
| --- | --- | --- |
| 单个来源输入字节数 `max_input_bytes` | `16 MiB` | `64 MiB` |
| 所有包含来源累计输入字节数 `max_total_input_bytes` | `64 MiB` | `64 MiB` |
| 最终输出字节数 `max_output_bytes` | `16 MiB` | `64 MiB` |
| 同时生效的宏定义数量 `max_defines` | `256` | `4,096` |
| 宏名称与替换体保留字节数 `max_macro_bytes` | `1 MiB` | `64 MiB` |
| 累计处理的宏展开总字节数 `max_expanded_bytes` | `64 MiB` | `64 MiB` |
| 宏展开次数 `max_expansions`、指令数 `max_directives` | 各 `65,536` | 各 `1,048,576` |
| 包含文件嵌套深度 `max_include_depth`（含根文件） | `16` | `64` |
| 宏递归展开深度 `max_expansion_depth` | `32` | `64` |
| 单文件条件块嵌套深度 `max_condition_depth` | `64` | `64` |

- **每次调用完全隔离**：通过 `options` 传入的初始预定义宏名称与值仅在调用期间借用，并会被深拷贝到本次调用私有的宏表中。不同预处理调用之间（包括多线程并发调用）互不共享宏状态，也不会修改任何进程全局变量。
- **模块裁剪开关**：CMake 选项 `HX_OS_ENABLE_PREPROCESSOR` 默认开启，并依赖 `HX_OS_ENABLE_LEXER` 与 `HX_OS_ENABLE_PARAMETERS`（文件入口另需 `HX_OS_ENABLE_FILE_UTILS`）。运行期可通过 `hx_os_preprocess_available()` 查询可用性；若任一依赖模块被关闭，预处理接口会明确返回 `HX_OS_NOT_SUPPORTED`。
