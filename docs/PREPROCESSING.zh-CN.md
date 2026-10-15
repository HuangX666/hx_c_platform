# 运行时常量、条件与文件包含

[English](PREPROCESSING.md) · [README](../README.zh-CN.md)

**先运行一个完整例子。** 运行时配置希望复用常量，并按条件保留不同文本时，先做预处理，再把输出交给 JSON 或自己的解析器。

打开 [preprocess_config.c](../examples/preprocess_config.c)。准备含 define／if 的独立文本 → preprocess_text_ex 展开 → 检查状态和 diagnostic → 使用输出 → destroy。

按[示例运行方法](../examples/README.zh-CN.md#run)启用 examples，并只构建 `hx_os_preprocess_config`。此例无命令行参数、无需输入文件。

**会得到什么：**输出包含 title=Camera、port=8080、enabled=true 的 JSON 文本。示例此时只打印文本；真实业务还要解析和校验字段，文件包含必须显式提供受限加载器。

下面的短片段用于解释接口；复制运行时以完整示例为准。也可按[最小工程配置](USAGE.zh-CN.md#c-project)把完整源码作为自己的 main。

## 1. 从一次调用开始

预处理器在运行时转换 UTF-8 配置文本，不修改 C/C++ 源码语法、不调用编译器，也不增加代码生成步骤。把模板保存为 `.hxconf` 等数据文件即可，不把它作为编译单元。

```c
hx_string text = HX_STRING_INIT;
hx_os_error error;

hx_os_status status =
	hx_os_preprocess_file("device.hxconf", &text, &error);
if (status == HX_OS_OK)
	puts(hx_string_c_str(&text));

hx_string_destroy(&text);
```

需包含 `<hx_os.h>` 和 `<stdio.h>`。C 字符串直接使用 `hx_os_preprocess_text(input, &text, &error)`；高级文本入口另接受明确字节长度、源名称、选项和可选诊断。输出使用自己的 context，失败保留原内容，不需要全局初始化。

[preprocess_config.c](../examples/preprocess_config.c) 是完整可执行程序。构建 `hx_os_preprocess_config` 后，Linux/macOS 运行 `build/bin/hx_os_preprocess_config`，Visual Studio 构建运行 `build/bin/Release/hx_os_preprocess_config.exe`。它输出合法 JSON，title 为 Camera、port 为 8080、enabled 为 true，多出的空白是正常的。配套 [device.hxconf](../examples/data/preprocess/device.hxconf) 与 [common.hxconf](../examples/data/preprocess/common.hxconf) 演示文件包含，两者一起复制后，把前者路径交给文件入口即可。需要按类型读取 JSON 值时，再把输出传给[配置解析器](CONFIGURATION.zh-CN.md)。

## 2. 语言规则

```text
#define PORT 8080
#define TITLE "Camera"
#if defined(PORT) && PORT >= 1024
{ "name": TITLE, "port": PORT }
#else
{ "name": TITLE, "port": 8080 }
#endif
```

| 指令 | 行为 |
| --- | --- |
| `#define NAME replacement` | 复制对象宏，最后一次成功定义生效 |
| `#define NAME` | 定义空替换内容，ifdef 仍能查到 |
| `#undef NAME` | 删除定义，原本不存在也可正常完成 |
| `#if expression` | 开始条件分支 |
| `#ifdef NAME`、`#ifndef NAME` | 检查定义是否存在 |
| `#elif expression` | 前面没有选中分支时才选择 |
| `#else`、`#endif` | 选择剩余分支一次，或结束本层条件 |
| `#include "relative.hxconf"` | 解析并处理另一个来源 |

`#` 必须是该行第一个非空白／注释 token。名称区分大小写，是最多 127 字节的 UTF-8 标识符，沿用已有 Lexer 规则。字符串和注释不展开宏。每个来源配对自己的条件，不能在包含文件中关闭父文件的条件。普通文本中的未定义标识符保持原样。

条件支持有符号 int64 十进制／十六进制和 true/false 字面量、`defined(NAME)` 或 `defined NAME`、一元 `+ - !`、比较、`&&`、`||` 和括号。优先级依次为一元、关系比较、相等比较、`&&`、`||`。先做文本展开再求值，不会暗中给宏加括号改变优先级。条件中的未定义名称按零处理，defined 内的名称不展开；defined/true/false 本身不能重定义。双字符运算符必须相邻，所有操作数都会被校验，包括逻辑结果不再需要的操作数。表达式嵌套固定最多 64 层。

不支持函数宏、字符串化／token 粘贴、续行、二元算术、位运算、整数后缀、字符算术、编译器／系统头包含，也不自动访问宿主变量。不支持的活动指令／运算符明确失败。未选中分支仍需词法有效的 UTF-8，但不会调用 include 或求值其中的嵌套条件。BOM 只在完整来源开头接受，不在内部片段中作为标记。

## 3. 明确的文件访问

**文本入口默认不读取文件**，活动 include 在未提供 resolver 时返回 `HX_OS_NOT_SUPPORTED`。**文件入口提供相对文件 resolver**，以被包含语句所在文件的目录为基础，Windows UTF-8 转换由库内部处理。

内置 resolver 拒绝绝对包含名、冒号语法和 `..` 分量，接受两种斜杠分隔符，会跟随文件系统链接。它是有界的可信配置访问，不是安全沙箱。应用资源表、包内资源或指定文件集合可使用自定义 resolver；`HX_OS_PREPROCESS_NO_INCLUDES` 即使有 resolver 也会关闭包含。只有文件入口依赖 FILE_UTILS。

resolver 接收父标识、请求名称和剩余字节额度，填充已初始化的 `hx_string`，必须遵守额度，不保留输出指针、不更换 context，也不销毁 owner。库校验返回长度、UTF-8 及非空、最多 4,096 字节的解析后标识。稳定标识用于循环检查，深度／字节额度还会限制不能直接比较出重复的别名。回调副作用不回滚。文件和 resolver 同步执行，没有硬截止时间，也不会自动启动工作线程。

## 4. C++11 与诊断

包含 `<hx/Preprocessor.hpp>` 和 `<cstdio>`，在返回 `hx_os_status` 的函数中可以这样写：

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

成功路径最后返回 `HX_OS_OK`。`Preprocessor::text()` 接收 StringView，两种方法都返回自己拥有数据的 String，不使用异常。选项、诊断和 context 可选，普通调用只需要输入。

C 诊断用 `HX_OS_PREPROCESS_DIAGNOSTIC_INIT` 或初始化函数设置，最后释放。每次调用清除旧诊断，失败提供来源及能够确定的、从 1 开始的调用处行列。description 借用库静态文本；来源名尽力复制，OOM 时可能为空，诊断失败不会替换主要状态／原生错误。诊断不能与输入、源名称或输出重叠，字符串／诊断存活期间需要保持实现库已加载。

输出可能增加空格和空行，以保持 token 边界、分隔包含内容，不是保留排版的编辑器或完整源码映射。Lexer 默认返回换行 token，下游把换行当空白时应开启 `HX_OS_LEXER_SKIP_NEWLINES`，配置解析器则按格式处理空白。

## 5. 上限和所有权

| 选项 | 默认 | 最大允许配置 |
| --- | --- | --- |
| 每来源 `max_input_bytes` | 16 MiB | 64 MiB |
| 来源累计 `max_total_input_bytes` | 64 MiB | 64 MiB |
| `max_output_bytes` | 16 MiB | 64 MiB |
| `max_defines` | 256 | 4,096 |
| 名称／替换保留量 `max_macro_bytes` | 1 MiB | 64 MiB |
| 累计处理替换量 `max_expanded_bytes` | 64 MiB | 64 MiB |
| `max_expansions`、`max_directives` | 各 65,536 | 各 1,048,576 |
| 包含根来源的 `max_include_depth` | 16 | 64 |
| `max_expansion_depth` | 32 | 64 |
| 每来源 `max_condition_depth` | 64 | 64 |

使用 `HX_OS_PREPROCESS_OPTIONS_INIT` 或带容量检查的初始化函数，零上限无效。初始定义数量要装得进 max_defines，名称／值只借用到调用结束，会复制到本次私有宏表。不同调用的定义互不影响，并发调用也如此。每次处理替换内容都消耗累计展开字节额度，原有 Lexer 的 token 大小／数量上限也继续生效。

来源／展开栈按需增长且受限，宏复用 PARAMETERS。这些是数据／操作上限，不是精确进程 RSS；分配头、倍增容量、Lexer 副本、原有输出和临时缓冲有另外的有界库内开销，自定义 resolver 内部由回调负责。嵌入式应用可以按实际需要降低限制。

成功替换输出，失败释放临时字符串、Lexer、帧和宏表并保留输出。调用方串行访问自己的输出／诊断，让回调、context 和输入覆盖本次调用。模块不改变进程 locale、环境、工作目录或全局配置。`HX_OS_ENABLE_PREPROCESSOR` 默认开启，还需 LEXER、PARAMETERS，`hx_os_preprocess_available()` 查询组合可用性；缺少模块时返回 NOT_SUPPORTED，不新增第三方依赖。
