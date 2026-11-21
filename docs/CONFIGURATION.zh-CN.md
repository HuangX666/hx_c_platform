# 配置文档

[English](CONFIGURATION.md) · [README](../README.zh-CN.md)

当应用在启动阶段需要从配置文件或内存文本中加载参数，并在运行期以只读树形结构安全地查询端口、开关、嵌套对象或数组时，配置文档模块提供了统一的 C99／C++11 接口，一次覆盖 **JSON、YAML、XML、INI、TOML、ENV（dotenv）和 Java 风格 properties** 七种格式。

你可以先通过内置示例体验完整流程：打开 [configuration.c](../examples/configuration.c)，程序调用 `hx_os_config_parse()` 解析一段内置 TOML 文本，按路径找到 `server.port` 节点，调用 `hx_os_config_node_get_i64()` 读取整数值，最后调用 `hx_os_config_release()` 释放文档。按[示例运行方法](../examples/README.zh-CN.md#run)启用示例后，单独构建并运行 `hx_os_configuration`（无需命令行参数或外部输入文件）：

```text
Server port: 8080
```

若要直接从磁盘文件加载，将 `parse` 替换为 `hx_os_config_load()` 即可（读取 `config.json` 的完整示例见[读取 JSON 的完整程序](USAGE.zh-CN.md#configuration)，也可按[最小工程配置](USAGE.zh-CN.md#c-project)将示例源码作为 `main.c` 编译）。

**模块定位与延伸阅读：**
- **不可变只读快照**：每次解析或加载都会生成一份**只读文档（Immutable Document）**。磁盘原文件后续被外部修改不会自动改变内存中已加载的文档；需要刷新时请重新加载。
- **修改配置与结构体序列化**：本页重点说明七种格式的语法支持范围、解析后端选择以及生命周期契约。如果你需要在内存中就地增删改节点并保存回文件，请阅读 [配置文档编辑](CONFIG_EDIT.zh-CN.md) 或 [设置教程](USAGE.zh-CN.md#settings)；如果需要将 C/C++ 业务结构体与 JSON／YAML／TOML 等格式双向转换，请阅读 [序列化教程](SERIALIZATION.zh-CN.md)。
- **默认内置后端**：所有格式默认均使用纯 C99 的内置解析器（`HX_OS_CONFIG_BACKEND_BUILTIN`）。即使构建时启用了第三方解析库，也不会隐式改变默认行为；你可以通过 `options.backend` 显式切换后端，并用 `hx_os_config_backend_available()` 检查对应后端是否已编入当前库（未启用或与格式不匹配时返回 `HX_OS_NOT_SUPPORTED`）。

## 输入与生命周期

- **统一 UTF-8 输入**：所有输入文本、文件路径、键名和字符串值均要求为合法 UTF-8。文件开头允许带有一个 UTF-8 BOM（会被自动忽略），但包含 UTF-16 BOM 或非法 UTF-8 字节序列的输入会直接报错（不会回退到本地 ANSI 代码页，也不修改进程 locale；若需处理 GB18030／Shift_JIS 等历史编码，请先用 `hx_os_text_convert()` 或 `hx_string_assign_encoded()` 转为 UTF-8）。
- **引用计数与跨线程只读共享**：
  - `hx_os_config_parse()` 在调用期间借用输入视图与 `options`，并将解析出的键名和节点值完整复制到文档内部；`hx_os_config_load()` 则使用跨平台文件 API（含 Windows UTF-8 转 UTF-16 长路径支持）读取文件。若解析失败，输出指针会被置空且当次申请的所有临时资源均被立即释放。
  - 解析成功后，文档通过自身的 `hx_context` 持有内存，直到最后一次 `hx_os_config_release()` 被调用。你可以通过 `hx_os_config_retain()`／`hx_os_config_release()` 配对管理引用计数。
  - 查询接口返回的 `hx_os_config_node *` 指针和 `hx_string_view` 均**借用自所属文档**。只要文档引用计数大于零（尚未执行最后一次 `release`），多个线程就可以完全并发地对同一份只读文档执行查询。切勿对已彻底释放的文档指针再次调用 `retain`。
- **内嵌 NUL 与 XML 树结构**：节点返回的 `hx_string_view` 包含精确的字节长度 `size` 并在底层末尾额外附带一个 `\0`。由于 JSON `\u0000` 等合法转义可能在键名或字符串值中产生内嵌 `\0`，查找对象成员时请使用带显式长度的视图而非 `strlen()`。对于 XML 文档，元素的子元素与文本／CDATA 节点按文档顺序保存在子节点链表中，而元素的属性（Attributes）则存放在独立的属性对象中；XML 命名空间前缀（如 `ns:tag`）按字面保留，不提供命名空间 URI 展开或 XPath 查询。

## 格式范围

超出下表支持范围的语法结构会明确报错，绝不会被静默展平或猜测类型：

| 格式 | 内置后端（`BUILTIN`）支持范围 | 可选原生后端（需在 CMake 显式开启） |
|---|---|---|
| **JSON** | 严格 RFC 8259 值、对象、数组、Unicode 转义及精确数值原文 | `cJSON 1.7.19` |
| **YAML** | 单文档、块级映射／序列、空 `{}`／`[]`、标量键和值 | `libyaml 0.2.5` |
| **XML** | UTF-8 XML 1.0 元素／属性、混合文本、CDATA、预定义实体与字符引用 | `Mini-XML 4.0.6` |
| **INI** | `[section]` 节标题与单行键值对 | `inih r62` |
| **TOML** | 常用表、点分键、单行标量、数组（可跨行）、单行内联表 | `tomlc17 R260821` |
| **ENV** | 明确的 `.env`（dotenv）方言 | 仅内置（`BUILTIN`） |
| **properties** | UTF-8 编码的 Java 风格 `.properties` 语法 | 仅内置（`BUILTIN`） |

**各格式详细规则与后端差异：**
- **JSON**：接受顶层标量、对象或数组；严格拒绝尾随垃圾字符、尾随逗号、非标准数字格式及孤立 UTF-16 代理项。数字节点完整保留字面原文，因此像 `9007199254740993` 这样超出 `double` 精确表示范围的 64 位大整数不会在建树阶段被提前舍入成浮点数。
- **YAML**：
  - **内置后端**：支持空格缩进、块序列（`- item`）、序列中的嵌套映射、单双引号、注释、可选的 `---`／`...` 文档边界标记，以及用于保持空集合类型往返的空流式容器 `{}` 和 `[]`。未加引号的 `true`／`false` 解析为布尔值，`null`、`~` 及空值解析为 `null`，符合 JSON 格式的数字解析为数值节点，其余未加引号标量（包括 `yes`、`on`、日期字面量）以及所有带引号标量均保持为字符串。内置双引号支持常见 JSON 转义与 `\UXXXXXXXX`。
  - **libyaml 后端**：在保持相同标量类型规则的前提下，额外支持非空流式容器（Flow Maps/Sequences）、多行普通／引号字符串、块标量（`|` 与 `>`）、指令、紧凑嵌套序列及 YAML 特有转义。
  - **共同边界**：两个后端均拒绝锚点（`&`）、别名（`*`）、复杂组合键及自定义标签，不执行别名展开或 `<<` 合并语义（并非完整的 YAML 1.2 实现）。
- **XML**：支持预定义实体（`&lt;`、`&amp;` 等）与十进制／十六进制字符引用，严格校验标签闭合与字符名称规则，并将行尾 `CR`／`CRLF` 归一化为 `LF`。属性值中的字面空白字符按 XML 规范归一化为空格（通过字符引用写入的空白则保留）。注释与处理指令（PI）不进入配置数据树。若存在开头的 `<?xml ...?>` 声明，版本须为 `1.0` 且编码声明（若有）须为 `UTF-8`。不支持 `DOCTYPE`／`DTD` 与外部实体加载。同名重复元素和相邻文本／CDATA 节点会各自保留。
- **INI**：支持 `=` 或 `:` 分隔符、行首 `#`／`;` 注释以及空白后的行内 `;` 注释。节名、键名和值两端的空白会被裁剪，值内部的引号与反斜杠按字面原样保留。重复出现的同名 `[section]` 会合并打开同一个节；但若节名与已有的同名标量键冲突则报错。两个后端均禁用多行续行。注意：`inih` 原生后端受其内部固定缓冲区限制，未裁剪的节名不得超过 `49` 个 UTF-8 字节，单行上限为 `1 MiB`（适配器会在发生截断前主动拒绝超长输入）；内置后端则遵循统一的 `options` 配置上限。节名或键名内部的空格加分号属于名称，值内的行尾注释仍按原规则识别。
- **TOML**：
  - **内置后端**：支持单行基本字符串与字面字符串、`true`／`false`、带符号 64 位整数（支持十进制、`0x`、`0o`、`0b` 及合法的数字下划线分隔）、浮点数、数组（支持跨行与尾随逗号）、点分键（Dotted Keys）、标准表 `[table]` 与单行内联表 `{ a = 1, b = 2 }`。
  - **tomlc17 后端**：额外支持多行字符串、`[[array_of_tables]]` 表数组、日期时间（`DATETIME` 节点完整保留原始文本精度与时区偏移）以及 TOML 1.1 特性，同时受上游限制（键路径最多 `10` 段、括号嵌套最多 `30` 层、单个表或数组最多 `16K` 项）。无论哪个后端，重复键或重复表定义一律报错，且数值始终保留原文而不经过 `double` 回转。
- **ENV（dotenv）**：键名需匹配 `[A-Za-z_][A-Za-z0-9_]*`，允许可选的 `export ` 前缀。值可以为空、单引号包裹（纯字面量）、双引号包裹（支持 `\\`、`\"`、`\/`、`\b`、`\f`、`\n`、`\r`、`\t`、`\uXXXX` 以及转义的 `\$`、`\'`、`\#`）或无引号（行首或空白后的 `#` 开启注释）。**不会**展开 `${VAR}` 变量或执行任何 Shell 命令。
- **properties**：支持未转义的 `=`、`:` 或空白作为键值分隔符，支持 `#` 或 `!` 行首注释、键名与分隔符的反斜杠转义、奇数个末尾反斜杠跨行续行（自动去除续行前导空白，但保留值末尾空格）以及带代理对校验的 `\uXXXX` 转义。输入要求为 UTF-8（而非早期 Java 默认的 ISO-8859-1）。
- **控制字符限制**：所有格式均拒绝未转义的原始 ASCII 控制字符（`\t`、`\r`、`\n` 除外，`properties` 额外允许换页符 `\f`）。

## 查询与错误

- **节点导航**：
  - `hx_os_config_node_member()` 按精确字节比较且区分大小写。
  - `hx_os_config_node_pointer()` 针对对象与数组实现 **RFC 6901 JSON Pointer**：空字符串 `""` 选中根节点，`"/a~1b"` 选中键名 `"a/b"`（`~0` 代表 `~`，`~1` 代表 `/`），数组下标使用无前导零的非负十进制整数（注意：普通点号 `.` 不是 JSON Pointer 的分隔符）。
  - XML 元素请使用 `first_child`／`next_sibling` 遍历子节点，并使用属性接口读取属性。按序号访问子节点的 `at()` 需要沿兄弟链表线性遍历。
- **类型安全取值（Getters）**：
  - 找不到节点返回 `HX_OS_NOT_FOUND`，节点类型不匹配返回 `HX_OS_TYPE_MISMATCH`；任何取值失败都保持调用方的输出变量原值不变。
  - `get_i64()` 与 `get_u64()` 既接受整数类型节点，也接受内容为完整整数的字符串节点（含 TOML 进制写法），严格检查溢出且绝不静默截断小数。
  - `get_double()` 使用独立的 C 数值 locale 解析浮点数（若溢出则报告范围错误）；若目标平台 SDK 缺少每次调用独立的 locale 能力，该函数返回 `HX_OS_NOT_SUPPORTED`（可通过能力位 `HX_OS_CAP_CONFIG_DOUBLE` 查询，且 CMake 会阻止在该平台上启用 `cJSON`／`tomlc17`）。
  - `get_bool()` 仅接受布尔节点或内容精确为 `"true"`／`"false"` 的字符串节点。
- **重复键策略**：默认策略为拒绝重复键并返回 `HX_OS_EXISTS`。对于 JSON、YAML、INI、ENV 和 properties，可通过 `options` 选择保留首次出现（`FIRST`）或最后一次出现（`LAST`，保留该键原有的兄弟节点排列位置）；而 TOML 重复键与 XML 重复属性始终视为语法错误。注意：被 `FIRST`／`LAST` 覆盖丢弃的项在解析过程中同样计入工作量、节点数与内存预算。
- **诊断信息与位置脱敏**：`hx_os_error` 包含复制后的 UTF-8 来源标识、状态码以及**不包含敏感配置值内容**的受控英文错误消息。行列号从 `1` 开始按 Unicode 码点计数（`CRLF` 算一次换行，`\t` 算一列，开头 BOM 算一个码点），同时提供字节偏移；无法确定位置时填写 `SIZE_MAX / 0 / 0`。语法错误返回 `HX_OS_SYNTAX`，内存不足返回 `HX_OS_NO_MEMORY`，超出预算上限返回 `HX_OS_LIMIT`，可识别的范围外特性返回 `HX_OS_NOT_SUPPORTED`。

## 上限与隔离

调用 `hx_os_config_options_init(&options)` 可初始化默认安全预算：
- **默认上限**：输入文本最大 **1 MiB**、解析期内存预算 **16 MiB**、节点总数（含建树过程中的临时节点）最多 **65,536** 个、嵌套深度最多 **64** 层（最大可调至 `128` 层，任意上限设为 `0` 均视为非法 `HX_OS_INVALID`）、解析工作量上限 **16,777,216** 单位。
- **内存统计口径**：16 MiB 解析内存预算涵盖了最终文档、内存块元数据、保留的暂存区以及被重定向到当前 `hx_context` 的第三方解析器 `malloc`／`calloc`／`realloc`／`strdup` 分配（扩容瞬间同时计入新旧两块缓冲区）。调用方传入的原始输入缓冲、`load()` 读文件时的输入缓冲以及解析完成后的 `get_double()` 临时转换缓冲不计入该解析树预算。
- **第三方符号与全局状态隔离**：所有可选第三方解析器（`cJSON`、`libyaml`、`Mini-XML`、`inih`、`tomlc17`）在编译入库时均做了私有符号前缀重命名，绝不调用上游的全局内存 Hook 设置函数，而是通过可嵌套的编译器线程局部变量（TLS）将分配请求路由到当前调用的 `hx_context`。由于上游 `cJSON` 即便在使用带选项的接口时仍会写入全局错误状态，库内部对 `cJSON` 后端的解析加了可同线程重入的互斥保护（内置 JSON 解析与只读查询完全无锁）。

## 构建与交付

五种可选第三方解析器默认全部关闭（`OFF`），而核心配置模块 `HX_OS_ENABLE_CONFIG` 默认开启（`ON`）。当你需要验证或使用全部五个可选后端时，它们会统一通过固定版本的 Git Submodule（或 `-DHX_OS_DEPENDENCY_PROVIDER=FETCHCONTENT`）自动接入，无需手动指定源码路径：

~~~sh
git submodule update --init --recursive
cmake -S . -B build-parsers \
  -DHX_OS_BUILD_EXAMPLES=ON \
  -DHX_OS_ENABLE_CJSON=ON -DHX_OS_ENABLE_LIBYAML=ON \
  -DHX_OS_ENABLE_MINIXML=ON -DHX_OS_ENABLE_INIH=ON \
  -DHX_OS_ENABLE_TOMLC=ON \
  -DHX_OS_ENABLE_CRYPTO=OFF -DHX_OS_ENABLE_ARCHIVE=OFF
cmake --build build-parsers --config Release --parallel 4
build-parsers/bin/hx_os_configuration
~~~

（在 Windows 多配置生成器如 Visual Studio 下，生成的可执行文件位于 `build-parsers/bin/Release/hx_os_configuration.exe`。）

- 若将 `HX_OS_ENABLE_CONFIG` 设为 `OFF`，所有格式的解析实现都会被裁去以减小体积，仅保留 ABI 查询入口并返回 `HX_OS_NOT_SUPPORTED`（注意：不能在关闭 `HX_OS_ENABLE_CONFIG` 的同时开启任何子解析后端）。
- 启用后的第三方解析器目标代码会私有编入最终的静态库或动态库中，下游使用者无需安装第三方头文件或额外链接解析器库；相关开源许可证会随安装包一并交付（详见 `THIRD_PARTY_NOTICES.zh-CN.md`）。
