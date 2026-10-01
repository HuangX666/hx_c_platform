# 配置文档

[English](CONFIGURATION.md) · [README](../README.zh-CN.md)

这是可裁剪的同步 C99 模块，通过 C ABI 返回只读文档。应用设置管理、自动重载、环境变量修改、变量展开、默认值合并、模式校验、文件包含及保存时机属于消费者策略。可选 JSON/INI/properties 序列化及原子保存机制见 [平台便利接口](PLATFORM_UTILITIES.zh-CN.md)。模块不创建后台线程或队列。

默认后端始终是 BUILTIN，安装依赖不会自动切换解析规则。通过 options.backend 选择，并用 hx_os_config_backend_available 查询。后端关闭或格式不匹配时返回 NOT_SUPPORTED，不发布文档。

## 输入与生命周期

输入、路径、键和值统一使用 UTF-8，接受开头的 UTF-8 BOM，拒绝 UTF-16 BOM 和非法 UTF-8。旧编码先通过 hx_os_text_convert 或 hx_string_assign_encoded 转换。不回退到 ANSI 代码页，不修改进程 locale。

parse 在调用期间借用输入和选项，复制所有保留的名称和值。 load 使用普通文件 API 及其 Windows UTF-8 到 UTF-16 路径适配。失败时清空文档输出并释放本次资源。成功文档持有 context 分配，直到最后一次 release。 retain/release 配对；节点和字符串视图借用文档。读取期间须排除最后一次释放；保留引用后可以跨线程只读查询。不能 retain 已释放的指针。

视图有字节长度，存储末尾额外附加 NUL。合法转义可以产生嵌入 NUL，JSON 键名也不例外。 member 应使用视图长度，不用 strlen 代替。 XML 保留有序元素/文本子节点，属性在单独的对象里；命名空间前缀按字面保留，不提供命名空间展开或 XPath。

## 格式范围

范围外结构会失败，不会被静默展平或猜测。

| 格式 | 内置范围 | 可选后端 |
|---|---|---|
| JSON | 严格值、对象、数组、Unicode 转义及数值原文 | cJSON 1.7.19 |
| YAML | 单文档、块映射/序列、标量键和值 | libyaml 0.2.5 |
| XML | UTF-8 XML 1.0 元素/属性、混合文本、CDATA、引用 | Mini-XML 4.0.6 |
| INI | 节和字符串键值对 | inih r62 |
| TOML | 常用表、点分键、标量、数组、内联表 | tomlc17 R260821 |
| ENV | 明确的 dotenv 方言 | 内置 |
| properties | UTF-8 的 Java 风格属性语法 | 内置 |

JSON 接受顶层标量，拒绝尾随内容/逗号、非法数字及孤立代理项。数字保留原文，9007199254740993 不会在建树时先舍入为 double。

内置 YAML 支持空格缩进、块序列、序列中的映射项、引号、注释，以及可选的 --- / ... 文档标记。普通 true/false 为布尔值，null/~ 和空值为 null， JSON 风格数字为数值，其余普通值为字符串。 yes/on/日期仍是字符串，引号值也始终是字符串。多行普通/引号标量、流式集合、指令、块标量、紧凑嵌套序列及 Unicode 换行扩展需要 libyaml。两个后端都拒绝锚点、别名、复杂键和自定义标签，不展开别名，也不实施 YAML 合并语义。 libyaml 增加流式集合、多行字符串和块折叠，同时沿用相同的标量规则；已知标准标量标签也须符合该规则。这不等于声称完整支持 YAML 1.2。内置双引号支持 JSON 常用转义和 UXXXXXXXX；其余 YAML 特有转义需要 libyaml。

XML 支持预定义实体和数字字符引用，检查字符、名称及闭合标签，归一化 CR/CRLF。属性里的字面空白变为空格，引用产生的空白保留。注释和处理指令按配置树契约主动忽略。开头声明指定 XML 1.0；若有编码声明则须为 UTF-8。 DTD 和其他具名实体不支持，不加载任何外部内容。重复元素及相邻文本/CDATA 节点分别保留。这是配置数据树，不是源文件无损编辑器。

INI 支持 = 或 :、行首 # / ; 注释及前有空白的行内 ; 注释。键、值、节名两端空白裁去，值中的引号和反斜杠按字面保留。重复节标题重新打开同一个节；标量/节名冲突失败。缩进行仍是普通赋值，两个后端都关闭多行续值。 inih 原有缓冲限制未裁剪节名不超过 49 个 UTF-8 字节，编译后的单行上限为 1 MiB；适配器在截断前拒绝超限输入。内置后端采用通用配置上限。

内置 TOML 支持单行基本/字面字符串、true/false、有符号 64 位整数（十进制/十六进制/八进制/二进制及合法下划线）、浮点值、数组（含多行和尾随逗号）、点分键、表和内联表。内联表的键值项之间不换行、不加尾随逗号；数组值内部可以换行。多行字符串、数组表和日期时间需要 tomlc17。重复键/表始终失败。原生后端增加这些能力和 TOML 1.1 解析，同时受上游限制：键路径 10 段、方括号/花括号各 30 层、每个表/数组 16K 项。配置上限和上游上限同时生效。 DATETIME 节点保留源文本，包括小数精度和时区偏移。数字也保留原文，不把上游 double 再格式化回文本。

ENV 键匹配 [A-Za-z_][A-Za-z0-9_]*，允许可选的 export 前缀。值可为空、单引号、双引号或无引号；单引号为字面内容。双引号支持反斜杠、双引号、斜线、b/f/n/r/t、 uXXXX 转义，以及转义的 $、单引号和 #。无引号值开头或空白后的 # 开始注释。不执行美元变量表达式或 shell 语法。这是明确的 dotenv 方言，不假设有统一的 ENV 文件标准。

properties 支持未转义的 = / : / 空白分隔符、# / ! 注释、键/分隔符/空格转义、奇数末尾反斜杠续行及检查代理对的 Unicode 转义。注释不续行，续行开头空白被移除，值末尾空格保留。未知转义像 Java properties 一样去掉反斜杠。输入是 UTF-8，不采用部分 Java API 的传统 ISO-8859-1 默认；旧编码应先转换。

原始 ASCII 控制字符只允许 Tab/CR/LF，properties 额外允许换页符；转义后的控制字符按格式规则处理。

## 查询与错误

member 按字节精确比较并区分大小写。 pointer 对对象/数组实现 RFC 6901：空路径选根，/a~1b 选键 a/b，~0 表示波浪号。数组下标为无前导零的无符号十进制，点号不是通用路径分隔符。 XML 使用 first/next 和属性接口。at 沿相邻节点遍历，耗时线性。

缺少值返回 NOT_FOUND，类型不符返回 TYPE_MISMATCH。 get_i64/get_u64 接受整数节点及完整整数字符串，包括 TOML 进制写法；检查溢出，不截掉小数。 get_double 使用独立 C 数值 locale，可能舍入，并报告 ERANGE。 SDK 缺少独立 locale 时，该 getter 返回 NOT_SUPPORTED； CMake 会拒绝在该 SDK 上启用 CJSON/TOMLC。 get_bool 只接受 true/false，包括对应的完整字符串。 getter 失败时不修改输出值。 HX_OS_CAP_CONFIG_DOUBLE 表示是否具备上述数值转换能力。

重复键默认 EXISTS。 JSON、YAML、INI、ENV、properties 可选 FIRST/LAST； LAST 保留键原有的遍历位置。 TOML 重复键和 XML 重复属性始终失败。舍弃项仍消耗解析工作量、节点和内存预算。

错误包含复制后的 UTF-8 来源、状态、可用的原生/后端详情，以及不包含配置值的受控英文消息。位置为字节偏移及从 1 开始的 Unicode 码点行列号： CRLF 算一个换行，Tab 算一列，BOM 算一个输入码点。未知位置用 SIZE_MAX / 0 / 0，不转发可能含秘密的后端错误文本。 libyaml 字符索引转换为字节位置； TOMLC/Mini-XML 没有可靠错误位置时报告未知。 SYNTAX 为非法语法，NO_MEMORY 为分配失败，LIMIT 为配置上限；可识别的范围外能力返回 NOT_SUPPORTED。类似未支持能力的非法语法仍可能报告 SYNTAX。

## 上限与隔离

使用 hx_os_config_options_init 初始化。默认输入 1 MiB、解析内存 16 MiB、节点 65,536 个、深度 64、工作量 16,777,216 单位；节点包括构建中的临时节点。零上限非法，最大可配置深度为 128。工作量统计输入/复制/比较字节及适配器操作，不代表第三方内部指令计数或墙上时间。不承诺硬截止时间或取消能力。

内存预算包括文档、块元数据、分配包装开销、保留的临时空间，以及重定向的第三方 malloc/calloc/realloc/strdup 分配。扩容同时计算旧缓冲和新缓冲。 context 分配器自身开销、C 运行库 locale 对象、系统锁、调用方输入及 load 读取缓冲不在此预算。 load 扩容期间最多同时持有两个缓冲，各受 max_input_bytes 限制。解析后的数值 getter 可临时分配转换缓冲和 locale，不属于解析期预算。

同步第三方调用通过可嵌套的编译器线程局部变量记录分配作用域，不持有 TLS 析构、隐藏线程、队列或消费者全局设置。分配器回调允许同线程重新进入解析，但不能在原生解析中途 yield 或切换 fiber。回调仍须满足核心分配器的线程安全、对齐和生命周期契约。

第三方符号全部私有化并加前缀，不调用其全局 hook 设置函数。 cJSON 即使用带选项的 API 也会写全局错误状态，所以私有 cJSON 解析被串行化，同时允许同线程重入。内置 JSON 和只读查询不取这把锁。 Mini-XML 的私有回调表始终只读，不分配 TLS 状态。数值转换使用独立 C locale，上游源码保持原样。

cJSON 和 Mini-XML 执行原生解析，并共用严格校验和建树，以保留嵌入 NUL、JSON 大整数及 XML 归一化/混合内容语义。它们不是独立的无损 DOM，也没有更快的承诺。 libyaml、inih、tomlc17 从原生事件或数据构建结果。本模块不声称提升吞吐。

## 构建与交付

Python 3.12+ 下载锁定归档并校验 SHA256。已有源码目录保留；CMake 不下载、不修改第三方源码。

~~~sh
python tools/fetch_parsers.py cjson libyaml minixml inih tomlc
cmake -S . -B build-parsers -DHX_OS_ENABLE_CJSON=ON \
  -DHX_OS_CJSON_SOURCE_DIR=/absolute/project/.deps/cJSON-1.7.19 \
  -DHX_OS_ENABLE_LIBYAML=ON \
  -DHX_OS_LIBYAML_SOURCE_DIR=/absolute/project/.deps/libyaml-0.2.5 \
  -DHX_OS_ENABLE_MINIXML=ON \
  -DHX_OS_MINIXML_SOURCE_DIR=/absolute/project/.deps/mxml-4.0.6 \
  -DHX_OS_ENABLE_INIH=ON \
  -DHX_OS_INIH_SOURCE_DIR=/absolute/project/.deps/inih-r62 \
  -DHX_OS_ENABLE_TOMLC=ON \
  -DHX_OS_TOMLC_SOURCE_DIR=/absolute/project/.deps/tomlc17-R260821
cmake --build build-parsers --config Debug --parallel 4
ctest --test-dir build-parsers -C Debug --output-on-failure
~~~

五个后端开关默认 OFF，HX_OS_ENABLE_CONFIG 默认 ON。 CONFIG=OFF 移除各格式实现，保留 ABI 查询/错误接口， parse/load 返回 NOT_SUPPORTED。关闭 CONFIG 时启用其后端属于配置错误。嵌入式消费者可以只选内置，也可以完全裁去配置模块。

第三方对象代码私有编入静态库或 DLL/so。安装后的消费者不需要第三方头文件或独立解析器库。启用依赖的许可证一同安装，详见 THIRD_PARTY_NOTICES.zh-CN.md。 examples/configuration.c 是共用 C 示例，不依赖 C++。
